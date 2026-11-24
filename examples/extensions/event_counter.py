#!/usr/bin/env python3
"""Count QSO events through the API using a separately chosen client database."""
import argparse
import json
import os
import pathlib
import sqlite3
import sys
import urllib.error
import urllib.parse
import urllib.request

APP_ID = 0x44455854
EXTENSION_ID = 'org.decodxlog.example.event-counter'
KINDS = ('added', 'updated', 'deleted', 'confirmed')
LIMIT = 100
MAX_REPLY = 1024 * 1024
MAX_SEQUENCE = (1 << 63) - 1


class EventGap(ValueError):
    def __init__(self, pruned_through, high_watermark):
        super().__init__('event history was pruned; reconcile before choosing a new cursor')
        self.pruned_through = pruned_through
        self.high_watermark = high_watermark


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def _number(value):
    if (not isinstance(value, str) or not value.isascii() or not value.isdigit()
            or len(value) > 19 or str(int(value)) != value or int(value) > MAX_SEQUENCE):
        raise ValueError('invalid event sequence')
    return int(value)


def _get(port, token, path):
    request = urllib.request.Request(f'http://127.0.0.1:{port}{path}',
                                     headers={'X-DecoDXLog-Token': token}, method='GET')
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    try:
        with opener.open(request, timeout=5) as response:
            raw = response.read(MAX_REPLY + 1)
    except urllib.error.HTTPError as exc:
        if exc.code == 410:
            try:
                raw = exc.read(16385)
                if len(raw) > 16384: raise ValueError('gap response too large')
                detail = json.loads(raw)
                if not isinstance(detail, dict): raise ValueError('invalid gap response')
                floor = _number(detail.get('prunedThrough'))
                high = _number(detail.get('highWatermark'))
                if floor > high: raise ValueError('invalid gap boundary')
            except (OSError, ValueError, UnicodeError, RecursionError) as parse_error:
                raise ValueError('invalid event gap response') from parse_error
            raise EventGap(floor, high) from exc
        raise ValueError('event API request failed') from exc
    except OSError as exc:
        raise ValueError('event API request failed') from exc
    if len(raw) > MAX_REPLY:
        raise ValueError('event API response too large')
    try:
        result = json.loads(raw)
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise ValueError('invalid event API response') from exc
    if not isinstance(result, dict):
        raise ValueError('invalid event API response')
    return result


