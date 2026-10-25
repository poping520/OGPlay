#!/usr/bin/env python3
"""Read-only gate: integration/session layout belongs to the traversal scheduler."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OWNER = Path("src/runtime/integration/dexvm_android/android_view.cpp")


def code_only(source):
    """Blank comments and literals while retaining offsets and line numbers."""
    tokens = r'//[^\n]*|/\*[\s\S]*?\*/|R"([^ ()\\\t\n]{0,16})\([\s\S]*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(tokens, lambda m: re.sub(r"[^\n]", " ", m.group()), source)


def function_body(code, name):
    match = re.search(r"\b(?:bool|void|runtime::AndroidBoundaryFrame)\s+(?:\w+::)?" + name + r"\s*\([^)]*\)\s*\{", code)
    if not match:
        return (-1, -1)
    start = match.end() - 1
    depth = 1
    for end in range(start + 1, len(code)):
        depth += (code[end] == "{") - (code[end] == "}")
        if depth == 0:
            return (start, end)
    return (-1, -1)


def presentation_errors(code):
    errors = []
    start, end = function_body(code, "RestoreActivity")
    body = code[start:end]
    # A host focus restore is legal only under the explicit Legacy guard.
    guarded = re.sub(
        r"if\s*\(context.ui_traversal_owner\s*==\s*runtime::AndroidUiTraversalOwner::Legacy\s*&&"
        r"\s*record.focused_node\s*&&\s*context.ui_tree.Get\(\*record.focused_node\)\)"
        r"\s*static_cast<void>\(context.ui_tree.RequestFocus\(\*record.focused_node,\s*false\)\);",
        "", body)
    if "RequestFocus" in guarded:
        errors.append("ExternalProjection window focus restore bypass")
    start, end = function_body(code, "ComposeAndroidPresentationFrame")
    body = code[start:end]
    if start < 0 or len(re.findall(r"\bAcquireAndroidPresentation\s*\(", body)) != 1:
        errors.append("Compose requires exactly one presentation lease")
    if re.search(r"ComposeVideoViews\(context|context.ui_bitmaps|EnsureAndroid|PublishAndroid|ClearDrawDirty|RefreshAndroid", body):
        errors.append("Compose consumes mutable owner state")
    return errors


def main():
    errors = []
    calls = []
    host_calls = []
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
            host_start, host_end = (
                function_body(code, "ShowAndroidDialog")
                if relative == Path("src/runtime/integration/dexvm_android/support_ui.cpp")
                else (-1, -1)
            )
            resize_start, resize_end = (
                function_body(code, "ResizeAndroidDialogs")
                if relative == Path("src/runtime/integration/dexvm_android/support_ui.cpp")
                else (-1, -1)
            )
            for match in re.finditer(r"\bLayoutUiTree\s*\(", code):
                location = f"{relative}:{code.count(chr(10), 0, match.start()) + 1}"
                # ADR-0161: additional calls resolve only unpublished,
                # independently owned HostLayout modal trees.
                if ((host_start < match.start() < host_end or resize_start < match.start() < resize_end) and
                    re.match(r"LayoutUiTree\(\*modal\.host_tree, managed_viewport\)", code[match.start():])):
                    host_calls.append(location)
                    continue
                calls.append(location)
                if not start < match.start() < end:
                    errors.append(f"layout bypass: {location}")
            if re.search(r"\bEnsureLayout\s*\(", code):
                errors.append(f"old layout entry: {relative}")
    # DVM-300: session may mutate a host scroll position only in an explicit Legacy block.
    session = ROOT / "src/session/dex_activity_lifecycle.cpp"
    code = code_only(session.read_text(encoding="utf-8"))
    errors.extend(presentation_errors(code))
    if '--self-test' in sys.argv:
        assert not presentation_errors(code)
        assert presentation_errors(code.replace('record.focused_node &&', 'true &&'))
        assert presentation_errors(code.replace('ComposeVideoViews(*snapshot,', 'ComposeVideoViews(context,'))
        print('UI presentation static negative fixtures passed')
    legacy_blocks = []
    for match in re.finditer(r"else\s+if\s*\(context.ui_traversal_owner\s*==\s*runtime::AndroidUiTraversalOwner::Legacy\)\s*\{", code):
        start = match.end() - 1
        depth = 1
        for end in range(start + 1, len(code)):
            depth += (code[end] == "{") - (code[end] == "}")
            if not depth:
                legacy_blocks.append((start, end))
                break
    for write in re.finditer(r"\.scroll_[xy]\s*(?:=(?!=)|[+*/-]=|\+\+|--)", code):
        if not any(start < write.start() < end for start, end in legacy_blocks):
            errors.append("session scroll coordinate write outside explicit Legacy branch")
    if len(host_calls) != 2:
        errors.append(f"expected two isolated HostLayout calls, found {len(host_calls)}")
    if len(calls) != 1:
        errors.append(f"expected one Legacy implementation call, found {len(calls)}")
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"UI HostLayout: {len(host_calls)} isolated modal call")
    print(f"UI traversal: {len(calls)} scheduler call, 0 bypasses ({calls[0]})")


if __name__ == "__main__":
    main()
