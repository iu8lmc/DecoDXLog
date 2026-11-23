import importlib.util
import pathlib
import sys
import unittest
from unittest.mock import patch

folder = pathlib.Path(__file__).resolve().parents[2] / "examples/extensions"
sys.path.insert(0, str(folder))
spec = importlib.util.spec_from_file_location("tracker_status", folder / "tracker_status.py")
tracker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tracker)


class TrackerAdapterTests(unittest.TestCase):
    def setUp(self):
        self.caps = {"apiVersion": 1, "permissionModel": "read-only-token", "operations": ["tracker.read"]}
        self.state = {"source": "predict_udp", "available": True, "fresh": True, "orbitalFresh": True,
                      "busy": False, "satellite": "FO-29", "azimuth": 10.0, "elevation": 20.0,
                      "doppler100MHz": 950.0, "receivedAt": "2026-01-01T00:00:00Z", "tleEpoch": "2025-12-31T00:00:00Z"}

    def test_fresh_and_stale_status_do_not_issue_commands(self):
        for state, expected in ((self.state, True), ({**self.state, "fresh": False}, False),
                                ({**self.state, "orbitalFresh": False}, False), ({**self.state, "busy": True}, False),
                                ({"source": "predict_udp", "available": False, "fresh": False,
                                  "orbitalFresh": False, "busy": False}, False)):
            with self.subTest(state=state), patch.object(tracker, "fetch_json", side_effect=[self.caps, state]) as fetch:
                result = tracker.read_tracker(12345, "read-only")
                self.assertEqual(result["freshForReview"], expected)
                self.assertEqual(result["control"], "none")
                self.assertEqual(fetch.call_args_list[1].args[1], "/api/v1/tracker")
                self.assertEqual(fetch.call_count, 2)

    def test_rejects_privileged_or_malformed_status(self):
        for caps in ({**self.caps, "permissionModel": "shared-token"}, {**self.caps, "apiVersion": 2},
                     {**self.caps, "operations": ["tracker.read", "qso.write"]}):
            with self.subTest(caps=caps), patch.object(tracker, "fetch_json", return_value=caps) as fetch:
                with self.assertRaises(ValueError): tracker.read_tracker(12345, "read-only")
                self.assertEqual(fetch.call_count, 1)
        for state in ({**self.state, "fresh": 1}, {**self.state, "azimuth": float("nan")},
                      {**self.state, "satellite": ""}):
            with self.subTest(state=state), patch.object(tracker, "fetch_json", side_effect=[self.caps, state]):
                with self.assertRaises(ValueError): tracker.read_tracker(12345, "read-only")


if __name__ == "__main__":
    unittest.main()
