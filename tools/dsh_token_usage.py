#!/usr/bin/env python3
"""Aggregate DeepSeek token usage from local DSH session logs.

DSH stores event-sourced sessions as zstd-compressed JSONL. Each completed
assistant message carries a usage record. This module reads only the usage
counters (never prompt or answer text) and answers the device's rolling
7-day and 30-day token questions without any API credential.
"""

from __future__ import annotations

import datetime as dt
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time
from typing import Any, Iterator

CACHE_SECONDS = 300.0
USAGE_KEYS = ("inputTokens", "outputTokens", "cacheReadTokens", "cacheWriteTokens")


class DshUsageError(Exception):
    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


def default_dsh_home() -> Path:
    value = os.environ.get("DSH_HOME")
    return Path(value) if value else Path.home() / ".dsh"


class ZstdUnavailable(DshUsageError):
    def __init__(self) -> None:
        super().__init__("zstd_unavailable")


def _iter_lines(path: Path) -> Iterator[str]:
    """Yield decoded JSONL lines from a plain or zstd-compressed DSH session."""
    if path.suffix == ".zstd":
        try:
            import zstandard  # type: ignore
        except ImportError:
            executable = shutil.which("zstd")
            if not executable:
                raise ZstdUnavailable()
            process = subprocess.Popen(
                [executable, "-dc", str(path)],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
            assert process.stdout is not None
            wrapper = io.TextIOWrapper(process.stdout, encoding="utf-8", errors="replace")
            try:
                for line in wrapper:
                    yield line
            finally:
                wrapper.close()
                process.wait()
            return
        with path.open("rb") as stream:
            reader = zstandard.ZstdDecompressor().stream_reader(stream)
            with reader:
                for line in io.TextIOWrapper(reader, encoding="utf-8", errors="replace"):
                    yield line
        return
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        yield from stream


def _message_usage(record: dict[str, Any]) -> tuple[str, int] | None:
    if record.get("type") != "assistant/message":
        return None
    data = record.get("data")
    if not isinstance(data, dict):
        return None
    message = data.get("message")
    usage = data.get("usage")
    if not isinstance(message, dict) or not isinstance(usage, dict):
        return None
    source = message.get("source")
    source = source if isinstance(source, dict) else {}
    provider = str(source.get("provider") or "").lower()
    model = str(source.get("model") or "").lower()
    if (provider or model) and "deepseek" not in provider and "deepseek" not in model:
        return None
    total = usage.get("totalTokens")
    if type(total) is not int or total < 0:
        total = 0
        for key in USAGE_KEYS:
            value = usage.get(key)
            if type(value) is int and value > 0:
                total += value
    if total <= 0:
        return None
    message_id = message.get("id")
    return (message_id if isinstance(message_id, str) else "", total)


class DshTokenUsage:
    """Thread-safe rolling token counts from local DSH sessions."""

    def __init__(self, home: str | Path | None = None, cache_seconds: float = CACHE_SECONDS) -> None:
        self.home = Path(home) if home else default_dsh_home()
        self.sessions = self.home / "sessions"
        self.cache_seconds = cache_seconds
        self._lock = threading.RLock()
        self._cached_at = 0.0
        self._state = self._empty_state()

    @staticmethod
    def _empty_state() -> dict[str, Any]:
        return {
            "available": False,
            "tokens_today": 0,
            "tokens_7d": 0,
            "tokens_30d": 0,
            "requests_today": 0,
            "updated": 0,
            "error": None,
            "sessions": 0,
        }

    def _session_files(self) -> list[Path]:
        if not self.sessions.is_dir():
            return []
        found: list[Path] = []
        for pattern in ("session*.jsonl.zstd", "session*.jsonl"):
            found.extend(self.sessions.rglob(pattern))
        # A file may match only one pattern; keep a deterministic, deduplicated order.
        return sorted(set(found))

    def refresh(self, force: bool = False) -> dict[str, Any]:
        with self._lock:
            if not force and self._cached_at and time.monotonic() - self._cached_at < self.cache_seconds:
                return dict(self._state)
            files = self._session_files()
            if not files:
                self._state = dict(self._empty_state(), updated=int(time.time()), error="no_sessions")
                self._cached_at = time.monotonic()
                return dict(self._state)

            today = dt.date.today()
            keys_7 = {today - dt.timedelta(days=offset) for offset in range(7)}
            keys_30 = {today - dt.timedelta(days=offset) for offset in range(30)}
            per_day: dict[dt.date, int] = {}
            seen_ids: set[str] = set()
            files_read = 0
            read_errors = 0
            tokens_today = 0
            requests_today = 0
            try:
                for path in files:
                    file_lines = 0
                    try:
                        for line in _iter_lines(path):
                            file_lines += 1
                            if '"assistant/message"' not in line:
                                continue
                            try:
                                record = json.loads(line)
                            except (ValueError, TypeError):
                                continue
                            parsed = _message_usage(record)
                            if parsed is None:
                                continue
                            message_id, total = parsed
                            if message_id:
                                if message_id in seen_ids:
                                    continue
                                seen_ids.add(message_id)
                            timestamp = record.get("time")
                            if isinstance(timestamp, bool) or not isinstance(timestamp, (int, float)) \
                                    or timestamp <= 0:
                                continue
                            day = dt.datetime.fromtimestamp(timestamp / 1000.0).date()
                            if day not in keys_30:
                                continue
                            per_day[day] = per_day.get(day, 0) + total
                            if day == today:
                                tokens_today += total
                                requests_today += 1
                    except ZstdUnavailable:
                        raise
                    except Exception:
                        # A session being appended may end in a partial zstd frame.
                        read_errors += 1
                    if file_lines > 0:
                        files_read += 1
            except ZstdUnavailable:
                self._state = dict(self._empty_state(), updated=int(time.time()),
                                   error="zstd_unavailable")
                self._cached_at = time.monotonic()
                return dict(self._state)
            if files_read == 0:
                self._state = dict(self._empty_state(), updated=int(time.time()), error="read_error")
                self._cached_at = time.monotonic()
                return dict(self._state)
            state = {
                "available": True,
                "tokens_today": tokens_today,
                "tokens_7d": sum(value for day, value in per_day.items() if day in keys_7),
                "tokens_30d": sum(per_day.values()),
                "requests_today": requests_today,
                "updated": int(time.time()),
                "error": None if read_errors == 0 else "partial",
                "sessions": files_read,
            }
            self._state = state
            self._cached_at = time.monotonic()
            return dict(state)

    def console_snapshot(self) -> dict[str, Any]:
        with self._lock:
            return dict(self._state)

    def protocol_snapshot(self) -> dict[str, Any]:
        with self._lock:
            state = self._state
            return {
                "available": bool(state["available"]),
                "tokens_today": int(state["tokens_today"]) if state["available"] else 0,
                "tokens_7d": int(state["tokens_7d"]) if state["available"] else 0,
                "tokens_30d": int(state["tokens_30d"]) if state["available"] else 0,
                "requests_today": int(state["requests_today"]) if state["available"] else 0,
                "updated": int(state["updated"]) if state["available"] else 0,
            }


def main() -> int:
    usage = DshTokenUsage()
    print(json.dumps(usage.refresh(force=True), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
