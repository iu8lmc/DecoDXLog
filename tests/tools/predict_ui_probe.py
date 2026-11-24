#!/usr/bin/env python3
from contextlib import closing
"""Read-only PREDICT app probe with isolated local UDP fixtures."""
import os
import pathlib
import socketserver
import sqlite3
import subprocess
import sys
import tempfile
import threading
import datetime

def checksummed(line):
    assert len(line) == 69
    total = sum(int(c) if c.isdigit() else 1 if c == '-' else 0 for c in line[:68])
    return line[:68] + str(total % 10)

class Fixture(socketserver.BaseRequestHandler):
    def handle(self):
        data, sock = self.request
        command = data.decode('ascii').strip()
        self.server.commands.append(command)
        if self.server.stopped:
            reply = 'NONE\n'
        elif command == 'GET_MODE':
            reply = 'MULTI\n'
        elif command == 'GET_LIST':
            reply = 'OSCAR-7\nFO-29 (JAS-2)\n'
        elif command == 'GET_SAT OSCAR-7':
            reply = 'OSCAR-7\n151.79\n-70.04\n203.75\n-60.01\n1800000000\n5988\n11881\n773\n7.47\n6292\nD\n279\n-32\n360\n'
        elif command == 'GET_DOPPLER OSCAR-7':
            reply = '961.742249\n'
        elif command == 'GET_TLE OSCAR-7':
            now = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=1)
            epoch = f'{now.year % 100:02}{now.timetuple().tm_yday:03}.00000000' if getattr(self.server, 'fresh', False) else '18126.90857181'
            first = checksummed(f'1 07530U 74089B   {epoch} -.00000043  00000-0  11708-4 0  9999')
            if getattr(self.server, 'change_tle', False) and self.server.commands.count('GET_TLE OSCAR-7') > 2:
                first = checksummed(first[:25] + ('2' if first[25] != '2' else '3') + first[26:])
            second = checksummed('2 07530 101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425')
            reply = f'OSCAR-7\n{first}\n{second}\n'
        elif command.startswith('PREDICT "OSCAR-7" '):
            start = int(command.rsplit(' ', 1)[1])
            base = (1800000000 if start <= 1800000000 else 1800000600 if start <= 1800000600 else 1800001200) if getattr(self.server, 'schedule', False) else (1800000180 if start > 1800000000 else 1800000000)
            for part in (f'{base} Fri 15Jan27 08:00:00 0 183 240 22 75 2135 38914 *\n'.encode(),
                         f'{base + 60} Fri 15Jan27 08:01:00 30 174 243 25 72'.encode(),
                         b' 1794 38914 *\n', f'{base + 120} Fri 15Jan27 08:02:00 0 160 246 28 69 1517 38914 *\n'.encode(), b'\x1a\n'):
                sock.sendto(part, self.client_address)
            return
        else:
            reply = 'Huh?\n'
        sock.sendto(reply.encode('ascii'), self.client_address)

for stopped in (False, True):
    with tempfile.TemporaryDirectory(prefix='decolog-predict-ui-') as tmp:
        root = pathlib.Path(tmp)
        with socketserver.ThreadingUDPServer(('127.0.0.1', 0), Fixture) as server:
            server.commands = []
            server.stopped = stopped
            threading.Thread(target=server.serve_forever, daemon=True).start()
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                       DECODXLOG_DATA_ROOT=str(root/'data'), DECODXLOG_GRAB_MS='2800')
            command = [sys.argv[1], '--software-renderer', '--settings', str(root/'settings'),
                       '--db', str(root/'test.sqlite'), '--port', '0', '--show',
                       f'predictprobe:127.0.0.1:{server.server_address[1]}:OSCAR-7',
                       '--grab', str(root/'screen.png')]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=25)
            output = result.stdout + result.stderr
            expected = 'PROBE predict success=false' if stopped else 'PROBE predict success=true satellite=OSCAR-7 old=true rendered=true warning=true'
            if result.returncode or expected not in output:
                raise SystemExit(f'FAIL stopped={stopped}: {output}; requests={server.commands}')
            expected_commands = ['GET_MODE'] if stopped else ['GET_MODE', 'GET_TLE OSCAR-7', 'GET_SAT OSCAR-7', 'GET_DOPPLER OSCAR-7', 'GET_TLE OSCAR-7', 'GET_MODE']
            if server.commands != expected_commands:
                raise SystemExit(f'Unexpected commands: {server.commands}')
            with closing(sqlite3.connect(root/'test.sqlite')) as db:
                assert db.execute('SELECT COUNT(*) FROM qso').fetchone()[0] == 0
            server.shutdown()
            print(f'PASS stopped={stopped}, commands={server.commands}')

