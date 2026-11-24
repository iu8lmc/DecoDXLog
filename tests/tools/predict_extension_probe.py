#!/usr/bin/env python3
from contextlib import closing
"""Run the tracker adapter against an isolated app and simulated PREDICT UDP server."""
import datetime
import json
import os
import pathlib
import socket
import socketserver
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

APP, ROOT = sys.argv[1:]
TOKEN = "fedcba9876543210fedcba9876543210"


def checksummed(line):
    assert len(line) == 69
    total = sum(int(c) if c.isdigit() else 1 if c == '-' else 0 for c in line[:68])
    return line[:68] + str(total % 10)

class Fixture(socketserver.BaseRequestHandler):
    def handle(self):
        packet, sock = self.request
        command = packet.decode("ascii").strip()
        self.server.commands.append(command)
        if command == "GET_MODE":
            reply = "MULTI\n"
        elif command == "GET_SAT OSCAR-7":
            reply = "OSCAR-7\n151.79\n-70.04\n203.75\n20.01\n1800000000\n5988\n11881\n773\n7.47\n6292\nD\n"
        elif command == "GET_DOPPLER OSCAR-7":
            reply = "961.742249\n"
        elif command == "GET_TLE OSCAR-7":
            epoch = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=1)
            value = f"{epoch.year % 100:02}{epoch.timetuple().tm_yday:03}.00000000"
            first = checksummed(f"1 07530U 74089B   {value} -.00000043  00000-0  11708-4 0  9999")
            second = checksummed("2 07530 101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425")
            reply = f"OSCAR-7\n{first}\n{second}\n"
        else:
            reply = "Huh?\n"
        sock.sendto(reply.encode("ascii"), self.client_address)


def read_status(port):
    req = urllib.request.Request(f"http://127.0.0.1:{port}/api/v1/tracker", headers={"X-DecoDXLog-Token": TOKEN})
    with urllib.request.urlopen(req, timeout=1) as response:
        return json.load(response)


with tempfile.TemporaryDirectory(prefix="decolog-tracker-extension-") as tmp:
    root = pathlib.Path(tmp)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    settings = root / "settings" / "Decodium"
    settings.mkdir(parents=True)
    (settings / "DecoDXLog.ini").write_text(
        f"[api]\nport={port}\ntoken=0123456789abcdef0123456789abcdef\nreadOnlyToken={TOKEN}\n[backup]\nenabled=false\n")
    with socketserver.ThreadingUDPServer(("127.0.0.1", 0), Fixture) as server:
        server.commands = []
        threading.Thread(target=server.serve_forever, daemon=True).start()
        env = os.environ.copy()
        env.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", DECODXLOG_DATA_ROOT=str(root / "data"),
                   DECODXLOG_GRAB_MS="18000", DECODXLOG_READ_TOKEN=TOKEN, PYTHONDONTWRITEBYTECODE="1")
        command = [APP, "--software-renderer", "--settings", str(root / "settings"), "--db", str(root / "test.sqlite"),
                   "--port", "0", "--show", f"predictprobe:127.0.0.1:{server.server_address[1]}:OSCAR-7",
                   "--grab", str(root / "screen.png")]
        app = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            deadline = time.monotonic() + 12
            state = {}
            while time.monotonic() < deadline:
                try:
                    state = read_status(port)
                    if state.get("fresh") and state.get("orbitalFresh"):
                        break
                except (OSError, ValueError):
                    pass
                time.sleep(0.1)
            assert state.get("satellite") == "OSCAR-7" and state.get("fresh") and state.get("orbitalFresh"), state
            adapter = pathlib.Path(ROOT) / "examples/extensions/tracker_status.py"
            proc = subprocess.run([sys.executable, str(adapter), "--port", str(port)], env=env, capture_output=True, text=True, timeout=8)
            assert proc.returncode == 0, proc.stderr
            result = json.loads(proc.stdout)
            assert result["freshForReview"] and result["control"] == "none" and result["snapshot"]["satellite"] == "OSCAR-7", result
            harness = pathlib.Path(ROOT) / "tools/extension_harness.py"
            manifest = pathlib.Path(ROOT) / "examples/extensions/tracker_status.manifest.json"
            check = subprocess.run([sys.executable, str(harness), str(manifest), "--port", str(port)], env=env, capture_output=True, text=True, timeout=12)
            assert check.returncode == 0 and json.loads(check.stdout)["scope"] == "tracker_snapshot", check.stderr
            manager = pathlib.Path(ROOT) / "tools/extension_manager.py"
            managed_root = root / "extensions"
            base = [sys.executable, str(manager), "--root", str(managed_root)]
            installed = subprocess.run(base + ["install", str(manifest), "--verify-port", str(port)], env=env, capture_output=True, text=True, timeout=12)
            assert installed.returncode == 0, installed.stderr
            extension_id = json.loads(installed.stdout)["id"]
            managed = subprocess.run(base + ["run", extension_id, "--port", str(port)], env=env, capture_output=True, text=True, timeout=12)
            assert managed.returncode == 0 and json.loads(managed.stdout)["scope"] == "tracker_snapshot", managed.stderr
            removed = subprocess.run(base + ["remove", extension_id], env=env, capture_output=True, text=True, timeout=8)
            assert removed.returncode == 0, removed.stderr
            absent = subprocess.run(base + ["run", extension_id, "--port", str(port)], env=env, capture_output=True, text=True, timeout=8)
            assert absent.returncode != 0 and TOKEN not in absent.stdout + absent.stderr, absent.stdout
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                state = read_status(port)
                if not state["fresh"]:
                    break
                time.sleep(0.2)
            assert state["available"] and not state["fresh"], state
            stale = subprocess.run([sys.executable, str(adapter), "--port", str(port)], env=env, capture_output=True, text=True, timeout=8)
            assert stale.returncode == 0 and not json.loads(stale.stdout)["freshForReview"], stale.stderr
            request = urllib.request.Request(f"http://127.0.0.1:{port}/api/v1/tracker", data=b"", method="POST", headers={"X-DecoDXLog-Token": TOKEN})
            try:
                urllib.request.urlopen(request, timeout=2)
                raise AssertionError("read-only token accepted a write")
            except urllib.error.HTTPError as error:
                assert error.code == 403, error.code
            expected = ["GET_MODE", "GET_TLE OSCAR-7", "GET_SAT OSCAR-7", "GET_DOPPLER OSCAR-7", "GET_TLE OSCAR-7", "GET_MODE"]
            assert server.commands == expected, server.commands
        finally:
            app.terminate()
            output, _ = app.communicate(timeout=10)
            server.shutdown()
        with closing(sqlite3.connect(root / "test.sqlite")) as db:
            assert db.execute("SELECT COUNT(*) FROM qso").fetchone()[0] == 0
        print("PASS tracker adapter: fresh, stale, read-only, no control packets or QSO")
