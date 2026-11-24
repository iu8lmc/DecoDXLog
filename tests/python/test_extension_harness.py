import importlib.util
import pathlib
import tempfile
import json
import os
import subprocess
import sys
import time
import unittest
from unittest.mock import patch

path = pathlib.Path(__file__).resolve().parents[2] / "tools/extension_harness.py"
spec = importlib.util.spec_from_file_location("extension_harness", path)
harness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(harness)


class ExtensionHarnessTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "POSIX process groups required")
    def test_denied_group_cleanup_stops_leader_and_reports_failure(self):
        base = {"schemaVersion": 1, "apiVersion": 1, "id": "org.example.demo", "version": "1.0.0",
                "entrypoint": "demo.py", "permissions": ["qso.read"]}
        with tempfile.TemporaryDirectory() as directory:
            folder=pathlib.Path(directory); marker=folder/"late-marker"
            manifest=folder/"manifest.json"; manifest.write_text(json.dumps(base))
            (folder/"demo.py").write_text("import time,pathlib\ntime.sleep(1)\npathlib.Path(%r).write_text('alive')\n" % str(marker))
            with patch.object(harness.os,"killpg",side_effect=PermissionError("injected")):
                with self.assertRaisesRegex(ValueError,"process group"):
                    harness.run_extension(manifest,"read-only",12345,0.2)
            time.sleep(1.1); self.assertFalse(marker.exists(),"extension leader continued after cleanup")

    @unittest.skipUnless(os.name == "posix", "POSIX process groups required")
    def test_timeout_and_normal_exit_stop_descendants(self):
        base = {"schemaVersion": 1, "apiVersion": 1, "id": "org.example.demo", "version": "1.0.0",
                "entrypoint": "demo.py", "permissions": ["qso.read"]}
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            manifest = folder / "manifest.json"
            manifest.write_text(json.dumps(base))
            for case in ("timeout", "exit", "crash", "output"):
                marker = folder / (case + "-marker")
                ready = folder / (case + "-ready")
                child = "import pathlib,time; pathlib.Path(%r).write_text('ready'); time.sleep(0.8); pathlib.Path(%r).write_text('alive')" % (str(ready), str(marker))
                source = "import subprocess,sys,time,json,pathlib\nsubprocess.Popen([sys.executable,'-c',%r])\nwhile not pathlib.Path(%r).exists(): time.sleep(0.01)\n" % (child, str(ready))
                source += {"timeout": "time.sleep(10)\n", "exit": "print(json.dumps({'scope':'recent_sample'}))\n",
                           "crash": "raise RuntimeError('crash')\n", "output": "print('x' * (2 * 1024 * 1024), flush=True); time.sleep(10)\n"}[case]
                (folder / "demo.py").write_text(source)
                if case != "exit":
                    with self.assertRaises(ValueError):
                        harness.run_extension(manifest, "read-only", 12345, 0.3 if case == "timeout" else 3)
                else:
                    self.assertEqual(harness.run_extension(manifest, "read-only", 12345, 3)["scope"], "recent_sample")
                time.sleep(1)
                self.assertFalse(marker.exists(), "descendant continued after the harness returned")

    def test_example_manifest_and_denied_permissions(self):
        manifest = pathlib.Path(__file__).resolve().parents[2] / "examples/extensions/recent_activity.manifest.json"
        result = harness.load_manifest(manifest)
        self.assertEqual(result["id"], "org.decodxlog.example.recent-activity")
        self.assertEqual(result["permissions"], ["qso.read"])

    def test_subprocess_success_crash_timeout_and_output_limit(self):
        base = {"schemaVersion": 1, "apiVersion": 1, "id": "org.example.demo", "version": "1.0.0",
                "entrypoint": "demo.py", "permissions": ["qso.read"]}
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            manifest = folder / "manifest.json"
            manifest.write_text(json.dumps(base))
            entry = folder / "demo.py"
            entry.write_text("import os,json; print(json.dumps({'scope':'recent_sample','leaked': 'FULL_TOKEN' in os.environ}))")
            with patch.dict("os.environ", {"FULL_TOKEN": "do-not-pass"}):
                result = harness.run_extension(manifest, "read-only", 12345, 3)
            self.assertEqual(result["scope"], "recent_sample")
            self.assertFalse(result["leaked"])
            entry.write_text("import os,json; print(json.dumps({'scope':'recent_sample','token':os.environ['DECODXLOG_READ_TOKEN']}))")
            with self.assertRaises(ValueError):
                harness.run_extension(manifest, "read-only", 12345, 3)
            # JSON may spell the same secret with Unicode escapes; checking only
            # the raw output bytes would miss it and main() would print it decoded.
            entry.write_text("print(r'{\"scope\":\"recent_sample\",\"nested\":[{\"value\":\"\\u0072ead-only\"}]}')")
            with self.assertRaises(ValueError):
                harness.run_extension(manifest, "read-only", 12345, 3)
            env = {**os.environ, "DECODXLOG_READ_TOKEN": "read-only", "PYTHONDONTWRITEBYTECODE": "1"}
            cli = subprocess.run([sys.executable, str(path), str(manifest), "--port", "12345"], env=env, capture_output=True, timeout=5)
            self.assertNotEqual(cli.returncode, 0)
            self.assertNotIn(b"read-only", cli.stdout + cli.stderr)
            entry.write_text("print(r'{\"scope\":\"recent_sample\",\"\\u0072ead-only\":true}')")
            with self.assertRaises(ValueError):
                harness.run_extension(manifest, "read-only", 12345, 3)
            entry.write_text("raise RuntimeError('crash')")
            with self.assertRaises(ValueError):
                harness.run_extension(manifest, "read-only", 12345, 3)
            entry.write_text("import time; time.sleep(10)")
            with self.assertRaises(ValueError):
                harness.run_extension(manifest, "read-only", 12345, 0.2)
            entry.write_text("print('x' * (2 * 1024 * 1024))")
            with self.assertRaises(ValueError):
                harness.run_extension(manifest, "read-only", 12345, 3)
            manifest.write_text('{"schemaVersion":1,"schemaVersion":1}')
            with self.assertRaises(ValueError):
                harness.load_manifest(manifest)
            manifest.write_text(json.dumps(base))
            entry.unlink()
            with self.assertRaises(OSError):
                harness.load_manifest(manifest)

    def test_tracker_manifest_is_read_only(self):
        manifest = pathlib.Path(__file__).resolve().parents[2] / "examples/extensions/tracker_status.manifest.json"
        result = harness.load_manifest(manifest)
        self.assertEqual(result["permissions"], ["tracker.read"])
        self.assertEqual(result["dependencies"], ["recent_activity.py"])
        for deps in (["../unsafe.py"], ["tracker_status.py"], ["recent_activity.py", "recent_activity.py"]):
            with self.assertRaises(ValueError):
                harness.validate_manifest({**result, "dependencies": deps})

    def test_manifest_version_and_unknown_permission_rejected(self):
        base = {"schemaVersion": 1, "apiVersion": 1, "id": "org.example.demo", "version": "1.0.0",
                "entrypoint": "demo.py", "permissions": ["qso.read"]}
        for field, value in (("schemaVersion", 2), ("apiVersion", 2),
                             ("permissions", ["tx.control"]), ("entrypoint", "../demo.py")):
            with self.subTest(field=field), self.assertRaises(ValueError):
                harness.validate_manifest({**base, field: value})


if __name__ == "__main__":
    unittest.main()
