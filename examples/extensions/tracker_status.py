#!/usr/bin/env python3
"""Read-only PREDICT snapshot adapter through the DecoDXLog local API."""
import argparse
import json
import math
import os
import sys
import urllib.error

from recent_activity import fetch_json


def read_tracker(port, token):
    if not 1 <= port <= 65535 or not token or not token.isascii() or any(c in token for c in "\r\n"):
        raise ValueError("invalid port or read-only token")
    caps = fetch_json(port, "/api/v1/capabilities?apiVersion=1", token)
    operations = caps.get("operations")
    if (caps.get("apiVersion") != 1 or caps.get("permissionModel") != "read-only-token"
            or not isinstance(operations, list) or "tracker.read" not in operations or "qso.write" in operations):
        raise ValueError("compatible read-only tracker access is required")
    state = fetch_json(port, "/api/v1/tracker", token)
    if state.get("source") != "predict_udp" or any(type(state.get(k)) is not bool for k in ("available", "fresh", "orbitalFresh", "busy")):
        raise ValueError("unsupported tracker status")
    if state["available"]:
        if not isinstance(state.get("satellite"), str) or not state["satellite"]:
            raise ValueError("missing satellite identity")
        for key in ("azimuth", "elevation", "doppler100MHz"):
            if type(state.get(key)) not in (int, float) or not math.isfinite(state[key]):
                raise ValueError("invalid tracker value")
        for key in ("receivedAt", "tleEpoch"):
            if not isinstance(state.get(key), str) or not state[key]:
                raise ValueError("missing tracker time")
    usable = state["available"] and state["fresh"] and state["orbitalFresh"] and not state["busy"]
    return {"scope": "tracker_snapshot", "freshForReview": usable, "control": "none", "snapshot": state}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    args = parser.parse_args()
    try:
        result = read_tracker(args.port, os.environ.get("DECODXLOG_READ_TOKEN", ""))
    except (ValueError, OSError, urllib.error.URLError) as exc:
        print(f"Tracker unavailable: {type(exc).__name__}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