def process_page(state_path, port, token, accept_gap=False):
    if not 1 <= port <= 65535 or not token or not token.isascii() or any(c in token for c in '\r\n'):
        raise ValueError('invalid port or extension token')
    path = pathlib.Path(state_path)
    if path.is_symlink():
        raise ValueError('state database must not be a symlink')
    caps = _get(port, token, '/api/v1/capabilities?apiVersion=1')
    if (caps.get('apiVersion') != 1 or caps.get('permissionModel') != 'extension-token'
            or caps.get('perExtensionPermissions') is not True or caps.get('events') is not True
            or caps.get('extensionId') != EXTENSION_ID
            or caps.get('operations') != ['qso.events']):
        raise ValueError('a scoped event-only credential is required')
    log_id = caps.get('eventLogIdentity')
    if not isinstance(log_id, str) or not log_id or len(log_id) > 128:
        raise ValueError('invalid event log identity')
    with sqlite3.connect(str(path), timeout=5, isolation_level=None) as db:
        db.execute('BEGIN IMMEDIATE')
        try:
            tables = {r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")}
            app_id = db.execute('PRAGMA application_id').fetchone()[0]
            if not tables and app_id == 0:
                db.execute(f'PRAGMA application_id={APP_ID}')
                db.execute('CREATE TABLE counter_state(id INTEGER PRIMARY KEY CHECK(id=1), log_id TEXT NOT NULL, cursor TEXT NOT NULL)')
                db.execute('CREATE TABLE counter_counts(kind TEXT PRIMARY KEY, count INTEGER NOT NULL CHECK(count>=0))')
                db.execute('INSERT INTO counter_state(id,log_id,cursor) VALUES(1,?,?)', (log_id, '0'))
                db.executemany('INSERT INTO counter_counts(kind,count) VALUES(?,0)', ((kind,) for kind in KINDS))
            elif app_id != APP_ID or tables not in ({'counter_state', 'counter_counts'}, {'counter_state', 'counter_counts', 'counter_gaps'}):
                raise ValueError('state database is not owned by the event counter')
            db.execute('CREATE TABLE IF NOT EXISTS counter_gaps(id INTEGER PRIMARY KEY, from_cursor TEXT NOT NULL, pruned_through TEXT NOT NULL, high_watermark TEXT NOT NULL)')
            row = db.execute('SELECT log_id,cursor FROM counter_state WHERE id=1').fetchone()
            if not row or row[0] != log_id:
                raise ValueError('log identity changed; preserve state and reconcile')
            cursor = _number(row[1])
            query = urllib.parse.urlencode({'log': log_id, 'after': str(cursor), 'limit': str(LIMIT)})
            try:
                page = _get(port, token, '/api/v1/events?' + query)
            except EventGap as gap:
                if not accept_gap: raise
                if gap.pruned_through <= cursor: raise ValueError('inconsistent event gap')
                db.execute('INSERT INTO counter_gaps(from_cursor,pruned_through,high_watermark) VALUES(?,?,?)',
                           (str(cursor), str(gap.pruned_through), str(gap.high_watermark)))
                cursor = gap.pruned_through
                query = urllib.parse.urlencode({'log': log_id, 'after': str(cursor), 'limit': str(LIMIT)})
                page = _get(port, token, '/api/v1/events?' + query)
            events = page.get('events')
            if (page.get('log') != log_id or not isinstance(events, list) or len(events) > LIMIT
                    or type(page.get('hasMore')) is not bool):
                raise ValueError('invalid event page')
            last = cursor
            changes = {kind: 0 for kind in KINDS}
            for event in events:
                if not isinstance(event, dict): raise ValueError('invalid event')
                sequence = _number(event.get('sequence'))
                if (sequence <= last or event.get('kind') not in KINDS or not isinstance(event.get('qsoUuid'), str)
                        or not event['qsoUuid'] or type(event.get('revision')) is not int or event['revision'] < 0
                        or not isinstance(event.get('recordedAt'), str) or not event['recordedAt']):
                    raise ValueError('invalid or repeated event')
                changes[event['kind']] += 1
                last = sequence
            if _number(page.get('next')) != last or (page['hasMore'] and not events):
                raise ValueError('invalid event page cursor')
            for kind, amount in changes.items():
                db.execute('UPDATE counter_counts SET count=count+? WHERE kind=?', (amount, kind))
            db.execute('UPDATE counter_state SET cursor=? WHERE id=1', (str(last),))
            counts = dict(db.execute('SELECT kind,count FROM counter_counts'))
            if set(counts) != set(KINDS): raise ValueError('invalid counter state')
            gap_count = db.execute('SELECT COUNT(*) FROM counter_gaps').fetchone()[0]
            db.execute('COMMIT')
            return {'scope': 'event_counts', 'log': log_id, 'cursor': str(last), 'counts': counts,
                    'hasMore': page['hasMore'], 'noRecordedGaps': gap_count == 0, 'gapCount': gap_count}
        except BaseException:
            db.execute('ROLLBACK')
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--state', type=pathlib.Path, required=True)
    parser.add_argument('--accept-gap', action='store_true', help='explicitly record missing history and resume at the retained boundary')
    args = parser.parse_args()
    try:
        result = process_page(args.state, args.port, os.environ.get('DECODXLOG_READ_TOKEN', ''), accept_gap=args.accept_gap)
    except (ValueError, OSError, sqlite3.Error) as exc:
        print('Event counter unavailable: ' + type(exc).__name__, file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
