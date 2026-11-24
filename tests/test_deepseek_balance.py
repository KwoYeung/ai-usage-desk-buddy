import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
import deepseek_balance


class NormalizeBalanceTests(unittest.TestCase):
    def test_prefers_cny_and_converts_to_cents(self) -> None:
        result = deepseek_balance.normalize_balance({
            "is_available": True,
            "balance_infos": [
                {"currency": "USD", "total_balance": "1.00",
                 "granted_balance": "0.00", "topped_up_balance": "1.00"},
                {"currency": "CNY", "total_balance": "110.00",
                 "granted_balance": "10.00", "topped_up_balance": "100.00"},
            ],
        })
        self.assertEqual(result["currency"], "CNY")
        self.assertEqual(result["total"], 11000)
        self.assertEqual(result["granted"], 1000)
        self.assertEqual(result["topped_up"], 10000)
        self.assertGreater(result["updated"], 0)

    def test_accepts_a_usd_only_account_and_rounds_half_up(self) -> None:
        result = deepseek_balance.normalize_balance({
            "is_available": False,
            "balance_infos": [
                {"currency": "usd", "total_balance": "0.005",
                 "granted_balance": "0", "topped_up_balance": "0.005"},
            ],
        })
        self.assertEqual(result["currency"], "USD")
        self.assertEqual(result["total"], 1)
        self.assertEqual(result["granted"], 0)
        self.assertEqual(result["topped_up"], 1)

    def test_rejects_malformed_documented_fields(self) -> None:
        invalid = [
            {},
            {"is_available": "yes", "balance_infos": []},
            {"is_available": True},
            {"is_available": True, "balance_infos": {}},
            {"is_available": True, "balance_infos": []},
            {"is_available": True, "balance_infos": [{"currency": "CNY"}]},
            {"is_available": True, "balance_infos": [{"currency": "CNY", "total_balance": "-1"}]},
            {"is_available": True, "balance_infos": [{"currency": "CNY", "total_balance": "abc"}]},
            {"is_available": True, "balance_infos": [{"currency": "人民币", "total_balance": "1"}]},
            {"is_available": True, "balance_infos": [{"currency": "CNY", "total_balance": 1}]},
            {"is_available": True, "balance_infos": [{"currency": "CNY", "total_balance": "1e20"}]},
        ]
        for payload in invalid:
            with self.assertRaises(deepseek_balance.DeepSeekResponseError, msg=repr(payload)):
                deepseek_balance.normalize_balance(payload)


class FetchBalanceTests(unittest.TestCase):
    class Response:
        def __init__(self, body: bytes) -> None:
            self.body = body

        def __enter__(self):
            return self

        def __exit__(self, *args):
            return False

        def read(self, limit: int = -1) -> bytes:
            return self.body[:limit] if limit >= 0 else self.body

    def test_maps_http_failures_to_sanitized_codes(self) -> None:
        cases = [
            (401, deepseek_balance.DeepSeekInvalidKey, "invalid_key"),
            (403, deepseek_balance.DeepSeekInvalidKey, "invalid_key"),
            (429, deepseek_balance.DeepSeekRateLimited, "rate_limited"),
            (500, deepseek_balance.DeepSeekHTTPError, "http_500"),
        ]
        for status, error_type, code in cases:
            with patch("urllib.request.urlopen",
                       side_effect=urllib.error.HTTPError("https://api.deepseek.com", status,
                                                          "error", {}, None)):
                with self.assertRaises(error_type) as raised:
                    deepseek_balance.fetch_balance("sk-test")
            self.assertEqual(raised.exception.code, code)

    def test_maps_network_and_malformed_responses(self) -> None:
        with patch("urllib.request.urlopen",
                   side_effect=urllib.error.URLError("offline")):
            with self.assertRaises(deepseek_balance.DeepSeekNetworkError):
                deepseek_balance.fetch_balance("sk-test")
        with patch("urllib.request.urlopen", return_value=self.Response(b"not json")):
            with self.assertRaises(deepseek_balance.DeepSeekResponseError):
                deepseek_balance.fetch_balance("sk-test")
        with patch("urllib.request.urlopen",
                   return_value=self.Response(b"x" * (deepseek_balance.MAX_RESPONSE_BYTES + 1))):
            with self.assertRaises(deepseek_balance.DeepSeekResponseError):
                deepseek_balance.fetch_balance("sk-test")

    def test_returns_normalized_balance(self) -> None:
        body = json.dumps({
            "is_available": True,
            "balance_infos": [{"currency": "CNY", "total_balance": "12.34",
                               "granted_balance": "0.34", "topped_up_balance": "12.00"}],
        }).encode()
        with patch("urllib.request.urlopen", return_value=self.Response(body)):
            result = deepseek_balance.fetch_balance("sk-test")
        self.assertEqual(result["currency"], "CNY")
        self.assertEqual(result["total"], 1234)
        self.assertEqual(result["granted"], 34)
        self.assertEqual(result["topped_up"], 1200)


