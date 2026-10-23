#!/usr/bin/env python3
"""Read-only gate: integration/session layout belongs to the traversal scheduler."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OWNER = Path("src/runtime/integration/dexvm_android/android_view.cpp")


def code_only(source):
    """Blank comments and literals while retaining offsets and line numbers."""
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|R"([^ ()\\\t\n]{0,16})\([\s\S]*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(tokens, lambda m: re.sub(r"[^\n]", " ", m.group()), source)


def function_body(code, name):
    match = re.search(r"\bbool\s+" + name + r"\s*\([^)]*\)\s*\{", code)
    if not match:
        return (-1, -1)
    start = match.end() - 1
    depth = 1
    for end in range(start + 1, len(code)):
        depth += (code[end] == "{") - (code[end] == "}")
        if depth == 0:
            return (start, end)
    return (-1, -1)


def main():
    errors = []
    calls = []
    for directory in ("src/runtime/integration", "src/session"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in {".cpp", ".h", ".cc", ".hpp", ".mm"}:
                continue
            relative = path.relative_to(ROOT)
            code = code_only(path.read_text(encoding="utf-8"))
            start, end = (
                function_body(code, "EnsureAndroidUiTraversal")
                if relative == OWNER
                else (-1, -1)
            )
            for match in re.finditer(r"\bLayoutUiTree\s*\(", code):
                location = f"{relative}:{code.count(chr(10), 0, match.start()) + 1}"
                calls.append(location)
                if not start < match.start() < end:
                    errors.append(f"layout bypass: {location}")
            if re.search(r"\bEnsureLayout\s*\(", code):
                errors.append(f"old layout entry: {relative}")
    if len(calls) != 1:
        errors.append(f"expected one Legacy implementation call, found {len(calls)}")
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"UI traversal: {len(calls)} scheduler call, 0 bypasses ({calls[0]})")


if __name__ == "__main__":
    main()
