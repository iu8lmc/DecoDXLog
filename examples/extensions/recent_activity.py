#!/usr/bin/env python3
"""Read-only recent-QSO sample report, using DecoDXLog's local HTTP API."""
import argparse
import collections
import json
import os
import sys
import urllib.error
import urllib.request

MAX_RESPONSE = 4 * 1024 * 1024


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def fetch_json(port, path, token):
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}{path}",
        headers={"X-DecoDXLog-Token": token, "Accept": "application/json"},
        method="GET",
    )
    with opener.open(req, timeout=5) as response:
        data = response.read(MAX_RESPONSE + 1)
    if len(data) > MAX_RESPONSE:
        raise ValueError("API response exceeds the report size limit")
    value = json.loads(data)
    if not isinstance(value, dict):
        raise ValueError("API returned an unsupported response")
    return value


def build_report(port, limit, token):
    if not 1 <= port <= 65535 or not 1 <= limit <= 500:
        raise ValueError("port must be 1–65535 and limit 1–500")
    if not token or not token.isascii() or any(c in token for c in "\r\n"):
        raise ValueError("a read-only token is required in DECODXLOG_READ_TOKEN")
    caps = fetch_json(port, "/api/v1/capabilities?apiVersion=1", token)
    operations = caps.get("operations")
    if (caps.get("apiVersion") != 1 or caps.get("permissionModel") != "read-only-token"
            or not isinstance(operations, list) or "qso.read" not in operations
            or "qso.write" in operations):
        raise ValueError("the API did not confirm a compatible read-only credential")
    data = fetch_json(port, f"/api/v1/qsos?limit={limit}", token)
    qsos = data.get("qsos")
    if not isinstance(qsos, list) or len(qsos) > limit or any(not isinstance(q, dict) for q in qsos):
        raise ValueError("the API returned an unsupported QSO list")

    def group(key):
        return dict(sorted(collections.Counter(
            str(key(q) or "(unknown)").strip() or "(unknown)" for q in qsos
        ).items()))

    return {"scope": "recent_sample", "requestedLimit": limit, "sampleSize": len(qsos),
            "byBand": group(lambda q: q.get("band")),
            "byMode": group(lambda q: q.get("submode") or q.get("mode")),
            "note": "Recent QSO sample only; not a complete-log or award report."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--limit", type=int, default=100)
    args = parser.parse_args()
    try:
        report = build_report(args.port, args.limit, os.environ.get("DECODXLOG_READ_TOKEN", ""))
    except (ValueError, OSError, urllib.error.URLError) as exc:
        # Do not print request objects, headers or token values.
        print(f"Report unavailable: {type(exc).__name__}", file=sys.stderr)
        return 1
    print(json.dumps(report, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
