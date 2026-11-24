import datetime as dt
import json
import shutil
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
import dsh_token_usage


def event(day: dt.date, message_id: str, total: int, provider: str = "deepseek-account",
          model: str = "deepseek-flash", usage: dict | None = None) -> str:
    moment = dt.datetime.combine(day, dt.time(12, 0, 0)).timestamp()
    payload = {"inputTokens": 10, "outputTokens": 20, "cacheReadTokens": 30,
               "cacheWriteTokens": 0, "totalTokens": total}
    if usage is not None:
        payload = usage
    return json.dumps({
        "type": "assistant/message",
        "time": int(moment * 1000),
        "data": {
            "message": {
                "id": message_id,
                "source": {"provider": provider, "model": model},
            },
            "usage": payload,
        },
    }) + "\n"


class DshTokenUsageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.home = Path(self.temp.name)
        self.session_dir = self.home / "sessions" / "workspace" / "session-one"
        self.session_dir.mkdir(parents=True)
        self.path = self.session_dir / "session.v4.jsonl"
        self.today = dt.date.today()

    def tearDown(self) -> None:
        self.temp.cleanup()

    def write(self, lines: list[str]) -> None:
        self.path.write_text("".join(lines), encoding="utf-8")

    def test_rolling_windows_filter_providers_and_deduplicate(self) -> None:
        other = self.home / "sessions" / "workspace" / "session-two"
        other.mkdir(parents=True)
        (other / "session.v4.jsonl").write_text(
            event(self.today, "same-id", 500) +
            event(self.today - dt.timedelta(days=1), "foreign", 999,
                  provider="openai", model="gpt-5"),
            encoding="utf-8")
        self.write([
            event(self.today, "same-id", 500),
            event(self.today - dt.timedelta(days=5), "five", 500),
            event(self.today - dt.timedelta(days=10), "ten", 500),
            event(self.today - dt.timedelta(days=40), "old", 500),
        ])
        usage = dsh_token_usage.DshTokenUsage(self.home, cache_seconds=0)
        state = usage.refresh(force=True)
        self.assertTrue(state["available"])
        self.assertEqual(state["tokens_today"], 500)
        self.assertEqual(state["tokens_7d"], 1000)
        self.assertEqual(state["tokens_30d"], 1500)
        self.assertEqual(state["requests_today"], 1)
        self.assertEqual(state["sessions"], 2)
        self.assertIsNone(state["error"])
        protocol = usage.protocol_snapshot()
        self.assertEqual(protocol["tokens_7d"], 1000)
        self.assertEqual(protocol["tokens_30d"], 1500)
        self.assertNotIn("error", protocol)

    def test_falls_back_to_individual_usage_fields(self) -> None:
        self.write([event(self.today, "fallback", 0,
                          usage={"inputTokens": 100, "outputTokens": 20,
                                 "cacheReadTokens": 5})])
        state = dsh_token_usage.DshTokenUsage(self.home, cache_seconds=0).refresh(force=True)
        self.assertEqual(state["tokens_today"], 125)
        self.assertEqual(state["tokens_7d"], 125)
        self.assertEqual(state["tokens_30d"], 125)
        self.assertEqual(state["requests_today"], 1)

    def test_missing_session_directory_is_unavailable(self) -> None:
        state = dsh_token_usage.DshTokenUsage(self.home / "missing").refresh(force=True)
        self.assertFalse(state["available"])
        self.assertEqual(state["error"], "no_sessions")
        self.assertEqual(state["tokens_7d"], 0)

    def test_zstd_session_is_supported_when_backend_is_available(self) -> None:
        zstd = shutil.which("zstd")
        if zstd is None:
            self.skipTest("zstd CLI unavailable")
        self.write([event(self.today, "compressed", 777)])
        compressed = self.session_dir / "session.v4.jsonl.zstd"
        subprocess.run([zstd, "-q", "-f", str(self.path), "-o", str(compressed)],
                       check=True)
        self.path.unlink()
        state = dsh_token_usage.DshTokenUsage(self.home, cache_seconds=0).refresh(force=True)
        self.assertTrue(state["available"])
        self.assertEqual(state["tokens_7d"], 777)

    def test_refresh_cache_can_be_forced(self) -> None:
        self.write([event(self.today, "first", 100)])
        usage = dsh_token_usage.DshTokenUsage(self.home, cache_seconds=3600)
        self.assertEqual(usage.refresh()["tokens_7d"], 100)
        with self.path.open("a", encoding="utf-8") as stream:
            stream.write(event(self.today, "second", 200))
        self.assertEqual(usage.refresh()["tokens_7d"], 100)
        state = usage.refresh(force=True)
        self.assertEqual(state["tokens_7d"], 300)
        self.assertEqual(state["tokens_today"], 300)
        self.assertEqual(state["requests_today"], 2)


if __name__ == "__main__":
    unittest.main()
