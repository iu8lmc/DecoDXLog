import importlib.util
import pathlib
import tempfile
import json
import unittest
from unittest.mock import patch

path = pathlib.Path(__file__).resolve().parents[2] / "tools/extension_harness.py"
spec = importlib.util.spec_from_file_location("extension_harness", path)
harness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(harness)


class ExtensionHarnessTests(unittest.TestCase):
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

    def test_manifest_version_and_unknown_permission_rejected(self):
        base = {"schemaVersion": 1, "apiVersion": 1, "id": "org.example.demo", "version": "1.0.0",
                "entrypoint": "demo.py", "permissions": ["qso.read"]}
        for field, value in (("schemaVersion", 2), ("apiVersion", 2),
                             ("permissions", ["tx.control"]), ("entrypoint", "../demo.py")):
            with self.subTest(field=field), self.assertRaises(ValueError):
                harness.validate_manifest({**base, field: value})


if __name__ == "__main__":
    unittest.main()
