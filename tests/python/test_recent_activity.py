import importlib.util
import pathlib
import unittest
from unittest.mock import patch

path = pathlib.Path(__file__).resolve().parents[2] / "examples/extensions/recent_activity.py"
spec = importlib.util.spec_from_file_location("recent_activity", path)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class RecentActivityTests(unittest.TestCase):
    def setUp(self):
        self.caps = {"apiVersion": 1, "permissionModel": "read-only-token",
                     "operations": ["qso.read", "status.read"]}

    def test_independent_counts_and_unknowns(self):
        rows = [{"band": "20m", "mode": "MFSK", "submode": "FT8"},
                {"band": "40m", "mode": "CW"}, {"band": "20m", "mode": "CW"}, {}]
        with patch.object(report, "fetch_json", side_effect=[self.caps, {"qsos": rows}]) as fetch:
            result = report.build_report(12345, 10, "secret")
        self.assertEqual(result["sampleSize"], 4)
        self.assertEqual(result["byBand"], {"20m": 2, "40m": 1, "(unknown)": 1})
        self.assertEqual(result["byMode"], {"CW": 2, "FT8": 1, "(unknown)": 1})
        self.assertEqual(fetch.call_count, 2)
        self.assertEqual(fetch.call_args_list[1].args[1], "/api/v1/qsos?limit=10")

    def test_scoped_qso_credential(self):
        scoped = {**self.caps, "permissionModel": "extension-token", "perExtensionPermissions": True, "operations": ["qso.read"]}
        with patch.object(report, "fetch_json", side_effect=[scoped, {"qsos": []}]):
            self.assertEqual(report.build_report(12345, 10, "secret")["sampleSize"], 0)
        with patch.object(report, "fetch_json", return_value={**scoped, "operations": ["tracker.read"]}) as fetch:
            with self.assertRaises(ValueError): report.build_report(12345, 10, "secret")
            self.assertEqual(fetch.call_count, 1)

    def test_rejects_full_token_and_incompatible_capabilities_before_read(self):
        for caps in ({**self.caps, "permissionModel": "shared-token"},
                     {**self.caps, "apiVersion": 2},
                     {**self.caps, "permissionModel": "extension-token", "perExtensionPermissions": False},
                     {**self.caps, "operations": ["qso.read", {}]},
                     {**self.caps, "operations": ["qso.read", "qso.write"]}):
            with self.subTest(caps=caps), patch.object(report, "fetch_json", return_value=caps) as fetch:
                with self.assertRaises(ValueError):
                    report.build_report(12345, 10, "secret")
                self.assertEqual(fetch.call_count, 1)

    def test_rejects_invalid_parameters_and_malformed_response(self):
        for port, limit, token in [(0, 10, "x"), (2, 501, "x"), (2, 10, ""), (2, 10, "x\ny")]:
            with self.subTest(port=port, limit=limit), self.assertRaises(ValueError):
                report.build_report(port, limit, token)
        for rows in (None, ["not a record"], [{}, {}]):
            with self.subTest(rows=rows), patch.object(report, "fetch_json", side_effect=[self.caps, {"qsos": rows}]):
                with self.assertRaises(ValueError):
                    report.build_report(12345, 1, "secret")


if __name__ == "__main__":
    unittest.main()
