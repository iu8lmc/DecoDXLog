import importlib.util
import json
import pathlib
import sqlite3
import sys
import tempfile
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
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
