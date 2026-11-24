import importlib.util
import json
import pathlib
import sqlite3
import tempfile
import threading
import unittest
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('event_counter', ROOT/'examples/extensions/event_counter.py')
counter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(counter)


class EventsApi(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.headers.get('X-DecoDXLog-Token') != 'test-token':
            self.send_error(403); return
        if self.path.startswith('/api/v1/capabilities'):
            value = {'apiVersion': 1, 'permissionModel': 'extension-token', 'extensionId': 'org.decodxlog.example.event-counter',
                     'perExtensionPermissions': True, 'operations': ['qso.events'], 'events': True,
                     'eventLogIdentity': self.server.log_id}
            status = 200
        else:
            status = self.server.event_status
            value = self.server.page if status == 200 else {'error': 'event history pruned', 'prunedThrough': '7', 'highWatermark': '8'}
            if status == 200 and getattr(self.server, 'dynamic', False):
                after=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query).get('after',['0'])[0]
                if after == '1': value={'log':'log-1','events':[],'hasMore':False,'next':'1'}
        data=json.dumps(value).encode(); self.send_response(status); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)
    def log_message(self,*args): pass


class EventCounterTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.server=ThreadingHTTPServer(('127.0.0.1',0),EventsApi)
        self.server.log_id='log-1'; self.server.event_status=200
        self.server.page={'log':'log-1','events':[{'sequence':'1','qsoUuid':'q1','revision':1,'kind':'added','recordedAt':'2026-01-01T00:00:00Z'}], 'hasMore':False,'next':'1'}
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True); self.thread.start()
        self.addCleanup(self.thread.join); self.addCleanup(self.server.server_close); self.addCleanup(self.server.shutdown)
        self.path=pathlib.Path(self.temp.name)/'counter.sqlite'

    def test_replay_is_idempotent_and_gap_keeps_state(self):
        first=counter.process_page(self.path,self.server.server_port,'test-token')
        self.assertEqual(first['counts']['added'],1); self.assertEqual(first['cursor'],'1')
        self.server.page={'log':'log-1','events':[],'hasMore':False,'next':'1'}
        second=counter.process_page(self.path,self.server.server_port,'test-token')
        self.assertEqual(second['counts']['added'],1); self.assertEqual(second['cursor'],'1')
        self.server.event_status=410
        with self.assertRaises(counter.EventGap): counter.process_page(self.path,self.server.server_port,'test-token')
        with sqlite3.connect(self.path) as db:
            self.assertEqual(db.execute('SELECT cursor FROM counter_state').fetchone()[0], '1')
            self.assertEqual(db.execute("SELECT count FROM counter_counts WHERE kind='added'").fetchone()[0],1)

    def test_rejects_wrong_log_and_out_of_order_without_consuming(self):
        counter.process_page(self.path,self.server.server_port,'test-token')
        self.server.log_id='log-2'
        with self.assertRaises(ValueError): counter.process_page(self.path,self.server.server_port,'test-token')
        self.server.log_id='log-1'; self.server.page={'log':'log-1','events':[{'sequence':'1','qsoUuid':'q1','revision':1,'kind':'added','recordedAt':'2026-01-01T00:00:00Z'}], 'hasMore':False,'next':'1'}
        with self.assertRaises(ValueError): counter.process_page(self.path,self.server.server_port,'test-token')
        with sqlite3.connect(self.path) as db: self.assertEqual(db.execute('SELECT cursor FROM counter_state').fetchone()[0],'1')

    def test_two_concurrent_consumers_commit_once(self):
        self.server.dynamic=True
        barrier=threading.Barrier(3); results=[]; errors=[]
        def run():
            barrier.wait()
            try: results.append(counter.process_page(self.path,self.server.server_port,'test-token'))
            except Exception as exc: errors.append(exc)
        threads=[threading.Thread(target=run) for _ in range(2)]
        for thread in threads: thread.start()
        barrier.wait()
        for thread in threads: thread.join(10); self.assertFalse(thread.is_alive())
        self.assertFalse(errors); self.assertEqual(len(results),2)
        self.assertEqual([result['counts']['added'] for result in results],[1,1])
        with sqlite3.connect(self.path) as db: self.assertEqual(db.execute("SELECT count FROM counter_counts WHERE kind='added'").fetchone()[0],1)

    def test_malformed_page_preserves_counters(self):
        counter.process_page(self.path,self.server.server_port,'test-token')
        self.server.page={'log':'log-1','events':[{'sequence':'2','qsoUuid':'q2','revision':1,'kind':'added','recordedAt':'2026-01-01T00:00:00Z'},
                                                   {'sequence':'2','qsoUuid':'q3','revision':1,'kind':'added','recordedAt':'2026-01-01T00:00:00Z'}],
                          'hasMore':False,'next':'2'}
        with self.assertRaises(ValueError): counter.process_page(self.path,self.server.server_port,'test-token')
        with sqlite3.connect(self.path) as db:
            self.assertEqual(db.execute('SELECT cursor FROM counter_state').fetchone()[0],'1')
            self.assertEqual(db.execute("SELECT count FROM counter_counts WHERE kind='added'").fetchone()[0],1)

    def test_failure_before_cursor_commit_rolls_back_counts(self):
        counter.process_page(self.path,self.server.server_port,'test-token')
        with sqlite3.connect(self.path) as db:
            db.execute("CREATE TRIGGER injected_failure BEFORE UPDATE ON counter_state BEGIN SELECT RAISE(ABORT,'injected'); END")
        self.server.page={'log':'log-1','events':[{'sequence':'2','qsoUuid':'q2','revision':1,'kind':'added','recordedAt':'2026-01-01T00:00:00Z'}], 'hasMore':False,'next':'2'}
        with self.assertRaises(sqlite3.IntegrityError): counter.process_page(self.path,self.server.server_port,'test-token')
        with sqlite3.connect(self.path) as db:
            self.assertEqual(db.execute('SELECT cursor FROM counter_state').fetchone()[0],'1')
            self.assertEqual(db.execute("SELECT count FROM counter_counts WHERE kind='added'").fetchone()[0],1)
            db.execute('DROP TRIGGER injected_failure')
        result=counter.process_page(self.path,self.server.server_port,'test-token')
        self.assertEqual(result['cursor'],'2'); self.assertEqual(result['counts']['added'],2)

    def test_rejects_unowned_existing_database(self):
        with sqlite3.connect(self.path) as db: db.execute('CREATE TABLE user_data(value TEXT)'); db.execute("INSERT INTO user_data VALUES ('keep')")
        with self.assertRaises(ValueError): counter.process_page(self.path,self.server.server_port,'test-token')
        with sqlite3.connect(self.path) as db: self.assertEqual(db.execute('SELECT value FROM user_data').fetchone()[0],'keep')

if __name__=='__main__': unittest.main()
