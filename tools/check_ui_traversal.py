#!/usr/bin/env python3
"""Read-only gate: integration/session layout belongs to the traversal scheduler."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OWNER = Path("src/runtime/integration/dexvm_android/android_view.cpp")
RETIRED = re.compile(r"AndroidUiTraversalOwner::Legacy|RefreshAndroidLayoutParams|RefreshAndroidViewLayoutInputs|"
                     r"BuildUiElements|InflateUiElements|UiWidgetRegistry|ui_click_listeners|ui_touch_listeners|"
                     r"ui_notified_view_sizes|ui_notified_window_focus|focused_edit_text")


def guest_legacy_errors(code):
    return ["retired guest Legacy traversal/state entry"] if RETIRED.search(code) else []


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
    if "RequestFocus" in body:
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
    session = ROOT / "src/session/dex_activity_lifecycle.cpp"
    code = code_only(session.read_text(encoding="utf-8"))
    errors.extend(presentation_errors(code))
    if re.search(r"\.scroll_[xy]\s*(?:=(?!=)|[+*/-]=|\+\+|--)", code):
        errors.append("session writes guest scroll coordinates")
    for path in (ROOT / "src").rglob("*"):
        if path.suffix not in {".cpp", ".h", ".cc", ".hpp", ".mm"}:
            continue
        source = code_only(path.read_text(encoding="utf-8"))
        errors.extend(f"{error}: {path.relative_to(ROOT)}" for error in guest_legacy_errors(source))
        if "UiGeometryOwner::HostLayout" in source:
            allowed = Path("src/runtime/integration/dexvm_android/support_ui.cpp")
            if path.relative_to(ROOT) != allowed or source.count("UiGeometryOwner::HostLayout") != 1:
                errors.append(f"guest Legacy geometry owner: {path.relative_to(ROOT)}")
    if '--self-test' in sys.argv:
        assert not presentation_errors(code)
        start, end = function_body(code, "RestoreActivity")
        assert presentation_errors(code[:start + 1] + "RequestFocus();" + code[start + 1:])
        assert presentation_errors(code.replace('ComposeVideoViews(*snapshot,', 'ComposeVideoViews(context,'))
        for retired in ('AndroidUiTraversalOwner::Legacy', 'RefreshAndroidLayoutParams(vm, context)',
                        'context.ui_click_listeners', 'context.ui_notified_window_focus', 'BuildUiElements(vm, context)'):
            assert guest_legacy_errors(retired)
        print('UI presentation and guest Legacy static negative fixtures passed')
    if len(host_calls) != 2:
        errors.append(f"expected two isolated HostLayout calls, found {len(host_calls)}")
    if calls:
        errors.append(f"expected zero guest HostLayout calls, found {len(calls)}")
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"UI HostLayout: {len(host_calls)} isolated modal call")
    print(f"UI traversal: {len(calls)} guest HostLayout calls, 0 bypasses")


if __name__ == "__main__":
    main()
