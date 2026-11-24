#!/usr/bin/env python3
"""Read the DeepSeek API account balance for the Passport companion.

Only the public balance endpoint is used. The API key never leaves this
computer: the bridge forwards the balance amount to the device, not the key.
"""

from __future__ import annotations

from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import json
import os
from pathlib import Path
import ssl
import threading
import time
import urllib.error
import urllib.request
from typing import Any

BALANCE_URL = "https://api.deepseek.com/user/balance"
REQUEST_TIMEOUT = 8.0
REFRESH_SECONDS = 300.0
MAX_RESPONSE_BYTES = 64 * 1024
MAX_CENTS = 10**15
USER_AGENT = "AI-Usage-Desk-Buddy/0.1.0"


class DeepSeekError(Exception):
    """Base class for sanitized DeepSeek failures."""

    code = "error"


class DeepSeekInvalidKey(DeepSeekError):
    code = "invalid_key"


class DeepSeekRateLimited(DeepSeekError):
    code = "rate_limited"


class DeepSeekNetworkError(DeepSeekError):
    code = "network"


class DeepSeekHTTPError(DeepSeekError):
    def __init__(self, status: int) -> None:
        super().__init__(f"HTTP {status}")
        self.code = f"http_{status}"


class DeepSeekResponseError(DeepSeekError):
    code = "bad_response"


def default_key_file() -> Path:
    """Return the console-owned key path, honoring CODEX_HOME."""
    home = os.environ.get("CODEX_HOME")
    base = Path(home) if home else Path.home() / ".codex"
    return base / "passport-console" / "deepseek.json"


def _ssl_context() -> ssl.SSLContext:
    try:
        import certifi
    except ImportError:
        pass
    else:
        try:
            return ssl.create_default_context(cafile=certifi.where())
        except OSError:
            pass
    for variable in ("SSL_CERT_FILE", "REQUESTS_CA_BUNDLE", "CURL_CA_BUNDLE"):
        path = os.environ.get(variable)
        if path and Path(path).is_file():
            try:
                return ssl.create_default_context(cafile=path)
            except OSError:
                pass
    # python.org macOS builds ship no CA store; the system bundle is still useful.
    if Path("/etc/ssl/cert.pem").is_file():
        try:
            return ssl.create_default_context(cafile="/etc/ssl/cert.pem")
        except OSError:
            pass
    return ssl.create_default_context()


def _cents(value: Any, field: str) -> int:
    if not isinstance(value, str) or not value.strip():
        raise DeepSeekResponseError(f"Missing {field}")
    try:
        amount = Decimal(value)
    except (InvalidOperation, ValueError):
        raise DeepSeekResponseError(f"Invalid {field}") from None
    if not amount.is_finite() or amount < 0:
        raise DeepSeekResponseError(f"Invalid {field}")
    cents = int((amount * 100).quantize(Decimal("1"), rounding=ROUND_HALF_UP))
    if cents > MAX_CENTS:
        raise DeepSeekResponseError(f"Oversized {field}")
    return cents


def normalize_balance(payload: Any) -> dict[str, Any]:
    """Validate the documented balance response and convert it to integer cents."""
    if not isinstance(payload, dict) or not isinstance(payload.get("is_available"), bool):
        raise DeepSeekResponseError("Missing is_available")
    infos = payload.get("balance_infos")
    if not isinstance(infos, list):
        raise DeepSeekResponseError("Missing balance_infos")
    chosen = None
    for info in infos:
        if not isinstance(info, dict):
            raise DeepSeekResponseError("Invalid balance entry")
        currency = info.get("currency")
        if not isinstance(currency, str) or not 1 <= len(currency) <= 7:
            raise DeepSeekResponseError("Invalid currency")
        if not currency.isascii() or not currency.isalpha():
            raise DeepSeekResponseError("Invalid currency")
        upper = currency.upper()
        if chosen is None or upper == "CNY":
            chosen = (upper, info)
            if upper == "CNY":
                break
    if chosen is None:
        raise DeepSeekResponseError("No balance entry")
    currency, info = chosen
    total = _cents(info.get("total_balance"), "total_balance")
    granted = _cents(info.get("granted_balance", "0"), "granted_balance")
    topped_up = _cents(info.get("topped_up_balance", "0"), "topped_up_balance")
    return {
        "currency": currency,
        "total": total,
        "granted": granted,
        "topped_up": topped_up,
        "updated": int(time.time()),
    }


