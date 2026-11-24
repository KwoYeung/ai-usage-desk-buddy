import json
import sys
from pathlib import Path
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
import dsh_activity


class DshActivityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "passport-state.json"

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write(self, **overrides) -> None:
        value = {
            "available": True,
            "working": True,
            "completion_seq": 3,
            "updated": int(time.time() * 1000),
        }
        value.update(overrides)
        self.path.write_text(json.dumps(value), encoding="utf-8")

    def test_reads_fresh_working_state(self) -> None:
        self.write()
        reader = dsh_activity.DshActivity(self.path)
        state = reader.snapshot()
        self.assertTrue(state["available"])
        self.assertTrue(state["working"])
        self.assertEqual(state["completion_seq"], 3)
        self.assertIsNone(state["error"])
        protocol = reader.protocol_snapshot()
        self.assertEqual(protocol["completion_seq"], 3)
        self.assertNotIn("error", protocol)

    def test_disabled_state_is_unavailable(self) -> None:
        self.write(available=False, working=False)
        state = dsh_activity.DshActivity(self.path).snapshot()
        self.assertFalse(state["available"])
        self.assertFalse(state["working"])
        self.assertEqual(state["error"], "disabled")

    def test_stale_state_clears_working(self) -> None:
        self.write(updated=int((time.time() - 60) * 1000))
        state = dsh_activity.DshActivity(self.path, max_age=30).snapshot()
        self.assertFalse(state["available"])
        self.assertFalse(state["working"])
        self.assertEqual(state["error"], "stale")

    def test_missing_or_malformed_state_fails_closed(self) -> None:
        self.assertEqual(dsh_activity.DshActivity(self.path).snapshot()["error"], "missing")
        for body in ("not json", "[]", '{"available":true}',
                     '{"available":true,"working":true,"completion_seq":-1,"updated":1}'):
            self.path.write_text(body, encoding="utf-8")
            state = dsh_activity.DshActivity(self.path).snapshot()
            self.assertFalse(state["available"])
            self.assertFalse(state["working"])
            self.assertEqual(state["error"], "bad_state")


if __name__ == "__main__":
    unittest.main()
