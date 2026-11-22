#!/usr/bin/env python3
"""Read-only PREDICT app probe with isolated local UDP fixtures."""
import os
import pathlib
import socketserver
import sqlite3
import subprocess
import sys
import tempfile
import threading

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
            reply = 'OSCAR-7\n1 07530U 74089B   18126.90857181 -.00000043  00000-0  11708-4 0  9999\n2 07530 101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425\n'
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
            expected_commands = ['GET_MODE'] if stopped else ['GET_MODE', 'GET_SAT OSCAR-7', 'GET_DOPPLER OSCAR-7', 'GET_TLE OSCAR-7', 'GET_MODE']
            if server.commands != expected_commands:
                raise SystemExit(f'Unexpected commands: {server.commands}')
            with sqlite3.connect(root/'test.sqlite') as db:
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
            with sqlite3.connect(root/'test.sqlite') as db:
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
        with sqlite3.connect(root/'test.sqlite') as db:
            assert db.execute('SELECT COUNT(*) FROM qso').fetchone()[0] == 0
        server.shutdown()
        Fixture.handle = original_handle
        print(f'PASS live: {server.commands}')