def fetch_balance(api_key: str, timeout: float = REQUEST_TIMEOUT) -> dict[str, Any]:
    """Fetch and normalize the balance; raise DeepSeekError on any failure."""
    request = urllib.request.Request(
        BALANCE_URL,
        headers={
            "Authorization": f"Bearer {api_key}",
            "Accept": "application/json",
            "User-Agent": USER_AGENT,
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout, context=_ssl_context()) as response:
            body = response.read(MAX_RESPONSE_BYTES + 1)
    except urllib.error.HTTPError as error:
        if error.code in (401, 403):
            raise DeepSeekInvalidKey("Authentication failed") from None
        if error.code == 429:
            raise DeepSeekRateLimited("Rate limited") from None
        raise DeepSeekHTTPError(error.code) from None
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        raise DeepSeekNetworkError(type(error).__name__) from None
    except Exception as error:
        # http.client.HTTPException and other backend failures stay sanitized.
        raise DeepSeekNetworkError(type(error).__name__) from None
    if len(body) > MAX_RESPONSE_BYTES:
        raise DeepSeekResponseError("Response too large")
    try:
        payload = json.loads(body)
    except (ValueError, UnicodeDecodeError):
        raise DeepSeekResponseError("Invalid JSON") from None
    return normalize_balance(payload)


def validate_key(value: Any) -> str:
    if not isinstance(value, str):
        raise ValueError("API Key 格式无效")
    key = value.strip()
    if not 8 <= len(key) <= 512 or any(ord(character) < 0x21 or ord(character) > 0x7e for character in key):
        raise ValueError("API Key 格式无效")
    return key


def _atomic_write(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, separators=(",", ":"))
            stream.write("\n")
        temporary.replace(path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


class DeepSeekBalance:
    """Thread-safe balance snapshot shared by the console and bridge."""

    def __init__(self, key_file: str | Path | None = None) -> None:
        self.key_file = Path(key_file) if key_file else default_key_file()
        self.cache_file = self.key_file.with_name("deepseek-cache.json")
        self._lock = threading.RLock()
        self._refresh_lock = threading.Lock()
        self._generation = 0
        self._state = self._initial_state()

    def _read_key(self) -> str | None:
        try:
            value = json.loads(self.key_file.read_text(encoding="utf-8"))
        except (OSError, ValueError, UnicodeDecodeError):
            return None
        if not isinstance(value, dict):
            return None
        key = value.get("api_key")
        if not isinstance(key, str):
            return None
        try:
            return validate_key(key)
        except ValueError:
            return None

    def _read_cache(self) -> dict[str, Any] | None:
        try:
            value = json.loads(self.cache_file.read_text(encoding="utf-8"))
        except (OSError, ValueError, UnicodeDecodeError):
            return None
        if not isinstance(value, dict):
            return None
        try:
            currency = value["currency"]
            if not isinstance(currency, str) or not currency.isascii() or not currency.isalpha() \
                    or not 1 <= len(currency) <= 7:
                return None
            totals = [int(value["total"]), int(value["granted"]), int(value["topped_up"])]
            updated = int(value["updated"])
            if any(amount < 0 or amount > MAX_CENTS for amount in totals):
                return None
            if not 0 <= updated <= 2**53 - 1:
                return None
            return {
                "currency": currency.upper(),
                "total": totals[0],
                "granted": totals[1],
                "topped_up": totals[2],
                "updated": updated,
            }
        except (KeyError, TypeError, ValueError):
            return None

    def _initial_state(self) -> dict[str, Any]:
        key_present = self._read_key() is not None
        state = {
            "configured": key_present,
            "available": False,
            "stale": False,
            "currency": "",
            "total": 0,
            "granted": 0,
            "topped_up": 0,
            "updated": 0,
            "error": None,
        }
        if not key_present:
            return state
        cached = self._read_cache()
        if cached is not None:
            state.update(cached)
            state["available"] = True
            state["stale"] = True
        return state

    def _save_cache(self, state: dict[str, Any]) -> None:
        try:
            _atomic_write(self.cache_file, {
                "currency": state["currency"],
                "total": state["total"],
                "granted": state["granted"],
                "topped_up": state["topped_up"],
                "updated": state["updated"],
            })
        except OSError:
            pass

    def refresh(self) -> dict[str, Any]:
        """Fetch a new snapshot. Network failures preserve the last cached values."""
        if not self._refresh_lock.acquire(timeout=REQUEST_TIMEOUT + 2):
            return self.console_snapshot()
        try:
            with self._lock:
                generation = self._generation
                key = self._read_key()
                previous = dict(self._state)
            if key is None:
                with self._lock:
                    if generation == self._generation:
                        self._state = {
                            "configured": False,
                            "available": False,
                            "stale": False,
                            "currency": "",
                            "total": 0,
                            "granted": 0,
                            "topped_up": 0,
                            "updated": 0,
                            "error": None,
                        }
                    return self.console_snapshot()
            try:
                fresh = fetch_balance(key)
            except DeepSeekError as error:
                with self._lock:
                    if generation != self._generation:
                        return self.console_snapshot()
                    state = previous
                    state["configured"] = True
                    state["error"] = error.code
                    if state.get("updated"):
                        state["available"] = True
                        state["stale"] = True
                    else:
                        state["available"] = False
                        state["stale"] = False
                        state["currency"] = ""
                        state["total"] = 0
                        state["granted"] = 0
                        state["topped_up"] = 0
                        state["updated"] = 0
                    self._state = state
                return self.console_snapshot()
            state = dict(fresh, configured=True, available=True, stale=False, error=None)
            with self._lock:
                if generation != self._generation:
                    return self.console_snapshot()
                self._state = state
            self._save_cache(state)
            return self.console_snapshot()
        finally:
            self._refresh_lock.release()

    def save_key(self, value: Any) -> None:
        key = validate_key(value)
        with self._lock:
            changed = key != self._read_key()
            self._generation += 1
            _atomic_write(self.key_file, {"api_key": key, "saved_at": int(time.time())})
            if changed:
                try:
                    self.cache_file.unlink()
                except FileNotFoundError:
                    pass
                self._state = {
                    "configured": True,
                    "available": False,
                    "stale": False,
                    "currency": "",
                    "total": 0,
                    "granted": 0,
                    "topped_up": 0,
                    "updated": 0,
                    "error": None,
                }
            else:
                self._state["configured"] = True

    def clear_key(self) -> None:
        with self._lock:
            self._generation += 1
            for path in (self.key_file, self.cache_file):
                try:
                    path.unlink()
                except FileNotFoundError:
                    pass
            self._state = {
                "configured": False,
                "available": False,
                "stale": False,
                "currency": "",
                "total": 0,
                "granted": 0,
                "topped_up": 0,
                "updated": 0,
                "error": None,
            }

    def console_snapshot(self) -> dict[str, Any]:
        with self._lock:
            return dict(self._state)

    def protocol_snapshot(self) -> dict[str, Any]:
        """Return only the fields the device needs; the error code stays local."""
        with self._lock:
            state = self._state
            available = bool(state["available"])
            return {
                "configured": bool(state["configured"]),
                "available": available,
                "stale": bool(state["stale"]) if available else False,
                "currency": str(state["currency"]) if available else "",
                "total": int(state["total"]) if available else 0,
                "granted": int(state["granted"]) if available else 0,
                "topped_up": int(state["topped_up"]) if available else 0,
                "updated": int(state["updated"]) if available else 0,
            }


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description="Fetch the DeepSeek account balance")
    parser.add_argument("--key-file", type=Path, default=None)
    parser.add_argument("--key", default=None, help="Use a key directly instead of the key file")
    args = parser.parse_args()
    if args.key:
        try:
            result = fetch_balance(validate_key(args.key))
        except (DeepSeekError, ValueError) as error:
            print(json.dumps({"ok": False, "error": getattr(error, "code", type(error).__name__)}))
            return 1
        print(json.dumps({"ok": True, **result}, ensure_ascii=False))
        return 0
    balance = DeepSeekBalance(args.key_file)
    state = balance.refresh()
    print(json.dumps(state, ensure_ascii=False, indent=2))
    return 0 if state["available"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
