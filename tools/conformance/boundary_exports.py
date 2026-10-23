#!/usr/bin/env python3
"""Compare production dump-surface native exports with pinned NDK API19 stubs.

Checks complete HLE libraries only. libc/libdl entries in dump-surface are
intercepts of guest libraries, not complete HLE providers. A matching export
set proves link surface coverage, not implementation or extension support.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASELINE = Path(__file__).with_name("boundary_exports_baseline.json")
NDK = ROOT / ".local/toolchain/android-ndk-r25c/toolchains/llvm/prebuilt/darwin-x86_64"
LIBRARIES = (
    "libEGL.so", "libGLESv1_CM.so", "libGLESv2.so", "libOpenSLES.so",
    "libandroid.so", "libjnigraphics.so", "liblog.so",
)
INTERCEPTS = {"libc.so", "libdl.so"}


def production_exports(surface):
    if not isinstance(surface, dict) or surface.get("schema") != 1 or surface.get("api") != 19:
        raise ValueError("expected dump-surface schema=1, api=19")
    rows = surface.get("native_exports")
    if not isinstance(rows, list) or not rows:
        raise ValueError("missing/non-array native_exports")
    result = {library: set() for library in LIBRARIES}
    seen = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("invalid native export row")
        library, symbol = row.get("library"), row.get("symbol")
        if not isinstance(library, str) or not isinstance(symbol, str) or not symbol:
            raise ValueError("invalid library/symbol")
        if library not in result and library not in INTERCEPTS:
            raise ValueError("unclassified provider: " + library)
        if (library, symbol) in seen:
            raise ValueError("duplicate native export: " + library + "!" + symbol)
        seen.add((library, symbol))
        if library in result:
            result[library].add(symbol)
    # A completely removed provider remains in result with all exports missing.
    return result


def nm_exports(output):
    result = set()
    for line in output.splitlines():
        fields = line.split()
        if not fields:
            continue
        if len(fields) < 3 or len(fields[1]) != 1 or fields[1] in {"U", "u"}:
            raise ValueError("invalid defined-only POSIX nm row: " + line)
        symbol = fields[0]
        if symbol in result:
            raise ValueError("duplicate stub symbol: " + symbol)
        result.add(symbol)
    if not result:
        raise ValueError("empty stub symbol table")
    return result


def path_identity(path):
    path = path.resolve()
    return path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)


def reference_exports(stubs, nm):
    references, exports = {}, {}
    for library in LIBRARIES:
        path = stubs / library
        raw = path.read_bytes()
        output = subprocess.run(
            [str(nm), "-D", "--defined-only", "--format=posix", str(path)],
            check=True, capture_output=True, text=True, encoding="utf-8",
        ).stdout
        exports[library] = nm_exports(output)
        references[library] = {
            "path": path_identity(path),
            "sha256": hashlib.sha256(raw).hexdigest(),
            "symbols": sorted(exports[library]),
        }
    return references, exports


def analyze(actual, reference):
    if set(actual) != set(LIBRARIES) or set(reference) != set(LIBRARIES):
        raise ValueError("provider inventory mismatch")
    return {library: {
        "MISSING": sorted(reference[library] - actual[library]),
        "EXTRA": sorted(actual[library] - reference[library]),
    } for library in LIBRARIES}


def compare_baseline(baseline, references, result):
    if baseline.get("schema") != 1 or baseline.get("api") != 19:
        raise ValueError("baseline schema/API mismatch")
    if baseline.get("references") != references:
        raise ValueError("baseline stub paths/hashes/symbols mismatch")
    previous = baseline.get("libraries")
    if not isinstance(previous, dict) or set(previous) != set(result):
        raise ValueError("baseline provider inventory mismatch")
    changes = {"new": [], "removed": []}
    for library, categories in result.items():
        if set(previous[library]) != {"MISSING", "EXTRA"}:
            raise ValueError("baseline category mismatch")
        for category, names in categories.items():
            old = previous[library][category]
            if not isinstance(old, list) or any(not isinstance(x, str) for x in old) or len(old) != len(set(old)):
                raise ValueError("invalid baseline symbol list")
            before, after = set(old), set(names)
            for symbol in sorted(after - before):
                changes["new"].append([library, category, symbol])
            for symbol in sorted(before - after):
                changes["removed"].append([library, category, symbol])
    return changes


def save_baseline(path, references, result):
    if path.exists():
        old = json.loads(path.read_text(encoding="utf-8"))
        if compare_baseline(old, references, result)["new"]:
            return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"schema": 1, "api": 19,
                               "references": references, "libraries": result},
                              indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return True


def self_test():
    surface = {"schema": 1, "api": 19, "native_exports": [
        {"library": library, "symbol": "present"} for library in LIBRARIES
    ] + [{"library": "libc.so", "symbol": "memcpy"}]}
    actual = production_exports(surface)
    reference = {library: {"present", "missing"} for library in LIBRARIES}
    refs = {library: {"path": library, "sha256": "fixture", "symbols": sorted(names)}
            for library, names in reference.items()}
    result = analyze(actual, reference)
    assert all(row == {"MISSING": ["missing"], "EXTRA": []} for row in result.values())
    improved = copy.deepcopy(actual)
    improved[LIBRARIES[0]].add("missing")
    improved_result = analyze(improved, reference)
    regressed = copy.deepcopy(improved)
    regressed[LIBRARIES[0]].remove("present")
    regressed[LIBRARIES[1]].add("extra")
    # Same total MISSING count can conceal a new missing symbol; reject it.
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "baseline.json"
        assert save_baseline(path, refs, result)
        assert save_baseline(path, refs, improved_result)
        before = path.read_bytes()
        assert not save_baseline(path, refs, analyze(regressed, reference))
        assert path.read_bytes() == before
        old = json.loads(before)
        assert compare_baseline(old, refs, improved_result) == {"new": [], "removed": []}
        changed = copy.deepcopy(refs)
        changed[LIBRARIES[0]]["sha256"] = "changed"
        expect_invalid(lambda: compare_baseline(old, changed, improved_result))
    vanished = copy.deepcopy(actual)
    vanished[LIBRARIES[0]].clear()
    assert analyze(vanished, reference)[LIBRARIES[0]]["MISSING"] == ["missing", "present"]
    for bad in ({}, {**surface, "api": 22}, {**surface, "native_exports": []},
                {**surface, "native_exports": [surface["native_exports"][0]] * 2},
                {**surface, "native_exports": [{"library": "unknown.so", "symbol": "x"}]},
                {**surface, "native_exports": [{"library": "liblog.so", "symbol": 3}]}):
        expect_invalid(lambda: production_exports(bad))
    assert nm_exports("function T 100 4\ndata D 104 4\n") == {"function", "data"}
    for bad in ("", "x U 0 0", "warning", "x T 0 4\nx T 0 4"):
        expect_invalid(lambda: nm_exports(bad))
    print("boundary_exports self-test passed (production surface, data, monotone baseline, pinned oracle)")


def expect_invalid(action):
    try:
        action()
    except ValueError:
        return
    raise AssertionError("invalid input was accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--surface", type=Path, help="production ogplay-cli dexvm dump-surface JSON")
    parser.add_argument("--stubs", type=Path, default=NDK / "sysroot/usr/lib/arm-linux-androideabi/19")
    parser.add_argument("--nm", type=Path, default=NDK / "bin/llvm-nm")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--report", type=Path, help="write machine-readable evidence including stub identities")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check-baseline", action="store_true")
    mode.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.surface:
        parser.error("--surface is required")
    actual = production_exports(json.loads(args.surface.read_text(encoding="utf-8")))
    references, reference = reference_exports(args.stubs, args.nm)
    result = analyze(actual, reference)
    for library, row in result.items():
        print(f'{library}: MISSING={len(row["MISSING"])} EXTRA={len(row["EXTRA"])}')
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({"schema": 1, "api": 19, "references": references,
                                          "libraries": result}, indent=2) + "\n", encoding="utf-8")
    if args.check_baseline or args.update_baseline:
        if args.baseline.exists():
            changes = compare_baseline(json.loads(args.baseline.read_text(encoding="utf-8")), references, result)
            print("Baseline changes: " + json.dumps(changes))
            if changes["new"]:
                return 1
        elif not args.update_baseline:
            raise ValueError("baseline missing; initialize with --update-baseline")
        if args.update_baseline and not save_baseline(args.baseline, references, result):
            return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        print("boundary_exports: " + str(error), file=sys.stderr)
        raise SystemExit(2)
