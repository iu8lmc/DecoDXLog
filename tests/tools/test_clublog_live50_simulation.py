#!/usr/bin/env python3
"""End-to-end, loopback-only exercise of the one-shot 50-QSO harness."""
import http.server, json, os, pathlib, subprocess, sys, tempfile, threading
ROOT = pathlib.Path(__file__).resolve().parents[2]
LIVE50 = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT/'build/clublog_live50'
STAGE = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT/'build/clublog_stage_existing'
class Handler(http.server.BaseHTTPRequestHandler):
    requests = []
    stage = None
    checkpoint = None
    fail_status = False
    def log_message(self, *_): pass
    def respond(self, status, data):
        raw = json.dumps(data).encode(); self.send_response(status)
        self.send_header('Content-Type','application/json'); self.send_header('Content-Length', str(len(raw)))
        self.end_headers(); self.wfile.write(raw)
    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        self.requests.append(('POST', self.path, self.headers.get('Authorization'), body))
        self.respond(202, {'status':'queued','upload_id':12345})
    def do_GET(self):
        self.requests.append(('GET', self.path, self.headers.get('Authorization'), b''))
        self.checkpoint = (self.stage/'live-journal.ini').read_bytes()
        Handler.checkpoint = self.checkpoint
        if Handler.fail_status:
            return self.respond(500, {'error': {'code': 'server_error', 'message': 'test failure'}})
        if len([r for r in self.requests if r[0] == 'GET']) == 1:
            return self.respond(200, {'status':'queued','records':[]})
        rows = [{'status':'duplicate','qsoid':str(i)} for i in range(1,51)]
        self.respond(200, {'status':'finished','records':rows, 'next_cursor':None})
def tag(k,v): return f'<{k}:{len(v)}>{v}'
with tempfile.TemporaryDirectory(prefix='clublog-live50-sim-') as temp:
    p = pathlib.Path(temp); source = p/'source.adi'; directory = p/'stage'; directory.mkdir(mode=0o700)
    def row(i):
        return ''.join(tag(k,v) for k,v in [('STATION_CALLSIGN','9H1SR'),('APP_CLUBLOG_QSOID',str(i)),('CALL','K1ABC'),('QSO_DATE','20260101'),('TIME_ON','1200'),('BAND','20m'),('MODE','FT8')])+'<EOR>\n'
    source.write_text('<EOH>\n'+''.join(row(i) for i in range(1,51)))
    repeated = p/'repeated.adi'
    repeated.write_text('<EOH>\n'+''.join(row(i) for i in range(1,50))+row('0001'))
    invalid = subprocess.run([str(STAGE), str(repeated), str(p/'invalid.adi'), str(p/'invalid.json')], capture_output=True)
    assert invalid.returncode == 4 and not (p/'invalid.adi').exists()
    subprocess.run([str(STAGE), str(source),str(directory/'selected-private.adi'),str(directory/'manifest-private.json')],check=True,capture_output=True)
    Handler.stage = directory
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    env=dict(os.environ,DECODXLOG_CLUBLOG_FIXTURE_URL=f'http://127.0.0.1:{server.server_port}/api/')
    try:
        cmd=[str(LIVE50),str(directory),'--submit-once']
        process=subprocess.run(cmd,input='cl_test_fixture\n',text=True,capture_output=True,env=env,timeout=15)
        if process.returncode: raise RuntimeError(f'first run failed: {process.returncode} {process.stdout} {process.stderr}')
        receipt=json.loads((directory/'receipt.json').read_text())
        assert receipt['count']==50 and receipt['duplicate']==50 and receipt['allOriginalQsoIdsMatched']
        again=subprocess.run(cmd,input='cl_test_fixture\n',text=True,capture_output=True,env=env,timeout=5)
        assert again.returncode==6
        # Simulate a crash after receipt commit but before the queued journal
        # was cleared. A matching receipt resumes without any network call.
        (directory/'live-journal.ini').write_bytes(Handler.checkpoint)
        corrupted=dict(receipt,sha256='0'*64)
        (directory/'receipt.json').write_text(json.dumps(corrupted))
        resume=[str(LIVE50),str(directory),'--resume']
        rejected=subprocess.run(resume,input='cl_test_fixture\n',text=True,capture_output=True,env=env,timeout=5)
        assert rejected.returncode==6
        assert 'queued' in (directory/'live-journal.ini').read_text()
        (directory/'receipt.json').write_text(json.dumps(receipt))
        recovered=subprocess.run(resume,input='cl_test_fixture\n',text=True,capture_output=True,env=env,timeout=5)
        assert recovered.returncode==0 and 'no network request' in recovered.stdout
        assert 'queued' not in (directory/'live-journal.ini').read_text()
        post=[r for r in Handler.requests if r[0]=='POST'];gets=[r for r in Handler.requests if r[0]=='GET']
        assert len(post)==1 and len(gets)==2
        assert all(r[2]=='Bearer cl_test_fixture' for r in Handler.requests)
        assert post[0][1]=='/api/logs/9H1SR/uploads' and b'name="file"' in post[0][3]
        assert b'name="clear"' not in post[0][3] and post[0][3].lower().count(b'<eor>')==50
        assert all('cl_test_fixture' not in r[1] for r in Handler.requests)
        # An accepted upload whose status endpoint fails must remain queued.
        # Resume polls the same ID and must never submit the file again.
        recovery_dir = p/'status-recovery'; recovery_dir.mkdir(mode=0o700)
        subprocess.run([str(STAGE), str(source), str(recovery_dir/'selected-private.adi'), str(recovery_dir/'manifest-private.json')],check=True,capture_output=True)
        Handler.stage = recovery_dir
        Handler.requests = []
        Handler.fail_status = True
        failed = subprocess.run([str(LIVE50), str(recovery_dir), '--submit-once'], input='cl_test_fixture\n', text=True, capture_output=True, env=env, timeout=15)
        assert failed.returncode != 0
        assert 'queued' in (recovery_dir/'live-journal.ini').read_text()
        assert not (recovery_dir/'receipt.json').exists()
        Handler.fail_status = False
        resumed = subprocess.run([str(LIVE50), str(recovery_dir), '--resume'], input='cl_test_fixture\n', text=True, capture_output=True, env=env, timeout=15)
        assert resumed.returncode == 0, (resumed.stdout, resumed.stderr)
        recovered_receipt = json.loads((recovery_dir/'receipt.json').read_text())
        assert recovered_receipt['count'] == 50 and recovered_receipt['allOriginalQsoIdsMatched']
        assert len([r for r in Handler.requests if r[0] == 'POST']) == 1
        assert len([r for r in Handler.requests if r[0] == 'GET']) == 2
        print('Simulation passed: one POST, 50 IDs reconciled, repeat blocked, receipt recovery without network, HTTP 500 followed by safe resume')
    finally:
        server.shutdown();server.server_close()