for stopped in (False, True):
    with tempfile.TemporaryDirectory(prefix='decolog-predict-list-') as tmp:
        root = pathlib.Path(tmp)
        with socketserver.ThreadingUDPServer(('127.0.0.1', 0), Fixture) as server:
            server.commands = []
            server.stopped = stopped
            threading.Thread(target=server.serve_forever, daemon=True).start()
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                       DECODXLOG_DATA_ROOT=str(root/'data'), DECODXLOG_GRAB_MS='2800')
            command = [sys.argv[1], '--software-renderer', '--settings', str(root/'settings'),
                       '--db', str(root/'test.sqlite'), '--port', '0', '--show',
                       f'predictlist:127.0.0.1:{server.server_address[1]}', '--grab', str(root/'screen.png')]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=25)
            output = result.stdout + result.stderr
            expected = 'PROBE predictList success=false count=0 selected=' if stopped else 'PROBE predictList success=true count=2 selected=OSCAR-7'
            if result.returncode or expected not in output:
                raise SystemExit(f'FAIL list stopped={stopped}: {output}; requests={server.commands}')
            expected_commands = ['GET_MODE'] if stopped else ['GET_MODE', 'GET_LIST', 'GET_MODE']
            if server.commands != expected_commands:
                raise SystemExit(f'Unexpected commands: {server.commands}')
            with closing(sqlite3.connect(root/'test.sqlite')) as db:
                assert db.execute('SELECT COUNT(*) FROM qso').fetchone()[0] == 0
            server.shutdown()
            print(f'PASS list stopped={stopped}, commands={server.commands}')

with tempfile.TemporaryDirectory(prefix='decolog-predict-live-') as tmp:
    root = pathlib.Path(tmp)
    with socketserver.ThreadingUDPServer(('127.0.0.1', 0), Fixture) as server:
        server.commands = []
        server.stopped = False
        original_handle = Fixture.handle
        def live_handle(handler):
            if handler.server.commands.count('GET_MODE') >= 4:
                handler.server.stopped = True
            original_handle(handler)
        Fixture.handle = live_handle
        threading.Thread(target=server.serve_forever, daemon=True).start()
        env = os.environ.copy()
        env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                   DECODXLOG_DATA_ROOT=str(root/'data'), DECODXLOG_GRAB_MS='9000')
        command = [sys.argv[1], '--software-renderer', '--settings', str(root/'settings'),
                   '--db', str(root/'test.sqlite'), '--port', '0', '--show',
                   f'predictlive:127.0.0.1:{server.server_address[1]}:OSCAR-7', '--grab', str(root/'screen.png')]
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=25)
        output = result.stdout + result.stderr
        expected = 'PROBE predictLive stopped=true rendered=false successes=2'
        if result.returncode or expected not in output:
            raise SystemExit(f'FAIL live: {output}; requests={server.commands}')
        allowed = {'GET_MODE', 'GET_SAT OSCAR-7', 'GET_DOPPLER OSCAR-7', 'GET_TLE OSCAR-7'}
        if set(server.commands) - allowed or server.commands.count('GET_SAT OSCAR-7') != 2:
            raise SystemExit(f'Unexpected live commands: {server.commands}')
        with closing(sqlite3.connect(root/'test.sqlite')) as db:
            assert db.execute('SELECT COUNT(*) FROM qso').fetchone()[0] == 0
        server.shutdown()
        Fixture.handle = original_handle
        print(f'PASS live: {server.commands}')

