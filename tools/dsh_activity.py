#!/usr/bin/env python3
"""Read the state published by the Passport DSH activity plugin.

The plugin writes only working/turn-completion counters. This reader never sees
prompt or answer text and never talks to the DSH API or its credentials.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import threading
import time
from typing import Any

MAX_AGE_SECONDS = 30.0
DEFAULT_STATE_FILE = Path(os.environ.get("DSH_HOME") or (Path.home() / ".dsh")) / "passport-state.json"


class DshActivity:
    """Thread-safe, freshness-checked DSH activity snapshot."""

    def __init__(self, state_file: str | Path | None = None,
                 max_age: float = MAX_AGE_SECONDS) -> None:
        self.state_file = Path(state_file) if state_file else DEFAULT_STATE_FILE
        self.max_age = max_age
        self._lock = threading.RLock()
        self._mtime_ns: int | None = None
        self._size: int | None = None
        self._raw: dict[str, Any] | None = None
        self._error: str | None = None

    def _read(self) -> dict[str, Any] | None:
        try:
            stat = self.state_file.stat()
        except OSError:
            self._mtime_ns = None
            self._size = None
            self._raw = None
            self._error = "missing"
            return None
        if self._raw is not None and self._mtime_ns == stat.st_mtime_ns and self._size == stat.st_size:
            return self._raw
        try:
            value = json.loads(self.state_file.read_text(encoding="utf-8"))
        except (OSError, ValueError, UnicodeDecodeError):
            self._error = "bad_state"
            self._raw = None
            return None
        if not isinstance(value, dict):
            self._error = "bad_state"
            self._raw = None
            return None
        available = value.get("available")
        working = value.get("working")
        completion = value.get("completion_seq")
        updated = value.get("updated")
        if not isinstance(available, bool) or not isinstance(working, bool):
            self._error = "bad_state"
            self._raw = None
            return None
        if isinstance(completion, bool) or not isinstance(completion, int) or completion < 0:
            self._error = "bad_state"
            self._raw = None
            return None
        if isinstance(updated, bool) or not isinstance(updated, (int, float)) or updated <= 0:
            self._error = "bad_state"
            self._raw = None
            return None
        self._mtime_ns = stat.st_mtime_ns
        self._size = stat.st_size
        self._raw = {
            "available": available,
            "working": working,
            "completion_seq": completion,
            "updated": int(updated / 1000.0),
        }
        self._error = None
        return self._raw

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            raw = self._read()
            if raw is None:
                return {
                    "available": False,
                    "working": False,
                    "completion_seq": 0,
                    "updated": 0,
                    "error": self._error,
                }
            age = time.time() - raw["updated"]
            available = bool(raw["available"]) and age <= self.max_age
            return {
                "available": available,
                "working": bool(raw["working"]) if available else False,
                "completion_seq": int(raw["completion_seq"]) if available else 0,
                "updated": int(raw["updated"]) if available else 0,
                "error": None if available else ("stale" if raw["available"] else "disabled"),
            }

    def console_snapshot(self) -> dict[str, Any]:
        return self.snapshot()

    def protocol_snapshot(self) -> dict[str, Any]:
        state = self.snapshot()
        return {
            "available": bool(state["available"]),
            "working": bool(state["working"]),
            "completion_seq": int(state["completion_seq"]),
            "updated": int(state["updated"]),
        }
