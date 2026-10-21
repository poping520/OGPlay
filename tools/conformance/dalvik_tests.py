#!/usr/bin/env python3
"""Run AOSP 4.4 dalvik/tests against `ogplay-cli dexvm run-dex`.

Each test is copied into a work directory, compiled with javac (source/target
1.7 against the pinned API 19 android.jar) and dx --debug as etc/default-build
does (d8 1.5 strips javac assertion code and has no switch to keep it), run on
both interpreter backends, and its stdout compared byte-for-byte with the
test's expected.txt (the 4.4 dalvikvm output; trailing CR stripped as run-test
does). The AOSP tree stays read-only. javac/android.jar resolution is shared
with tools/bootdex/build_bootdex.py; dx.jar sits next to its d8.jar, extracted
from the same build-tools 29.0.2 archive (OGPLAY_DX_JAR overrides).

Outcome classes:
  pass                  stdout equals expected.txt
  fail:output           stdout differs, no unimplemented evidence
  fail:unimplemented    stdout differs and the ledger or output names a gap
  fail:crash            run-dex internal error (exit 70) or a host signal
  fail:timeout          wall budget exceeded
  excluded:compile      javac/d8 rejected the unmodified sources
  excluded:scope        outside AGENTS.md scope (reason recorded)
  excluded:harness      needs harness support not built yet (reason recorded)
  excluded:oracle       expected.txt differs from target Dalvik 4.4 (source recorded)

This is a conformance probe, not a gate: results are evidence for planning.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_TESTS = REPO / ".local/aosp/dalvik/tests"
DEFAULT_OUT = REPO / ".local/analysis/wu6"
DEFAULT_CLI = REPO / "build/release/ogplay-cli"
BASELINE = Path(__file__).with_name("dalvik_baseline.json")
BACKENDS = ("switch", "threaded")
INTERNAL_ERROR_EXIT = 70

# Fixed exclusions; every entry carries its reason. Do not edit to raise a pass rate.
EXCLUDED = {
    "001-nop": ("excluded:harness", "run script only echoes text; the VM is never started"),
    "063-process-manager": ("excluded:scope", "spawns host child processes (Runtime.exec)"),
    "068-classloader": ("excluded:harness", "loads a secondary src-ex dex from a device path"),
    "071-dexfile": ("excluded:harness", "src-ex secondary dex, DexFile on device paths and a native library"),
    "089-many-methods": ("excluded:harness", "expects dx's own 64K-reference build error; the VM is never started"),
}

# Fixed oracle mismatches against the target Dalvik 4.4 device semantics.
ORACLE_DIVERGENCE = {
    "300-package-override": (
        "excluded:oracle",
        "expected.txt 对应 4.4 之后的修复；4.4 的 createVtable 因 b/7301030 有意允许（vm/oo/Class.cpp:2919）"),
}

# Ports of the custom `build` scripts (classes removed after the first javac pass).
REMOVE_AFTER_SRC = {
    "003-omnibus-opcodes": ["UnresClass.class"],
    "004-annotations": ["android/test/anno/MissingAnnotation.class"],
}
BUILD_NOTES = {
    "085-old-style-inner-class": "original build uses -source 1.4; JDK 17 compiles it as 1.7",
    "056-const-string-jumbo": "original dx flags --no-optimize --positions=none --no-locals not mirrored",
}


@dataclass
class Result:
    test: str
    backend: str
    outcome: str
    detail: str = ""
    exit_code: int | None = None
    seconds: float = 0.0
    ledger_top: list[str] = field(default_factory=list)
    build_note: str = ""


def toolchain():
    sys.path.insert(0, str(REPO / "tools/bootdex"))
    import build_bootdex  # noqa: E402  (shared resolver, see module docstring)
    dx_jar = Path(os.environ.get("OGPLAY_DX_JAR") or build_bootdex.D8_JAR.with_name("dx.jar"))
    if not dx_jar.exists():
        raise SystemExit(f"dx.jar not found at {dx_jar}; extract android-10/lib/dx.jar from "
                         ".local/toolchain/downloads/build-tools_r29.0.2-macosx.zip")
    return build_bootdex.JAVAC, build_bootdex.JAVA, build_bootdex.ANDROID_JAR, dx_jar


def generate_sources(test: str, work: Path) -> None:
    src = work / "src"
    if test == "023-many-interfaces":
        subprocess.run(["cc", "-o", "iface-gen", "iface-gen.c"], cwd=work, check=True,
                       capture_output=True)
        subprocess.run(["./iface-gen"], cwd=work, check=True, capture_output=True)
    elif test == "056-const-string-jumbo":
        for name, start, end in (("Zorch1", 0, 16383), ("Zorch2", 16384, 32767)):
            lines = [f"public class {name} {{"]
            lines += [f'    static public final String s{i} = "{i}";' for i in range(start, end + 1)]
            (src / f"{name}.java").write_text("\n".join(lines + ["}"]) + "\n")
    elif test == "089-many-methods":
        src.mkdir(exist_ok=True)
        for name, member in (("FillerStatic", "static public int staticInt"),
                             ("FillerField", "public int fieldInt")):
            body = [f"    {member}{i};" for i in range(1, 65501)]
            (src / f"{name}.java").write_text("\n".join([f"public class {name} {{", *body, "}"]) + "\n")
        body = [f"    public void meth{i}() {{ }}" for i in range(1, 65501)]
        (src / "FillerMethod.java").write_text("\n".join(["public class FillerMethod {", *body, "}"]) + "\n")


def first_error(text: str) -> str:
    for line in text.splitlines():
        if "error" in line.lower():
            return line.strip()[:300]
    lines = [line for line in text.splitlines() if line.strip()]
    return lines[0].strip()[:300] if lines else "no diagnostic"


def crash_message(stderr: str) -> str:
    # FormatError prints an "error [ogplay]" header; the cause is the next line.
    for line in stderr.splitlines():
        text = line.strip()
        if text and not text.startswith("error [") and not text.startswith("warn "):
            return text[:300]
    return first_error(stderr)


def build(test: str, tests_dir: Path, work: Path, tools) -> tuple[Path | None, str]:
    javac, java, android_jar, dx_jar = tools
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree(tests_dir / test, work)
    generate_sources(test, work)
    classes = work / "classes"
    classes.mkdir()
    base = [str(javac), "-nowarn", "-encoding", "UTF-8", "-source", "1.7", "-target", "1.7",
            "-bootclasspath", str(android_jar), "-cp", str(classes), "-d", str(classes)]
    for pass_dir in ("src", "src2"):
        sources = sorted(str(p) for p in (work / pass_dir).rglob("*.java")) if (work / pass_dir).exists() else []
        if not sources:
            continue
        done = subprocess.run(base + sources, capture_output=True, text=True)
        if done.returncode != 0:
            return None, f"javac {pass_dir}: " + first_error(done.stderr + done.stdout)
        if pass_dir == "src":
            for relative in REMOVE_AFTER_SRC.get(test, []):
                (classes / relative).unlink()
    class_files = sorted(str(p) for p in classes.rglob("*.class"))
    if not class_files:
        return None, "no class files produced"
    dex = work / "classes.dex"
    done = subprocess.run([str(java), "-Xmx1024m", "-cp", str(dx_jar), "com.android.dx.command.Main",
                           "--dex", "--debug", f"--output={dex}", str(classes)],
                          capture_output=True, text=True)
    if done.returncode != 0 or not dex.exists():
        return None, "dx: " + first_error(done.stderr + done.stdout)
    return dex, ""


def normalize(data: bytes) -> bytes:
    return b"\n".join(line.rstrip(b"\r") for line in data.split(b"\n"))


def classify(stdout: bytes, expected: bytes, exit_code: int | None, timed_out: bool,
             ledger_hits: list[str]) -> tuple[str, str]:
    if timed_out:
        return "fail:timeout", "wall budget exceeded"
    if exit_code is not None and (exit_code == INTERNAL_ERROR_EXIT or exit_code < 0):
        return "fail:crash", f"run-dex exit {exit_code}"
    if normalize(stdout) == normalize(expected):
        return "pass", ""
    got, want = normalize(stdout).split(b"\n"), normalize(expected).split(b"\n")
    for index in range(max(len(got), len(want))):
        g = got[index] if index < len(got) else b"<missing>"
        w = want[index] if index < len(want) else b"<extra>"
        if g != w:
            detail = f"line {index + 1}: expected {w[:120]!r} got {g[:120]!r}"
            break
    if ledger_hits or b"UnsatisfiedLinkError" in stdout:
        return "fail:unimplemented", detail
    return "fail:output", detail


def run_one(cli: Path, dex: Path, expected: bytes, out: Path, backend: str, timeout: float):
    ledger = out / f"ledger-{backend}.json"
    started = time.monotonic()
    timed_out, exit_code = False, None
    try:
        # run-test passes -ea to dalvikvm for every test.
        done = subprocess.run([str(cli), "dexvm", "run-dex", str(dex), "--backend", backend,
                               "--enable-assertions", "--ledger-json", str(ledger)],
                              capture_output=True, timeout=timeout)
        stdout, stderr, exit_code = done.stdout, done.stderr, done.returncode
    except subprocess.TimeoutExpired as expired:
        stdout, stderr, timed_out = expired.stdout or b"", expired.stderr or b"", True
    seconds = time.monotonic() - started
    (out / f"stdout-{backend}.txt").write_bytes(stdout)
    (out / f"stderr-{backend}.txt").write_bytes(stderr)
    hits: list[str] = []
    if ledger.exists():
        entries = json.loads(ledger.read_text()).get("unimplemented", [])
        hits = [f"{e['id']} x{e['count']}" for e in sorted(entries, key=lambda e: -e["count"])]
    outcome, detail = classify(stdout, expected, exit_code, timed_out, hits)
    if outcome == "fail:crash":
        detail += ": " + crash_message(stderr.decode("utf-8", "replace"))
    return outcome, detail, exit_code, seconds, hits[:3]


def run_test(test: str, args, tools) -> list[Result]:
    if test in EXCLUDED or test in ORACLE_DIVERGENCE:
        outcome, reason = (ORACLE_DIVERGENCE if test in ORACLE_DIVERGENCE else EXCLUDED)[test]
        return [Result(test, b, outcome, reason) for b in args.backends]
    out = args.out / "tests" / test
    out.mkdir(parents=True, exist_ok=True)
    dex, error = build(test, args.tests_dir, args.out / "build" / test, tools)
    note = BUILD_NOTES.get(test, "")
    if dex is None:
        (out / "build-error.txt").write_text(error + "\n")
        return [Result(test, b, "excluded:compile", error, build_note=note) for b in args.backends]
    expected = (args.tests_dir / test / "expected.txt").read_bytes()
    results = []
    for backend in args.backends:
        outcome, detail, code, seconds, hits = run_one(args.cli, dex, expected, out, backend, args.timeout)
        results.append(Result(test, backend, outcome, detail, code, round(seconds, 2), hits, note))
    return results


def summarize(results: list[Result], out: Path, backends) -> str:
    lines = ["# dalvik/tests raw summary (generated by tools/conformance/dalvik_tests.py)", ""]
    for backend in backends:
        rows = [r for r in results if r.backend == backend]
        counts: dict[str, int] = {}
        for r in rows:
            counts[r.outcome] = counts.get(r.outcome, 0) + 1
        ran = sum(v for k, v in counts.items() if not k.startswith("excluded"))
        rate = f"{counts.get('pass', 0)}/{ran}" if ran else "0/0"
        lines.append(f"- {backend}: pass {rate}; " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))
    lines += ["", "| test | backend | outcome | detail | ledger top | s |", "| --- | --- | --- | --- | --- | --- |"]
    for r in sorted(results, key=lambda r: (r.test, r.backend)):
        detail = (r.detail + (f" [{r.build_note}]" if r.build_note else "")).replace("|", "\\|")
        lines.append(f"| {r.test} | {r.backend} | {r.outcome} | {detail[:200]} | "
                     f"{'; '.join(r.ledger_top)[:120]} | {r.seconds} |")
    diverging = sorted({r.test for r in results} - {
        t for t in {r.test for r in results}
        if len({r.outcome for r in results if r.test == t}) == 1})
    lines += ["", "Backend-divergent tests: " + (", ".join(diverging) if diverging else "none")]
    text = "\n".join(lines) + "\n"
    (out / "summary.md").write_text(text)
    (out / "summary.json").write_text(json.dumps([asdict(r) for r in results], indent=1) + "\n")
    return text


def compare_baseline(baseline: dict, results: list[Result], backends=BACKENDS,
                     only: list[str] | None = None) -> dict[str, list[str]]:
    current: dict[str, dict[str, str]] = {}
    for r in results:
        current.setdefault(r.test, {})[r.backend] = r.outcome
    tests = set(only) if only is not None else set(baseline) | set(current)
    changes: dict[str, list[str]] = {"regressions": [], "new passes": [], "other changes": [], "oracle migrations": []}
    for test in sorted(tests):
        for backend in backends:
            before = baseline.get(test, {}).get(backend, "missing")
            after = current.get(test, {}).get(backend, "missing")
            if before == after:
                continue
            migration = before == "pass" and after == "excluded:oracle" and test in ORACLE_DIVERGENCE
            kind = "oracle migrations" if migration else "regressions" if before == "pass" else (
                "new passes" if after == "pass" else "other changes")
            changes[kind].append(f"{test} {backend}: {before} -> {after}")
    return changes


def check_baseline(path: Path, baseline: dict, results: list[Result], backends=BACKENDS,
                   only: list[str] | None = None, update: bool = False) -> int:
    changes = compare_baseline(baseline, results, backends, only)
    for kind, rows in changes.items():
        print(f"Baseline {kind}: {len(rows)}")
        for row in rows:
            print(f"  {row}")
    if changes["regressions"]:
        if update:
            print("Baseline update refused: regressions detected.")
        return 1
    if update:
        # Merge only observed results; subset/backend runs must retain other passes.
        merged = {test: dict(outcomes) for test, outcomes in baseline.items()}
        for r in results:
            merged.setdefault(r.test, {})[r.backend] = r.outcome
        path.write_text(json.dumps(merged, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"Baseline updated: {path}")
    return 0


def self_test() -> None:
    assert normalize(b"a\r\nb") == normalize(b"a\nb")
    assert classify(b"x\n", b"x\n", 0, False, [])[0] == "pass"
    assert classify(b"x\n", b"x\n", 1, False, [])[0] == "pass"  # dalvik compares output only
    assert classify(b"", b"x\n", None, True, [])[0] == "fail:timeout"
    assert classify(b"", b"x\n", INTERNAL_ERROR_EXIT, False, [])[0] == "fail:crash"
    assert classify(b"", b"x\n", -6, False, [])[0] == "fail:crash"
    assert classify(b"y\n", b"x\n", 0, False, ["dexvm.x x1"])[0] == "fail:unimplemented"
    outcome, detail = classify(b"a\nb\n", b"a\nc\n", 0, False, [])
    assert outcome == "fail:output" and detail.startswith("line 2"), detail
    assert all(kind.startswith("excluded:") and reason for kind, reason in EXCLUDED.values())
    assert crash_message("error [ogplay]\n  class is not available: LX;\n") == "class is not available: LX;"
    baseline = {"a": {"switch": "pass", "threaded": "pass"},
                "b": {"switch": "fail:output", "threaded": "fail:crash"}}
    results = [Result("a", "switch", "fail:timeout"), Result("a", "threaded", "pass"),
               Result("b", "switch", "pass"), Result("b", "threaded", "fail:output")]
    changes = compare_baseline(baseline, results)
    assert changes == {"regressions": ["a switch: pass -> fail:timeout"],
                       "new passes": ["b switch: fail:output -> pass"],
                       "other changes": ["b threaded: fail:crash -> fail:output"], "oracle migrations": []}
    for outcome in ("fail:output", "fail:unimplemented", "fail:crash", "fail:timeout",
                    "excluded:compile", "excluded:scope", "excluded:harness", "excluded:oracle"):
        assert len(compare_baseline(baseline, [Result("a", "switch", outcome)],
                                    ("switch",), ["a"])["regressions"]) == 1
    assert len(compare_baseline(baseline, [])["regressions"]) == 2
    subset = [Result("b", "switch", "pass")]
    assert compare_baseline(baseline, subset, ("switch",), ["b"]) == {
        "regressions": [], "new passes": ["b switch: fail:output -> pass"], "other changes": [], "oracle migrations": []}
    assert not compare_baseline(baseline, [Result("a", "switch", "pass")],
                                ("switch",), ["a"])["regressions"]
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "baseline.json"
        original = json.dumps(baseline).encode("utf-8")
        path.write_bytes(original)
        assert check_baseline(path, baseline, results, update=True) == 1
        assert path.read_bytes() == original
        assert check_baseline(path, baseline, subset, ("switch",), ["b"], update=True) == 0
        merged = json.loads(path.read_text(encoding="utf-8"))
        assert merged == {**baseline, "b": {"switch": "pass", "threaded": "fail:crash"}}
        assert check_baseline(path, merged, subset, ("switch",), ["b"]) == 0
    from types import SimpleNamespace
    assert not (set(EXCLUDED) & set(ORACLE_DIVERGENCE))
    assert all(kind == "excluded:oracle" and "vm/oo/Class.cpp:2919" in reason
               for kind, reason in ORACLE_DIVERGENCE.values())
    oracle_test = "300-package-override"
    oracle_results = run_test(oracle_test, SimpleNamespace(backends=BACKENDS), None)
    assert all(r.outcome == "excluded:oracle" and "b/7301030" in r.detail for r in oracle_results)
    oracle_baseline = {oracle_test: {b: "pass" for b in BACKENDS}}
    migration = compare_baseline(oracle_baseline, oracle_results)
    assert migration == {"regressions": [], "new passes": [], "other changes": [],
                         "oracle migrations": [f"{oracle_test} {b}: pass -> excluded:oracle" for b in BACKENDS]}
    for bad in ("fail:output", "excluded:scope", "missing"):
        rows = [] if bad == "missing" else [Result(oracle_test, b, bad) for b in BACKENDS]
        assert len(compare_baseline(oracle_baseline, rows)["regressions"]) == 2
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "baseline.json"
        path.write_text(json.dumps(oracle_baseline), encoding="utf-8")
        assert check_baseline(path, oracle_baseline, oracle_results, update=True) == 0
        assert json.loads(path.read_text(encoding="utf-8")) == {
            oracle_test: {b: "excluded:oracle" for b in BACKENDS}}
        mixed = {**oracle_baseline, "a": {"switch": "pass"}}
        original = json.dumps(mixed)
        path.write_text(original, encoding="utf-8")
        assert check_baseline(path, mixed, oracle_results + [Result("a", "switch", "fail:crash")],
                              update=True) == 1
        assert path.read_text(encoding="utf-8") == original
    print("dalvik_tests self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tests-dir", type=Path, default=DEFAULT_TESTS)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--backend", dest="backends", action="append", choices=BACKENDS)
    parser.add_argument("--timeout", type=float, default=60.0, help="per run wall seconds")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--only", action="append", help="test name (repeatable)")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--check-baseline", action="store_true", help="fail if a baseline pass regresses")
    parser.add_argument("--update-baseline", action="store_true", help="update baseline only without regressions")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    args.backends = tuple(args.backends or BACKENDS)
    baseline = json.loads(BASELINE.read_text(encoding="utf-8")) if (
        args.check_baseline or args.update_baseline) else None
    args.out = args.out.resolve()
    if not args.cli.exists():
        parser.error(f"ogplay-cli not found: {args.cli}")
    tests = sorted(p.name for p in args.tests_dir.iterdir()
                   if p.is_dir() and re.match(r"\d{3}-", p.name) and (p / "expected.txt").exists())
    if args.only:
        unknown = set(args.only) - set(tests)
        if unknown:
            parser.error(f"unknown tests: {', '.join(sorted(unknown))}")
        tests = [t for t in tests if t in set(args.only)]
    tools = toolchain()
    results: list[Result] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for batch in pool.map(lambda t: run_test(t, args, tools), tests):
            results.extend(batch)
            for r in batch:
                print(f"{r.test:32} {r.backend:8} {r.outcome:20} {r.detail[:90]}", flush=True)
    print(summarize(results, args.out, args.backends).split("\n\n")[1])
    if baseline is not None:
        return check_baseline(BASELINE, baseline, results, args.backends, args.only, args.update_baseline)
    return 0


if __name__ == "__main__":
    sys.exit(main())
