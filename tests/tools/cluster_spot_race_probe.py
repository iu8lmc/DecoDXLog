#!/usr/bin/env python3
"""DecoDXLog cluster tune against a delayed local rigctl simulator; no RF."""
import json
import os
import pathlib
import queue
import socketserver
import subprocess
import sys
import tempfile
import threading
import time

class Rig(socketserver.StreamRequestHandler):
    def handle(self):
        pending = queue.Queue()
        done = threading.Event()
        def writer():
            while not done.is_set():
                try:
                    at, reply = pending.get(timeout=0.1)
                except queue.Empty:
                    continue
                time.sleep(max(0, at-time.monotonic()))
                try:
                    self.wfile.write(reply.encode()); self.wfile.flush()
                except OSError:
                    return
        worker=threading.Thread(target=writer,daemon=True); worker.start()
        try:
            for raw in self.rfile:
                cmd=raw.decode().strip().removeprefix('+'); self.server.commands.append(cmd)
                if cmd == 'f': reply=f'get_freq:\nFrequency: {self.server.frequency}\nRPRT 0\n'
                elif cmd == 'm': reply=f'get_mode:\nMode: {self.server.mode}\nPassband: 500\nRPRT 0\n'
                elif cmd.startswith('F '):
                    self.server.frequency=int(cmd.split()[1]); reply='set_freq:\nRPRT 0\n'
                elif cmd.startswith('M '):
                    self.server.mode=cmd.split()[1]; reply='set_mode:\nRPRT 0\n'
                elif cmd == 'l KEYSPD': reply='get_level: KEYSPD\n22\nRPRT 0\n'
                else: reply='RPRT -11\n'
                pending.put((time.monotonic()+0.8,reply))
        finally:
            done.set()
class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True

with tempfile.TemporaryDirectory(prefix='decolog-cluster-race-') as tmp, Server(('127.0.0.1',0),Rig) as rig:
    rig.commands=[]; rig.frequency=7030000; rig.mode='CW'
    threading.Thread(target=rig.serve_forever,daemon=True).start()
    root=pathlib.Path(tmp); env=os.environ.copy(); env.update(QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',DECODXLOG_DATA_ROOT=str(root/'data'),DECODXLOG_GRAB_MS='4500')
    args=[sys.argv[1],'--software-renderer','--settings',str(root/'settings'),'--db',str(root/'test.sqlite'),'--port','0','--rig',f'127.0.0.1:{rig.server_address[1]}','--show','clusterraceprobe','--grab',str(root/'screen.png')]
    result=subprocess.run(args,env=env,capture_output=True,text=True,timeout=20); output=result.stdout+result.stderr
    rig.shutdown()
    prefix='PROBE cluster race '
    lines=[line.split(prefix,1)[1] for line in output.splitlines() if prefix in line]
    if result.returncode or len(lines)!=1:
        raise SystemExit(f'Probe did not finish: {output}; rig={rig.commands}')
    data=json.loads(lines[0]); writes=[c for c in rig.commands if c.startswith(('F ','M ','T '))]
    carrier=[line.split('PROBE cluster carrier ',1)[1] for line in output.splitlines() if 'PROBE cluster carrier ' in line]
    after=json.loads(carrier[0]) if len(carrier)==1 else None
    if data != {'call':'J51A','mode':'FT8','freq':'14.075300'} or after != data or writes != ['F 14074000','M PKTUSB 0']:
        raise SystemExit(f'Unexpected cluster tune: early={data}; later={after}; writes={writes}; rig={rig.commands}; output={output}')
    print(f'PASS cluster spot: early={data}, later={after}; rig writes={writes}; delayed pre-tune replies; no RF')