class BalanceStateTests(unittest.TestCase):
    KEY = "sk-" + "a" * 40

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.key_file = self.directory / "deepseek.json"
        self.balance = deepseek_balance.DeepSeekBalance(self.key_file)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_missing_key_is_unconfigured_and_does_not_fetch(self) -> None:
        self.assertFalse(self.balance.console_snapshot()["configured"])
        with patch.object(deepseek_balance, "fetch_balance") as fetch:
            state = self.balance.refresh()
        fetch.assert_not_called()
        self.assertFalse(state["configured"])
        self.assertFalse(state["available"])

    def test_save_key_uses_private_permissions_and_stores_no_key_in_snapshots(self) -> None:
        self.balance.save_key(self.KEY)
        if sys.platform != "win32":
            self.assertEqual(stat.S_IMODE(self.key_file.stat().st_mode), 0o600)
        self.assertEqual(json.loads(self.key_file.read_text())["api_key"], self.KEY)
        console = self.balance.console_snapshot()
        protocol = self.balance.protocol_snapshot()
        self.assertTrue(console["configured"])
        self.assertNotIn(self.KEY, json.dumps(console))
        self.assertNotIn(self.KEY, json.dumps(protocol))
        self.assertNotIn("error", protocol)

    def test_successful_refresh_caches_and_serves_both_snapshots(self) -> None:
        self.balance.save_key(self.KEY)
        fresh = {"currency": "CNY", "total": 1234, "granted": 200,
                 "topped_up": 1034, "updated": 1700000000}
        with patch.object(deepseek_balance, "fetch_balance", return_value=fresh) as fetch:
            state = self.balance.refresh()
        fetch.assert_called_once()
        self.assertTrue(state["available"])
        self.assertFalse(state["stale"])
        self.assertEqual(state["total"], 1234)
        protocol = self.balance.protocol_snapshot()
        self.assertEqual(protocol["total"], 1234)
        self.assertTrue(protocol["configured"])
        self.assertFalse(protocol["stale"])
        self.assertEqual(json.loads(self.balance.cache_file.read_text())["total"], 1234)

    def test_failed_refresh_keeps_last_balance_as_stale(self) -> None:
        self.balance.save_key(self.KEY)
        fresh = {"currency": "CNY", "total": 1234, "granted": 0,
                 "topped_up": 1234, "updated": 1700000000}
        with patch.object(deepseek_balance, "fetch_balance", return_value=fresh):
            self.balance.refresh()
        with patch.object(deepseek_balance, "fetch_balance",
                          side_effect=deepseek_balance.DeepSeekNetworkError("offline")):
            state = self.balance.refresh()
        self.assertTrue(state["available"])
        self.assertTrue(state["stale"])
        self.assertEqual(state["error"], "network")
        self.assertEqual(state["total"], 1234)
        self.assertTrue(self.balance.protocol_snapshot()["stale"])

    def test_changing_or_clearing_the_key_discards_old_cache(self) -> None:
        self.balance.save_key(self.KEY)
        fresh = {"currency": "CNY", "total": 1234, "granted": 0,
                 "topped_up": 1234, "updated": 1700000000}
        with patch.object(deepseek_balance, "fetch_balance", return_value=fresh):
            self.balance.refresh()
        self.assertTrue(self.balance.cache_file.exists())

        self.balance.save_key("sk-" + "b" * 40)
        state = self.balance.console_snapshot()
        self.assertFalse(state["available"])
        self.assertEqual(state["total"], 0)
        self.assertFalse(self.balance.cache_file.exists())

        self.balance.clear_key()
        self.assertFalse(self.key_file.exists())
        self.assertFalse(self.balance.console_snapshot()["configured"])

    def test_corrupt_cache_never_reaches_the_device(self) -> None:
        self.balance.save_key(self.KEY)
        self.balance.cache_file.write_text(
            '{"currency":"CNY","total":-1,"granted":0,"topped_up":0,"updated":1}',
            encoding="utf-8")
        reopened = deepseek_balance.DeepSeekBalance(self.key_file)
        state = reopened.console_snapshot()
        self.assertFalse(state["available"])
        self.assertEqual(state["total"], 0)
        protocol = reopened.protocol_snapshot()
        self.assertFalse(protocol["available"])
        self.assertEqual(protocol["total"], 0)

    def test_rejects_invalid_key_shapes(self) -> None:
        for value in (None, "", "short", "sk-" + "a" * 600, "sk-abc def"):
            with self.assertRaises(ValueError, msg=repr(value)):
                self.balance.save_key(value)


if __name__ == "__main__":
    unittest.main()
