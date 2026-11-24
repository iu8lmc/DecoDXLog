from contextlib import closing
import importlib.util
import json
import os
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Event, Thread
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('extension_manager', ROOT / 'tools/extension_manager.py')
manager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manager)


class Api(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.headers.get('X-DecoDXLog-Token') != 'read-token':
            self.send_error(403); return
        if self.path.startswith('/api/v1/capabilities'):
            value = {'apiVersion': 1, 'permissionModel': 'read-only-token', 'operations': ['qso.read']}
        elif self.path.startswith('/api/v1/qsos'):
            value = {'qsos': [{'band': '20m', 'mode': 'CW'}, {'band': '40m', 'mode': 'SSB'}]}
        else:
            self.send_error(404); return
        data = json.dumps(value).encode(); self.send_response(200); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
    def log_message(self, *args): pass


class ManagerTests(unittest.TestCase):
    def test_managed_event_counter_persists_state_and_replays_safely(self):
        class EventApi(Api):
            def do_GET(self):
                if self.headers.get('X-DecoDXLog-Token') != 'read-token': self.send_error(403); return
                if self.path.startswith('/api/v1/capabilities'):
                    value={'apiVersion':1,'permissionModel':'extension-token','extensionId':'org.decodxlog.example.event-counter',
                           'perExtensionPermissions':True,'operations':['qso.events'],'events':True,'eventLogIdentity':'log-1'}
                else:
                    if getattr(self.server,'gap',False) and 'after=1' in self.path:
                        value={'error':'event history pruned','prunedThrough':'7','highWatermark':'8'}
                        data=json.dumps(value).encode(); self.send_response(410); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data); return
                    if getattr(self.server,'gap',False) and 'after=7' in self.path:
                        value={'log':'log-1','events':[{'sequence':'8','qsoUuid':'q8','revision':1,'kind':'added','recordedAt':'2026-01-01T00:00:00Z'}], 'hasMore':False,'next':'8'}
                    else:
                        value={'log':'log-1','events':[], 'hasMore':False,'next':'1'} if 'after=1' in self.path else {
                            'log':'log-1','events':[{'sequence':'1','qsoUuid':'q1','revision':1,'kind':'added','recordedAt':'2026-01-01T00:00:00Z'}], 'hasMore':False,'next':'1'}
                data=json.dumps(value).encode(); self.send_response(200); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); root=base/'extensions'; log=base/'log.sqlite'
            with closing(sqlite3.connect(log)) as db, db: db.execute('CREATE TABLE qso(call TEXT)'); db.execute("INSERT INTO qso VALUES('K1ABC')")
            server=ThreadingHTTPServer(('127.0.0.1',0),EventApi); thread=Thread(target=server.serve_forever,daemon=True); thread.start()
            try:
                manifest=ROOT/'examples/extensions/event_counter.manifest.json'; identity='org.decodxlog.example.event-counter'
                manager.install(root,manifest,token='read-token',port=server.server_port)
                state=root/identity/'.event-state.sqlite'
                self.assertFalse(state.exists(), 'preflight must not create live state')
                try:
                    state.symlink_to(log)
                except OSError:
                    pass  # Symlink creation may be unavailable on Windows.
                else:
                    with self.assertRaises(ValueError): manager.run(root,identity,'read-token',server.server_port,5)
                    state.unlink()
                first=manager.run(root,identity,'read-token',server.server_port,5)
                second=manager.run(root,identity,'read-token',server.server_port,5)
                self.assertEqual(first['counts']['added'],1); self.assertEqual(second['counts']['added'],1)
                self.assertEqual(second['cursor'],'1')
                self.assertTrue((root/identity/'.event-state.sqlite').is_file())
                server.gap=True
                with self.assertRaises(manager.harness.ExtensionEventGap): manager.run(root,identity,'read-token',server.server_port,5)
                cli=subprocess.run([sys.executable,str(ROOT/'tools/extension_manager.py'),'--root',str(root),'run',identity,'--port',str(server.server_port)],
                                   env={**os.environ,'DECODXLOG_READ_TOKEN':'read-token'},capture_output=True,timeout=10)
                self.assertNotEqual(cli.returncode,0); self.assertIn(b'Event history was pruned',cli.stderr); self.assertNotIn(b'read-token',cli.stderr)
                recovered=manager.run(root,identity,'read-token',server.server_port,5,accept_gap=True)
                self.assertFalse(recovered['noRecordedGaps']); self.assertEqual(recovered['gapCount'],1); self.assertEqual(recovered['cursor'],'8')
                manager.remove(root,identity); self.assertFalse((root/identity).exists())
                with closing(sqlite3.connect(log)) as db, db: self.assertEqual(db.execute('SELECT call FROM qso').fetchone()[0],'K1ABC')
            finally:
                server.shutdown(); server.server_close(); thread.join()

    def test_scoped_credential_must_match_manifest(self):
        class ScopedApi(Api):
            def do_GET(self):
                value = self.server.capabilities
                data = json.dumps(value).encode(); self.send_response(200); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
        with tempfile.TemporaryDirectory() as temp:
            base = pathlib.Path(temp); root = base/'extensions'; source = base/'source'; source.mkdir()
            (source/'demo.py').write_text("print('ok')\n")
            manifest = {'schemaVersion':1,'apiVersion':1,'id':'org.example.report','version':'1.0.0','entrypoint':'demo.py','permissions':['qso.read']}
            path=source/'manifest.json'; path.write_text(json.dumps(manifest)); manager.install(root,path)
            server=ThreadingHTTPServer(('127.0.0.1',0),ScopedApi); thread=Thread(target=server.serve_forever,daemon=True); thread.start()
            good={'apiVersion':1,'permissionModel':'extension-token','extensionId':manifest['id'],'perExtensionPermissions':True,'operations':['qso.read']}
            try:
                with patch.object(manager.harness,'run_extension',return_value={'scope':'recent_sample'}) as execute:
                    for caps in ({**good,'extensionId':'org.example.other'}, {**good,'operations':['qso.read','tracker.read']}, {**good,'operations':[]}, {**good,'perExtensionPermissions':False}):
                        server.capabilities=caps
                        with self.assertRaises(ValueError): manager.run(root,manifest['id'],'read-token',server.server_port,3)
                    execute.assert_not_called()
                    server.capabilities=good
                    self.assertEqual(manager.run(root,manifest['id'],'read-token',server.server_port,3)['scope'],'recent_sample')
                    execute.assert_called_once()
            finally:
                server.shutdown(); server.server_close(); thread.join()

    @unittest.skipUnless(os.name == 'posix','read-only directory lock test is POSIX-specific')
    def test_list_and_run_preserve_read_only_installation(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); root=base/'extensions'; source=base/'source'; source.mkdir()
            (source/'demo.py').write_text("print('ok')\n")
            manifest={'schemaVersion':1,'apiVersion':1,'id':'org.example.readonly','version':'1.0.0','entrypoint':'demo.py','permissions':['qso.read']}
            path=source/'manifest.json'; path.write_text(json.dumps(manifest)); manager.install(root,path)
            root.chmod(0o500)
            try:
                self.assertEqual(manager.list_installed(root)[0]['id'],manifest['id'])
                with patch.object(manager, '_verify_credential'), patch.object(manager.harness,'run_extension',return_value={'scope':'recent_sample'}):
                    self.assertEqual(manager.run(root,manifest['id'],'test',1,2),{'scope':'recent_sample'})
            finally:
                root.chmod(0o700)
    def test_running_extension_cannot_be_removed_until_it_exits(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); root=base/'extensions'; source=base/'source'; source.mkdir()
            (source/'demo.py').write_text("print('ok')\n")
            manifest={'schemaVersion':1,'apiVersion':1,'id':'org.example.running','version':'1.0.0','entrypoint':'demo.py','permissions':['qso.read']}
            path=source/'manifest.json'; path.write_text(json.dumps(manifest)); manager.install(root,path)
            started=Event(); release=Event(); outcome=[]
            def execute(*args,**kwargs):
                started.set()
                if not release.wait(5): raise ValueError('test timeout')
                return {'scope':'recent_sample'}
            def run():
                try: outcome.append(manager.run(root,manifest['id'],'test',1,2))
                except Exception as exc: outcome.append(exc)
            with patch.object(manager, '_verify_credential'), patch.object(manager.harness,'run_extension',side_effect=execute):
                thread=Thread(target=run); thread.start()
                if not started.wait(5): release.set(); thread.join(); self.fail('extension did not start')
                try:
                    with self.assertRaisesRegex(ValueError,'operation is in progress'): manager.remove(root,manifest['id'])
                finally:
                    release.set(); thread.join(5)
            self.assertFalse(thread.is_alive()); self.assertEqual(outcome,[{'scope':'recent_sample'}])
            manager.remove(root,manifest['id']); self.assertEqual(manager.list_installed(root),[])

    def test_lock_serializes_separate_processes_and_releases(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); root=base/'extensions'; root.mkdir(); source=base/'source'; source.mkdir(); ready=base/'ready'
            (source/'demo.py').write_text("print('ok')\n")
            manifest={'schemaVersion':1,'apiVersion':1,'id':'org.example.process','version':'1.0.0','entrypoint':'demo.py','permissions':['qso.read']}
            path=source/'manifest.json'; path.write_text(json.dumps(manifest))
            script=('import pathlib,sys; sys.path.insert(0,sys.argv[1]); import extension_manager as m\n'
                    'with m._operation_lock(m._root(sys.argv[2])):\n'
                    ' pathlib.Path(sys.argv[3]).write_text("ready"); sys.stdin.read(1)\n')
            process=subprocess.Popen([sys.executable,'-c',script,str(ROOT/'tools'),str(root),str(ready)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                deadline=time.monotonic()+5
                while not ready.exists() and process.poll() is None and time.monotonic()<deadline: time.sleep(0.01)
                self.assertTrue(ready.exists(),'other process did not acquire lock')
                with self.assertRaisesRegex(ValueError,'operation is in progress'): manager.install(root,path)
                process.stdin.write(b'x'); process.stdin.flush(); self.assertEqual(process.wait(timeout=5),0)
                self.assertEqual(manager.install(root,path)['id'],manifest['id'])
            finally:
                if process.poll() is None: process.kill(); process.wait(timeout=5)
                if process.stdin: process.stdin.close()
                if process.stdout: process.stdout.close()
                if process.stderr: process.stderr.close()

    def test_symlinked_manager_lock_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); root=base/'extensions'; root.mkdir(); target=base/'untouched'; target.write_text('unchanged')
            try: (root/'.extension-manager.lock').symlink_to(target)
            except (OSError,NotImplementedError): self.skipTest('symlinks unavailable')
            if os.name == 'nt':
                with self.assertRaises(ValueError): manager.list_installed(root)
            else:
                self.assertEqual(manager.list_installed(root),[])
            self.assertEqual(target.read_text(),'unchanged')

    def test_lock_is_released_when_manager_process_ends(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); root=base/'extensions'; root.mkdir(); ready=base/'ready'
            script=('import pathlib,sys; sys.path.insert(0,sys.argv[1]); import extension_manager as m\n'
                    'with m._operation_lock(m._root(sys.argv[2])):\n'
                    ' pathlib.Path(sys.argv[3]).write_text("ready"); sys.stdin.read(1)\n')
            process=subprocess.Popen([sys.executable,'-c',script,str(ROOT/'tools'),str(root),str(ready)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                deadline=time.monotonic()+5
                while not ready.exists() and process.poll() is None and time.monotonic()<deadline: time.sleep(0.01)
                self.assertTrue(ready.exists(),'other process did not acquire lock')
                with self.assertRaisesRegex(ValueError,'operation is in progress'): manager.list_installed(root)
                process.kill(); process.wait(timeout=5)
                self.assertEqual(manager.list_installed(root),[])
            finally:
                if process.poll() is None: process.kill(); process.wait(timeout=5)
                if process.stdin: process.stdin.close()
                if process.stdout: process.stdout.close()
                if process.stderr: process.stderr.close()
    def test_remove_cannot_delete_an_install_in_progress(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); source=base/'source'; source.mkdir(); root=base/'extensions'
            log=base/'log.sqlite'
            with closing(sqlite3.connect(log)) as db, db: db.execute('CREATE TABLE qso(call TEXT)'); db.execute("INSERT INTO qso VALUES('K1ABC')")
            (source/'demo.py').write_text("print('ok')\n")
            manifest={'schemaVersion':1,'apiVersion':1,'id':'org.example.race','version':'1.0.0','entrypoint':'demo.py','permissions':['qso.read']}
            path=source/'manifest.json'; path.write_text(json.dumps(manifest))
            started=Event(); release=Event(); outcome=[]
            def preflight(*args,**kwargs):
                started.set()
                if not release.wait(5): raise ValueError('test timeout')
                return {'scope':'recent_sample'}
            def install():
                try: outcome.append(manager.install(root,path,token='test',port=1))
                except Exception as exc: outcome.append(exc)
            with patch.object(manager, '_verify_credential'), patch.object(manager.harness,'run_extension',side_effect=preflight):
                thread=Thread(target=install); thread.start()
                if not started.wait(5): release.set(); thread.join(); self.fail('preflight did not start')
                try:
                    try: manager.remove(root,manifest['id']); removal_error=None
                    except Exception as exc: removal_error=exc
                    self.assertEqual(manager.install(base/'independent',path)['id'],manifest['id'])
                finally:
                    release.set(); thread.join(5)
            self.assertFalse(thread.is_alive())
            self.assertIsInstance(removal_error,ValueError)
            self.assertEqual(len(outcome),1); self.assertIsInstance(outcome[0],dict)
            self.assertEqual(manager.list_installed(root)[0]['id'],manifest['id'])
            with closing(sqlite3.connect(log)) as db, db: self.assertEqual(db.execute('SELECT call FROM qso').fetchall(),[('K1ABC',)])
    def test_installed_manifest_changes_are_detected(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); src=base/'src'; src.mkdir(); root=base/'installed'
            (src/'demo.py').write_text("print('ok')\n")
            manifest={'schemaVersion':1,'apiVersion':1,'id':'org.example.demo','version':'1.0.0','entrypoint':'demo.py','permissions':['qso.read']}
            path=src/'manifest.json'; path.write_text(json.dumps(manifest)); manager.install(root,path)
            installed=root/manifest['id']/manifest['version']/'manifest.json'
            altered=json.loads(installed.read_text()); altered['permissions'].append('status.read'); installed.write_text(json.dumps(altered))
            self.assertEqual(manager.list_installed(root),[{'id':manifest['id'],'status':'invalid'}])
            with self.assertRaises(ValueError): manager.run(root,manifest['id'],'read-token',12345,1)
            manager.remove(root,manifest['id']); self.assertEqual(manager.list_installed(root),[])

    def test_legacy_pointer_remains_readable_and_reinstall_adds_manifest_hash(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); src=base/'src'; src.mkdir(); root=base/'installed'
            (src/'demo.py').write_text("print('ok')\n")
            manifest={'schemaVersion':1,'apiVersion':1,'id':'org.example.demo','version':'1.0.0','entrypoint':'demo.py','permissions':['qso.read']}
            path=src/'manifest.json'; path.write_text(json.dumps(manifest)); manager.install(root,path)
            pointer=root/manifest['id']/'current.json'; old=json.loads(pointer.read_text()); del old['manifestSha256']; pointer.write_text(json.dumps(old))
            self.assertEqual(manager.list_installed(root)[0]['id'],manifest['id'])
            manager.install(root,path); self.assertIn('manifestSha256',json.loads(pointer.read_text()))

    def test_corrupt_extension_does_not_hide_other_installed_extensions(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); source=base/'source'; source.mkdir(); root=base/'extensions'
            (source/'demo.py').write_text("print('ok')\n")
            m={'schemaVersion':1,'apiVersion':1,'id':'org.example.first','version':'1.0.0','entrypoint':'demo.py','permissions':['qso.read']}
            file=source/'manifest.json'; file.write_text(json.dumps(m)); manager.install(root,file)
            m['id']='org.example.second'; file.write_text(json.dumps(m)); manager.install(root,file)
            (root/'org.example.first'/'current.json').write_text('{broken')
            rows=manager.list_installed(root)
            self.assertEqual(len(rows),2)
            self.assertEqual(rows[0]['id'],'org.example.first')
            self.assertEqual(rows[0]['status'],'invalid')
            self.assertEqual(rows[1]['id'],'org.example.second')
            self.assertEqual(rows[1]['version'],'1.0.0')
            manager.remove(root,'org.example.first')
            self.assertEqual(manager.list_installed(root)[0]['id'],'org.example.second')
    def test_install_update_execute_remove_preserves_log(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp); extensions = root/'extensions'; source = root/'source'; source.mkdir()
            log = root/'log.sqlite'; db = sqlite3.connect(log); db.execute('create table qso(call text)'); db.execute("insert into qso values ('K1ABC')"); db.commit(); db.close()
            original = ROOT/'examples/extensions/recent_activity.py'; entry = source/'recent_activity.py'; entry.write_bytes(original.read_bytes())
            manifest = {'schemaVersion':1,'apiVersion':1,'id':'org.example.activity','version':'1.0.0','entrypoint':entry.name,'permissions':['qso.read']}
            file = source/'manifest.json'; file.write_text(json.dumps(manifest)); manager.install(extensions,file)
            self.assertEqual(manager.list_installed(extensions)[0]['version'],'1.0.0')
            manager.install(extensions,file)  # identical version is idempotent
            server = ThreadingHTTPServer(('127.0.0.1',0),Api); thread=Thread(target=server.serve_forever,daemon=True); thread.start()
            try:
                report=manager.run(extensions,manifest['id'],'read-token',server.server_port,5)
                self.assertEqual(report['byBand'],{'20m':1,'40m':1})
            finally:
                server.shutdown(); server.server_close(); thread.join()
            entry.write_bytes(original.read_bytes()+b'\n# version 2\n'); manifest['version']='1.1.0'; file.write_text(json.dumps(manifest)); manager.install(extensions,file)
            self.assertEqual(manager.list_installed(extensions)[0]['version'],'1.1.0')
            entry.write_text('different'); self.assertRaises(ValueError,manager.install,extensions,file)
            self.assertEqual(manager.list_installed(extensions)[0]['version'],'1.1.0')
            self.assertRaises((ValueError,OSError),manager.install,extensions,source/'missing.json')
            manager.remove(extensions,manifest['id']); self.assertEqual(manager.list_installed(extensions),[])
            db=sqlite3.connect(log); self.assertEqual(db.execute('select call from qso').fetchall(),[('K1ABC',)]); db.close()
            self.assertRaises(ValueError,manager.run,extensions,manifest['id'],'read-token',12345,1)

    def test_incompatible_or_invalid_update_keeps_active_version(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); src=base/'src'; src.mkdir(); root=base/'installed'; entry=src/'demo.py'; entry.write_text("print('ok')\n")
            manifest={'schemaVersion':1,'apiVersion':1,'id':'org.example.demo','version':'1.0.0','entrypoint':'demo.py','permissions':['qso.read']}
            path=src/'manifest.json'; path.write_text(json.dumps(manifest)); manager.install(root,path)
            manifest.update(version='2.0.0',permissions=['status.read']); path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError): manager.install(root,path)
            self.assertEqual(manager.list_installed(root)[0]['version'],'1.0.0')
            manifest['permissions']=['qso.read']; path.write_text(json.dumps(manifest)); entry.write_text('def broken(:\n')
            with self.assertRaises(ValueError): manager.install(root,path)
            self.assertEqual(manager.list_installed(root)[0]['version'],'1.0.0')
            entry.write_text("print('valid')\n")
            with patch.object(manager, '_atomic_file', side_effect=OSError('injected pointer failure')):
                with self.assertRaises(OSError): manager.install(root,path)
            self.assertEqual(manager.list_installed(root)[0]['version'],'1.0.0')
            manager.install(root,path); self.assertEqual(manager.list_installed(root)[0]['version'],'2.0.0')

    def test_preflight_rejects_crashing_update_before_activation(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); src=base/'src'; src.mkdir(); root=base/'installed'; entry=src/'recent_activity.py'
            original=(ROOT/'examples/extensions/recent_activity.py').read_bytes(); entry.write_bytes(original)
            manifest={'schemaVersion':1,'apiVersion':1,'id':'org.example.activity','version':'1.0.0','entrypoint':entry.name,'permissions':['qso.read']}
            path=src/'manifest.json'; path.write_text(json.dumps(manifest)); manager.install(root,path)
            legacy_root=base/'unverified'; manager.install(legacy_root,path)
            server=ThreadingHTTPServer(('127.0.0.1',0),Api); thread=Thread(target=server.serve_forever,daemon=True); thread.start()
            try:
                manifest['version']='2.0.0'; path.write_text(json.dumps(manifest)); entry.write_text("raise RuntimeError('broken')\n")
                new_root=base/'new-failed'
                with self.assertRaises(ValueError): manager.install(new_root,path,token='read-token',port=server.server_port,timeout=3)
                self.assertEqual(manager.list_installed(new_root),[])
                manager.install(legacy_root,path)
                self.assertEqual(manager.list_installed(legacy_root)[0]['version'],'2.0.0')
                with self.assertRaises(ValueError): manager.run(legacy_root,manifest['id'],'read-token',server.server_port,3)
                with self.assertRaises(ValueError): manager.install(root,path,token='read-token',port=server.server_port,timeout=3)
                self.assertEqual(manager.list_installed(root)[0]['version'],'1.0.0')
                self.assertEqual(manager.run(root,manifest['id'],'read-token',server.server_port,3)['sampleSize'],2)
                entry.write_text("import pathlib; pathlib.Path(__file__).write_text('# modified'); print('{\"scope\":\"recent_sample\"}')\n")
                with self.assertRaises(ValueError): manager.install(root,path,token='read-token',port=server.server_port,timeout=3)
                self.assertEqual(manager.list_installed(root)[0]['version'],'1.0.0')
                entry.write_bytes(original+b'\n# updated\n')
                with self.assertRaises(ValueError): manager.install(root,path,token='wrong-token',port=server.server_port,timeout=3)
                self.assertEqual(manager.list_installed(root)[0]['version'],'1.0.0')
                manager.install(root,path,token='read-token',port=server.server_port,timeout=3)
                self.assertEqual(manager.list_installed(root)[0]['version'],'2.0.0')
                self.assertEqual(manager.run(root,manifest['id'],'read-token',server.server_port,3)['sampleSize'],2)
            finally:
                server.shutdown(); server.server_close(); thread.join()

    def test_dependencies_are_copied_and_changes_detected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=pathlib.Path(temp)/'installed'
            source=ROOT/'examples/extensions/tracker_status.manifest.json'
            manifest=manager.install(root,source)
            folder=root/manifest['id']/manifest['version']
            self.assertTrue((folder/'recent_activity.py').is_file())
            self.assertEqual(manager.list_installed(root)[0]['id'],manifest['id'])
            (folder/'recent_activity.py').write_text('# altered')
            self.assertEqual(manager.list_installed(root),[{'id':manifest['id'],'status':'invalid'}])
            with self.assertRaises(ValueError): manager.run(root,manifest['id'],'read-token',12345,1)
            with self.assertRaises(ValueError): manager.install(root,source)
            manager.remove(root,manifest['id'])
            self.assertEqual(manager.list_installed(root),[])

    def test_rejects_unsafe_manifest_and_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            base=pathlib.Path(temp); src=base/'src'; src.mkdir(); (src/'demo.py').write_text('print(1)')
            m={'schemaVersion':1,'apiVersion':1,'id':'org.example.demo','version':'1.0.0','entrypoint':'demo.py','permissions':['tx.control']}; f=src/'manifest.json'; f.write_text(json.dumps(m))
            with self.assertRaises(ValueError): manager.install(base/'ext',f)
            self.assertFalse((base/'ext'/'org.example.demo').exists())
            m['permissions']=['qso.read']; f.write_text(json.dumps(m)); (src/'demo.py').unlink(); (src/'demo.py').symlink_to('/etc/hosts')
            with self.assertRaises((ValueError,OSError)): manager.install(base/'ext',f)
            (base/'unmanaged'/'org.example.demo').mkdir(parents=True)
            (base/'unmanaged'/'org.example.demo'/'keep.txt').write_text('keep')
            with self.assertRaises((ValueError,OSError)): manager.remove(base/'unmanaged','org.example.demo')
            self.assertEqual((base/'unmanaged'/'org.example.demo'/'keep.txt').read_text(),'keep')

if __name__ == '__main__': unittest.main()
