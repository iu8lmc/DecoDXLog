#!/usr/bin/env python3
"""PREDICT tracker draft with a connected loopback rig; no RF hardware."""
import datetime
import os
import pathlib
import socketserver
import sqlite3
import subprocess
import sys
import tempfile
import threading

class Rig(socketserver.StreamRequestHandler):
    def handle(self):
        for raw in self.rfile:
            cmd=raw.decode().strip().removeprefix('+'); self.server.commands.append(cmd)
            replies={'f':'get_freq:\nFrequency: 435250000\nRPRT 0\n','m':'get_mode:\nMode: CW\nPassband: 500\nRPRT 0\n',
                     'l KEYSPD':'get_level: KEYSPD\n22\nRPRT 0\n','s':'get_split_vfo:\nSplit: 1\nTX VFO: VFOB\nRPRT 0\n',
                     'i':'get_split_freq:\nTX Frequency: 145900000\nRPRT 0\n','v':'get_vfo:\nVFO: VFOA\nRPRT 0\n',
                     'j':'get_rit:\nRIT: 0\nRPRT 0\n','z':'get_xit:\nXIT: 0\nRPRT 0\n'}
            self.wfile.write(replies.get(cmd,'RPRT 0\n').encode()); self.wfile.flush()
class RigServer(socketserver.ThreadingTCPServer):
    allow_reuse_address=True; daemon_threads=True

def checksum(line):
    assert len(line)==69
    return line[:68]+str(sum(int(c) if c.isdigit() else 1 if c=='-' else 0 for c in line[:68])%10)
class Tracker(socketserver.BaseRequestHandler):
    def handle(self):
        data,sock=self.request; command=data.decode('ascii').strip(); self.server.commands.append(command)
        if command=='GET_MODE': reply='MULTI\n'
        elif command=='GET_SAT OSCAR-7': reply='OSCAR-7\n151.79\n-70.04\n203.75\n20.01\n1800000000\n5988\n11881\n773\n7.47\n6292\nD\n'
        elif command=='GET_DOPPLER OSCAR-7': reply='961.742249\n'
        elif command=='GET_TLE OSCAR-7':
            now=datetime.datetime.now(datetime.timezone.utc)-datetime.timedelta(days=1); epoch=f'{now.year%100:02}{now.timetuple().tm_yday:03}.00000000'
            a=checksum(f'1 07530U 74089B   {epoch} -.00000043  00000-0  11708-4 0  9999'); b=checksum('2 07530 101.6842  94.8539 0012518  56.0919 329.7963 12.53632314989425'); reply=f'OSCAR-7\n{a}\n{b}\n'
        else: reply='Huh?\n'
        sock.sendto(reply.encode(),self.client_address)

save = '--save' in sys.argv[2:]
with tempfile.TemporaryDirectory(prefix='decolog-tracker-rig-') as tmp, RigServer(('127.0.0.1',0),Rig) as rig, socketserver.ThreadingUDPServer(('127.0.0.1',0),Tracker) as tracker:
    rig.commands=[]; tracker.commands=[]
    threading.Thread(target=rig.serve_forever,daemon=True).start(); threading.Thread(target=tracker.serve_forever,daemon=True).start()
    root=pathlib.Path(tmp); env=os.environ.copy(); env.update(QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',DECODXLOG_DATA_ROOT=str(root/'data'),DECODXLOG_GRAB_MS='8000')
    args=[sys.argv[1],'--software-renderer','--settings',str(root/'settings'),'--db',str(root/'test.sqlite'),'--port','0', '--rig',f'127.0.0.1:{rig.server_address[1]}','--show',f'{"trackerrigsaveprobe" if save else "trackerrigprobe"}:127.0.0.1:{tracker.server_address[1]}:OSCAR-7','--grab',str(root/'screen.png')]
    result=subprocess.run(args,env=env,capture_output=True,text=True,timeout=25); output=result.stdout+result.stderr
    rig.shutdown(); tracker.shutdown()
    expected=['GET_MODE','GET_TLE OSCAR-7','GET_SAT OSCAR-7','GET_DOPPLER OSCAR-7','GET_TLE OSCAR-7','GET_MODE']
    if result.returncode or 'PROBE trackerRig selections=true tracker=true connected=true' not in output or tracker.commands != expected:
        raise SystemExit(f'Probe failed: {output}; rig={rig.commands}; tracker={tracker.commands}')
    with sqlite3.connect(root/'test.sqlite') as db:
        rows=db.execute('SELECT call,freq,band,freq_rx,band_rx,prop_mode,sat_name,mode FROM qso WHERE deleted=0').fetchall()
        if save:
            assert rows==[('K1AAA',145.998596,'2m',435.004184,'70cm','SAT','OSCAR-7','CW')], rows
            assert 'PROBE predictDraftInvalid kept=true' in output and 'PROBE predictDraftSaved=true' in output, output
        else: assert not rows, rows
    if 'PROBE trackerRig selections=true tracker=true connected=true tx=145.998596 rx=435.004184' not in output:
        raise SystemExit(f'Tracker draft frequencies changed: {output}')
    writes=[c for c in rig.commands if c.split(' ',1)[0] in {'F','I','S','T','M'}]
    if writes: raise SystemExit(f'Unexpected rig writes: {writes}')
    print(f'PASS tracker draft: rig queries={len(rig.commands)}; writes=0; QSO={len(rows)}; PREDICT read-only')
