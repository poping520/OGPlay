#!/usr/bin/env python3
"""Align GuestStallSnapshot host_tid values with WinDbg/lldb stack blocks."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
from typing import Any


HEADERS = (
    (re.compile(r"\bhost_tid=(0x[0-9a-fA-F]+|[0-9]+)\b"), 0),
    (re.compile(r"\bId:\s+[0-9a-fA-F]+\.([0-9a-fA-F]+)\b"), 16),
    (re.compile(r"\btid\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\b"), 0),
)


def parse_tid(line: str) -> int | None:
    for pattern, forced_base in HEADERS:
        match = pattern.search(line)
        if match:
            text = match.group(1)
            return int(text, forced_base or (16 if text.startswith("0x") else 10))
    return None


def stack_blocks(text: str) -> dict[int, list[str]]:
    blocks: dict[int, list[str]] = {}
    current: int | None = None
    for line in text.splitlines():
        tid = parse_tid(line)
        if tid is not None:
            current = tid
            blocks.setdefault(tid, []).append(line)
        elif current is not None:
            blocks[current].append(line)
    return blocks


def align(snapshot: dict[str, Any], stacks: str) -> list[dict[str, Any]]:
    blocks = stack_blocks(stacks)
    result: list[dict[str, Any]] = []
    for execution in snapshot.get("executions", []):
        host_tid = int(execution["host_tid"])
        result.append(
            {
                "host_tid": host_tid,
                "guest_tid": execution.get("guest_tid"),
                "context_token": execution.get("context_token"),
                "path": execution.get("path"),
                "name": execution.get("name"),
                "stack_status": "matched" if host_tid in blocks else "unavailable",
                "stack_lines": blocks.get(host_tid, []),
            }
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    parser.add_argument("--stacks", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    snapshot = json.loads(arguments.snapshot.read_text(encoding="utf-8"))
    stacks = arguments.stacks.read_text(encoding="utf-8", errors="replace")
    print(json.dumps({"threads": align(snapshot, stacks)}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
