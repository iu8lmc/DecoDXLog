#!/usr/bin/env python3
from contextlib import closing
"""Historical QSO through a live loopback split-rig fixture, without RF."""
import json
import os
import pathlib
import socketserver
import sqlite3
import subprocess
import sys
import tempfile
import threading

commands = []
class Rig(socketserver.StreamRequestHandler):
    def handle(self):
        for raw in self.rfile:
            cmd = raw.decode().strip().removeprefix('+')
            commands.append(cmd)
            replies = {
                'f': 'get_freq:\nFrequency: 435250000\nRPRT 0\n',
                'm': 'get_mode:\nMode: CW\nPassband: 500\nRPRT 0\n',
                'l KEYSPD': 'get_level: KEYSPD\n22\nRPRT 0\n',
                's': 'get_split_vfo:\nSplit: 1\nTX VFO: VFOB\nRPRT 0\n',
                'i': 'get_split_freq:\nTX Frequency: 145900000\nRPRT 0\n',
                'v': 'get_vfo:\nVFO: VFOA\nRPRT 0\n',
                'j': 'get_rit:\nRIT: 0\nRPRT 0\n',
                'z': 'get_xit:\nXIT: 0\nRPRT 0\n',
            }
            self.wfile.write(replies.get(cmd, 'RPRT -11\n').encode())
            self.wfile.flush()
class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

with tempfile.TemporaryDirectory(prefix='decolog-historical-rig-') as tmp, Server(('127.0.0.1', 0), Rig) as server:
    threading.Thread(target=server.serve_forever, daemon=True).start()
    root = pathlib.Path(tmp)
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
               DECODXLOG_DATA_ROOT=str(root/'data'), DECODXLOG_GRAB_MS='8000')
    result = subprocess.run([sys.argv[1], '--software-renderer', '--settings', str(root/'settings'),
        '--db', str(root/'test.sqlite'), '--port', '0', '--rig', f'127.0.0.1:{server.server_address[1]}',
        '--show', 'historicalrigprobe', '--grab', str(root/'screen.png')], env=env, capture_output=True, text=True, timeout=25)
    server.shutdown()
    output = result.stdout + result.stderr
    if result.returncode or 'PROBE historicalProfile count=4' not in output:
        raise SystemExit(f'Historical entry did not complete: {output}; commands={commands}')
    with closing(sqlite3.connect(root/'test.sqlite')) as db:
        rows = db.execute('SELECT call,freq,band,freq_rx,band_rx,adif_extra FROM qso WHERE deleted=0 ORDER BY id').fetchall()
    if len(rows) != 4:
        raise SystemExit(f'Expected four explicit records, got {len(rows)}')
    for call, freq, band, freq_rx, band_rx, extra in rows:
        if (freq, band, freq_rx, band_rx) != (14.05, '20m', None, None):
            raise SystemExit(f'Historical {call} inherited split rig data: {(freq, band, freq_rx, band_rx)}')
        if json.loads(extra or '{}').get('APP_DECOLOG_ENTRY_CONTEXT') != 'HISTORICAL':
            raise SystemExit(f'Missing historical provenance for {call}')
    unsafe = [c for c in commands if c.split(' ', 1)[0] in {'F', 'I', 'S', 'T', 'M'}]
    if unsafe:
        raise SystemExit(f'Unexpected CAT write/PTT commands: {unsafe}')
    print(f'PASS historical QSO: {len(rows)} explicit records; split rig queries={len(commands)}, CAT writes=0')