with tempfile.TemporaryDirectory(prefix='decolog-predict-pass-') as tmp:
    root = pathlib.Path(tmp)
    with socketserver.ThreadingUDPServer(('127.0.0.1', 0), Fixture) as server:
        server.commands = []
        server.stopped = False
        threading.Thread(target=server.serve_forever, daemon=True).start()
        env = os.environ.copy()
        env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                   DECODXLOG_DATA_ROOT=str(root/'data'), DECODXLOG_GRAB_MS='2800')
        command = [sys.argv[1], '--software-renderer', '--settings', str(root/'settings'),
                   '--db', str(root/'test.sqlite'), '--port', '0', '--show',
                   f'predictpass:127.0.0.1:{server.server_address[1]}:OSCAR-7', '--grab', str(root/'screen.png')]
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=25)
        output = result.stdout + result.stderr
        if result.returncode or 'PROBE predictPass success=true rendered=true max=30 old=true' not in output or 'PROBE predictPassNext success=true rendered=true' not in output:
            raise SystemExit(f'FAIL pass: {output}; requests={server.commands}')
        if len(server.commands) != 6 or server.commands[0] != 'GET_TLE OSCAR-7' or not server.commands[1].startswith('PREDICT "OSCAR-7" ') or server.commands[2:4] != ['GET_TLE OSCAR-7', 'GET_TLE OSCAR-7'] or server.commands[4] != 'PREDICT "OSCAR-7" 1800000121' or server.commands[5] != 'GET_TLE OSCAR-7':
            raise SystemExit(f'Unexpected pass commands: {server.commands}')
        with closing(sqlite3.connect(root/'test.sqlite')) as db:
            assert db.execute('SELECT COUNT(*) FROM qso').fetchone()[0] == 0
        server.shutdown()
        print(f'PASS pass: {server.commands}')

for change_tle in (False, True):
    with tempfile.TemporaryDirectory(prefix='decolog-predict-schedule-') as tmp:
        root = pathlib.Path(tmp)
        with socketserver.ThreadingUDPServer(('127.0.0.1', 0), Fixture) as server:
            server.commands = []
            server.stopped = False
            server.schedule = True
            server.change_tle = change_tle
            threading.Thread(target=server.serve_forever, daemon=True).start()
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                       DECODXLOG_DATA_ROOT=str(root/'data'), DECODXLOG_GRAB_MS='2800')
            command = [sys.argv[1], '--software-renderer', '--settings', str(root/'settings'),
                       '--db', str(root/'test.sqlite'), '--port', '0', '--show',
                       f'predictschedule:127.0.0.1:{server.server_address[1]}:OSCAR-7', '--grab', str(root/'screen.png')]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=25)
            output = result.stdout + result.stderr
            expected = ('PROBE predictSchedule success=false count=1 rendered=1 old=true' if change_tle
                        else 'PROBE predictSchedule success=true count=3 rendered=3 old=true')
            if not change_tle:
                expected += '\nPROBE schedule filter hidden=3 restored=3 cleared=true blocked=true\nPROBE selectedSchedule aos=1800000600 next=true\nPROBE selectedScheduleNext success=true aos=1800001200'
            if result.returncode or 'RangeError' in output or 'TypeError' in output or any(line not in output for line in expected.split('\n')):
                raise SystemExit(f'FAIL schedule change_tle={change_tle}: {output}; requests={server.commands}')
            count = 2 if change_tle else 4
            if not change_tle and server.commands[-2] != 'PREDICT "OSCAR-7" 1800000721':
                raise SystemExit(f'Following pass did not use selected LOS: {server.commands}')
            if len(server.commands) != count * 3 or any(not c.startswith(('GET_TLE OSCAR-7', 'PREDICT "OSCAR-7" ')) for c in server.commands):
                raise SystemExit(f'Unexpected schedule commands: {server.commands}')
            with closing(sqlite3.connect(root/'test.sqlite')) as db:
                assert db.execute('SELECT COUNT(*) FROM qso').fetchone()[0] == 0
            server.shutdown()
            print(f'PASS schedule change_tle={change_tle}: {server.commands}')

for fresh, save_mode, mapping in ((False, '', ''), (True, '', ''), (True, 'CW', ''), (True, 'FM', ''),
                                  (True, 'CW', 'inverting'), (True, 'CW', 'noninverting')):
    save = bool(save_mode)
    with tempfile.TemporaryDirectory(prefix='decolog-predict-draft-') as tmp:
        root = pathlib.Path(tmp)
        with socketserver.ThreadingUDPServer(('127.0.0.1', 0), Fixture) as server:
            server.commands = []
            server.stopped = False
            server.fresh = fresh
            threading.Thread(target=server.serve_forever, daemon=True).start()
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
                       DECODXLOG_DATA_ROOT=str(root/'data'), DECODXLOG_GRAB_MS='2800')
            probe = ('predictdraftlinear' if mapping == 'inverting' else 'predictdraftnoninvert' if mapping == 'noninverting'
                     else 'predictdraftfm' if save_mode == 'FM' else 'predictdraftsave' if save else 'predictdraft')
            command = [sys.argv[1], '--software-renderer', '--settings', str(root/'settings'),
                       '--db', str(root/'test.sqlite'), '--port', '0', '--show',
                       f'{probe}:127.0.0.1:{server.server_address[1]}:OSCAR-7', '--grab', str(root/'screen.png')]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=25)
            output = result.stdout + result.stderr
            expected_tx = '146.008596' if mapping else '145.998596'
            expected_rx = '434.994183' if mapping == 'inverting' else '435.014184' if mapping == 'noninverting' else '435.004184'
            expected = (f'PROBE predictDraft visible=true tx={expected_tx} rx={expected_rx} sat=OSCAR-7' if fresh
                        else 'PROBE predictDraft visible=false')
            if result.returncode or expected not in output:
                raise SystemExit(f'FAIL draft fresh={fresh}: {output}; requests={server.commands}')
            if fresh and 'PROBE predictDraftPreserved=true notice=true' not in output:
                raise SystemExit(f'FAIL draft overwrite: {output}')
            if mapping and 'PROBE predictLinear outside=true draft=false' not in output:
                raise SystemExit(f'FAIL out-of-range offset: {output}')
            if save and any(marker not in output for marker in ('PROBE predictDraftBeforeSave count=0', 'PROBE predictDraftInvalid kept=true', 'PROBE predictDraftSaved=true')):
                raise SystemExit(f'FAIL explicit save: {output}')
            with closing(sqlite3.connect(root/'test.sqlite')) as db:
                rows = db.execute('SELECT call,freq,band,freq_rx,band_rx,prop_mode,sat_name,mode,sat_mode FROM qso WHERE deleted=0').fetchall()
                if save:
                    assert rows == [('K1AAA', float(expected_tx), '2m', float(expected_rx), '70cm', 'SAT', 'OSCAR-7', save_mode, 'V/U')], rows
                else:
                    assert not rows, rows
            if save:
                verify_env = env.copy()
                verify_env.update(DECODXLOG_TEST_FIXTURE='1', DECOLOG_TEST_SATELLITE_DB=str(root/'test.sqlite'),
                                  DECOLOG_TEST_SATELLITE_MODE=save_mode, DECOLOG_TEST_SATELLITE_TX=expected_tx,
                                  DECOLOG_TEST_SATELLITE_RX=expected_rx)
                verify = subprocess.run([sys.argv[2], 'exportTrackerQsoFixture'], env=verify_env,
                                        capture_output=True, text=True, timeout=15)
                if verify.returncode:
                    raise SystemExit(f'FAIL ADIF roundtrip {save_mode}: {verify.stdout} {verify.stderr}')
            if server.commands != ['GET_MODE', 'GET_TLE OSCAR-7', 'GET_SAT OSCAR-7', 'GET_DOPPLER OSCAR-7', 'GET_TLE OSCAR-7', 'GET_MODE']:
                raise SystemExit(f'Unexpected draft commands: {server.commands}')
            server.shutdown()
            print(f'PASS draft fresh={fresh}')
