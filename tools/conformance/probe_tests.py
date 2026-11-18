#!/usr/bin/env python3
"""Build, run, and compare the OGPlay API 19 capability probe."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import datetime
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import time
from urllib.request import ProxyHandler, Request, build_opener
from uuid import uuid4

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "samples/probe"
BASELINE = Path(__file__).with_name("probe_baseline.json")
PACKAGE = "org.ogplay.probe"
DEVICE_DIR = "/sdcard/OGPlayProbe"
SCHEMA = "ogplay.probe.report/1"
STATUSES = ("pass", "fail", "error", "crash", "timeout", "skip", "info")
CONFIGS = {
    "a": dict(width_px=720, height_px=1280, density_dpi=320, cpu_cores=2, cpu_frequency_mhz=1000),
    "b": dict(width_px=480, height_px=800, density_dpi=240, cpu_cores=4, cpu_frequency_mhz=1500),
}
FLAGS = dict(width_px="--surface-width", height_px="--surface-height", density_dpi="--density-dpi",
             cpu_cores="--cpu-cores", cpu_frequency_mhz="--cpu-frequency-mhz")
UI_DEGRADED = re.compile(r"\bfrontend\.run_apk\.ui_degraded (.+?) count=(\d+)(?: |$)")
TARGET = re.compile(r"(L[\w/$]+;)\s*(?:->|\.)\s*([\w$<>]+)\s*(\([^\s]*\)[\w/$;\[]+|:[\w/$;\[]+)?")
LEDGER_APIS = {
    "dexvm.alarm_delivery": "Landroid/app/AlarmManager;.set(",
    "dexvm.process.exec": "Ljava/lang/Runtime;.exec(",
    "ui.degraded.Notification display": "Landroid/app/NotificationManager;.notify(",
}


def write_json(path: Path, value) -> None:
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temp.replace(path)


def read_json(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} should contain a JSON object")
    return value


def optional_json(path: Path) -> dict:
    return read_json(path) if path.exists() else {}


def guest_json(external: Path, overlay: Path, name: str) -> dict:
    path = overlay / name
    return optional_json(path if path.exists() else external / name)


def events_in(*paths: Path) -> list[dict]:
    events = []
    for path in paths:
        with path.open(encoding="utf-8", errors="replace") as log:
            for line in log:
                _, marker, suffix = line.partition("runtime.dexvm.guest OGProbe: ")
                if marker:
                    try:
                        entry, _ = json.JSONDecoder().raw_decode(suffix)
                    except ValueError:
                        continue
                    if isinstance(entry, dict):
                        events.append(entry)
    return events


def summary_counts(rows: list[dict]) -> dict[str, int]:
    counts = Counter(row["status"] for row in rows)
    return {"total": len(rows), **{status: counts[status] for status in STATUSES}}


class LedgerSampler:
    def __init__(self):
        with socket.socket() as server:
            server.bind(("127.0.0.1", 0))
            self.port = server.getsockname()[1]
        self.opener = build_opener(ProxyHandler({}))
        self.hits = {}
        self.samples = 0
        self.truncated = False

    def capture(self, pid: int) -> None:
        query = {"jsonrpc": "2.0", "id": 1, "method": "dash.snapshot", "params": {"sections": ["capabilities"]}}
        request = Request(f"http://127.0.0.1:{self.port}/dash/rpc",
                          json.dumps(query).encode("utf-8"), {"Content-Type": "application/json"})
        try:
            with self.opener.open(request, timeout=0.2) as response:
                result = json.load(response).get("result", {})
        except (OSError, ValueError):
            return
        # A concurrently starting runner can claim the port between bind and launch.
        if result.get("process_id") != pid:
            return
        section = result.get("capabilities", {})
        if section.get("status") not in ("partial", "complete"):
            return
        self.truncated |= "limit" in section.get("reason", "")
        self.samples += 1
        for hit in section.get("data", []):
            self.hits[hit["id"]] = {"kind": "unimplemented", **hit}

    def collect(self, stderr: Path) -> list[dict]:
        with stderr.open(encoding="utf-8", errors="replace") as source:
            for line in source:
                match = UI_DEGRADED.search(line)
                if match:
                    symbol, count = match.groups()
                    self.hits[symbol] = {"kind": "ui_degraded", "id": symbol, "count": int(count)}
        return list(self.hits.values())


def attempt(args: argparse.Namespace, external: Path, number: int, deadline: float) -> tuple[dict, list[dict]]:
    stdout = args.out / f"stdout-{number:02}.log"
    stderr = args.out / f"stderr-{number:02}.log"
    run_report = args.out / f"run-report-{number:02}.json"
    sampler = LedgerSampler()
    command = [str(args.cli), "run-apk", str(args.apk), "--sandbox-dir", str(args.out / "sandbox"),
               "--installation-id", PACKAGE, "--external-dir", str(external.parent),
               "--run-report", str(run_report), "--exit-after-frames", str(args.frames),
               "--max-fps", "60", "--dexvm-interpreter", args.interpreter, "--mcp-port", str(sampler.port)]
    for name, value in CONFIGS[args.config].items():
        command.extend((FLAGS[name], str(value)))
    timed_out = False
    with stdout.open("wb") as output, stderr.open("wb") as errors:
        with subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=errors) as process:
            try:
                while process.poll() is None:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        timed_out = True
                        break
                    sampler.capture(process.pid)
                    try:
                        process.wait(timeout=min(0.15, remaining))
                    except subprocess.TimeoutExpired:
                        continue
            finally:
                if process.poll() is None:
                    process.kill()
                code = process.wait()
    diagnostics = {"attempt": number, "exit_code": code, "wall_timeout": timed_out,
                   "stdout": stdout.name, "stderr": stderr.name,
                   "ledger_samples": sampler.samples, "ledger_truncated": sampler.truncated,
                   "run_report": optional_json(run_report)}
    hits = [{**hit, "attempt": number} for hit in sampler.collect(stderr)]
    return diagnostics, hits


def api_signature(value: str) -> tuple[str, str, str] | None:
    match = TARGET.search(value)
    return match.groups(default="") if match else None


def mentions(exception: dict | None, symbol: str) -> bool:
    if not exception:
        return False
    messages = [exception.get("message"), *(cause.get("message") for cause in exception.get("causes", []))]
    return any(message and (symbol in message or symbol.removeprefix("dexvm.intrinsic.") in message)
               for message in messages)


def related(symbol: str, row: dict) -> bool:
    if mentions(row.get("exception"), symbol):
        return True
    prefix = LEDGER_APIS.get(symbol)
    if prefix and any(api.startswith(prefix) for api in row.get("api", [])):
        return True
    service = symbol.removeprefix("dexvm.service_manager.")
    if service != symbol:
        service = service.split(".")[-1].removesuffix("_manager")
        if row["id"] == f"services.registry.{service}" or row["id"].startswith(f"services.{service}."):
            return True
    signature = api_signature(symbol)
    for api in row.get("api", []):
        if symbol == api or "." not in api and symbol.endswith("." + api):
            return True
        candidate = api_signature(api)
        if not candidate:
            continue
        if signature and signature[:2] == candidate[:2] and (not signature[2] or signature[2] == candidate[2]):
            return True
        owner = candidate[0][1:-1].replace("/", ".")
        names = (owner + "." + candidate[1], owner.rsplit(".", 1)[-1] + "." + candidate[1])
        if any(symbol == name or symbol.startswith(name + "(") or symbol.endswith("." + name) for name in names):
            return True
    return False


def hits_for(rows: list[dict], ledger: list[dict]) -> list[dict]:
    for entry in ledger:
        entry["probe_ids"] = [row["id"] for row in rows if related(entry["id"], row)]
    return sorted(ledger, key=lambda row: (row["id"], row["attempt"]))


def compare(report: dict, baseline: dict) -> tuple[list[str], list[str]]:
    current = {row["id"]: row["status"] for row in report["results"]}
    prefixes = report.get("host", {}).get("filter", [])
    selected = lambda name: not prefixes or any(name.startswith(prefix) for prefix in prefixes)
    regressions = [f"{name}: pass → {current.get(name, 'missing')}"
                   for name in sorted(baseline) if selected(name) and current.get(name) != "pass"]
    additions = sorted(name for name, status in current.items()
                       if name not in baseline and status == "pass" and not name.startswith("selftest."))
    return regressions, additions


def brief(value) -> str:
    if value is None:
        return "—"
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).replace("|", "\\|").replace("`", "'")


def summary_markdown(report: dict, baseline: dict, reference: dict | None = None) -> str:
    host = report["host"]
    lines = ["# OGPlay 兼容性测试", "",
             f"配置 {host['config']} · {host['interpreter']} · 启动 {len(host['attempts'])} 次 · "
             + ("报告完整" if host["complete"] else "报告未完成"),
             "", "| 分类 | 通过 / 总数 | 通过率 |", "| --- | ---: | ---: |"]
    categories = defaultdict(list)
    for row in report["results"]:
        categories[row["category"]].append(row)
    for name, rows in sorted(categories.items()):
        counts = summary_counts(rows)
        lines.append(f"| {name} | {counts['pass']} / {counts['total']} | {counts['pass'] / counts['total']:.1%} |")
    lines.extend(("", "## 首错与退出", ""))
    for run in host["attempts"]:
        detail = run["run_report"]
        if host.get("serial"):
            lines.append(f"- Android 真机 `{host['serial']}` · {detail['end']}")
            continue
        problem = detail.get("error") or detail.get("exit_error") or ""
        lines.append(f"- 第 {run['attempt']} 次 · {detail.get('end') or 'unknown'} · "
                     f"退出码 {run['exit_code']} · ledger 采样 {run['ledger_samples']} 次"
                     + (" · 宿主超时" if run["wall_timeout"] else "")
                     + (" · ledger 达到采样上限" if run["ledger_truncated"] else "")
                     + (f" · {brief(problem)}" if problem else "")
                     + f" · [stderr]({run['stderr']})")
    if host["pending"]:
        lines.append("- 尚未完成 " + ", ".join(f"`{name}`" for name in host["pending"]))
    for event in host["framework_errors"]:
        lines.append(f"- 探针框架异常 `{brief(event.get('exception'))}`")
    grouped = defaultdict(list)
    for hit in host["ledger"]:
        for name in hit["probe_ids"]:
            grouped[name].append(hit)
    for section, statuses in (("缺口", {"fail", "error", "crash", "timeout"}), ("信息项", {"info"})):
        lines.extend(("", f"## {section}", ""))
        for category, rows in sorted(categories.items()):
            open_rows = [row for row in rows if row["status"] in statuses]
            if not open_rows:
                continue
            lines.extend((f"### {category}", "",
                          "| ID | Scope | Status | 说明 | 首个异常或差异 | Ledger |",
                          "| --- | --- | --- | --- | --- | --- |"))
            for row in open_rows:
                evidence = {"observed": row.get("observed"), "expected": row.get("expected")}
                exception = row.get("exception")
                if exception:
                    detail = brief({key: exception.get(key) for key in ("class", "message")})
                    if evidence["observed"] is not None:
                        detail += " " + brief(evidence)
                else:
                    detail = brief(evidence)
                related = ", ".join(f"`{hit['id']}` ×{hit['count']}" for hit in grouped[row["id"]]) or "—"
                lines.append(f"| `{row['id']}` | {row['scope']} | {row['status']} | "
                             f"{str(row.get('message', '')).replace('|', '&#124;')} | `{detail}` | {related} |")
            lines.append("")
    lines.extend(("", "## 跳过项", ""))
    skipped = [row for row in report["results"] if row["status"] == "skip"]
    lines.extend(f"- `{row['id']}` · {row.get('message', '')}" for row in skipped)
    if not skipped:
        lines.append("无跳过项。")
    lines.extend(("## 未关联的 ledger 条目", ""))
    orphaned = [hit for hit in host["ledger"] if not hit["probe_ids"]]
    lines.extend(f"- 第 {hit['attempt']} 次 · {hit['kind']} · `{hit['id']}` ×{hit['count']}" for hit in orphaned)
    if not orphaned:
        lines.append("无已采集条目。")
    lines.extend(("", "## 基线变化", ""))
    if host.get("serial"):
        lines.append("真机报告用于判据校准。")
    elif host["complete"]:
        regressions, additions = compare(report, baseline)
        lines.extend(f"- 回归 {item}" for item in regressions)
        lines.extend(f"- 新通过 `{item}`" for item in additions)
        if not regressions and not additions:
            lines.append("无变化。")
    else:
        lines.append("报告未完成，未判定基线。")
    if reference:
        label = reference.get("host", {})
        lines.extend(("", f"## 状态对比（{label.get('config', '?')} · {label.get('interpreter', '?')}）", ""))
        current = {row["id"]: row["status"] for row in report["results"]}
        earlier = {row["id"]: row["status"] for row in reference["results"]}
        changes = [name for name in sorted(current.keys() | earlier.keys()) if current.get(name) != earlier.get(name)]
        if not label.get("complete", True) or not host["complete"]:
            lines.append("至少一份报告未完成。")
        if changes:
            lines.extend(f"- `{name}` {earlier.get(name, 'missing')} → {current.get(name, 'missing')}" for name in changes)
        else:
            lines.append("所有已记录的探针状态一致。")
    return "\n".join(lines) + "\n"


def run_probes(args: argparse.Namespace, selftest: bool = False) -> dict:
    args.cli = args.cli.resolve(strict=True)
    args.apk = args.apk.resolve(strict=True)
    args.out = (args.out or ROOT / ".local/probe" /
                (datetime.now().strftime("%Y%m%d-%H%M%S-%f") + f"-{args.config}-{args.interpreter}")).resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    if any(args.out.iterdir()):
        raise ValueError(f"run output must be empty: {args.out}")
    external = args.out / "external/OGPlayProbe"
    external.mkdir(parents=True)
    overlay = args.out / "sandbox" / PACKAGE / "sdcard/ogplayprobe"
    if selftest:
        args.filter = ["native.load", "selftest.", "device.build.fields", "storage.prefs.persist_across_restart", "device.identity"]
    session = uuid4().hex
    config = {"session": session, "filter": args.filter, "expect": CONFIGS[args.config],
              "exit": True, "selftest": selftest}
    write_json(external / "autorun.json", config)
    attempts, ledger, events = [], [], []
    progress, guest, previous = {}, {}, {}
    deadline = time.monotonic() + args.timeout
    for n in range(1, args.restarts + 2):
        if time.monotonic() >= deadline:
            break
        result, hits = attempt(args, external, n, deadline)
        attempts.append(result)
        ledger.extend(hits)
        events.extend(events_in(args.out / result["stdout"], args.out / result["stderr"]))
        progress = guest_json(external, overlay, "progress.json")
        guest = guest_json(external, overlay, "report.json")
        if progress.get("session") != session:
            progress = {}
        if guest.get("schema") != SCHEMA:
            guest = {}
        detail = result["run_report"]
        if (progress.get("complete") or not progress or result["wall_timeout"]
                or detail.get("end") == "startup_failed"
                or detail.get("end") in ("closed", "no_frame") and result["exit_code"] == 0):
            break
        # An interrupted attempt which never advanced the old journal is a startup failure for recovery.
        if n > 1 and progress == previous:
            break
        previous = progress

    rows = {}
    for event in events:
        row = event.get("result")
        if event.get("event") == "result" and isinstance(row, dict) and row.get("status") in STATUSES:
            rows[row["id"]] = row
    for path in (overlay / "results", external / "results"):
        for item in sorted(path.glob("*.json")):
            saved = read_json(item)
            if saved.get("session") == session:
                row = saved["result"]
                rows[row["id"]] = row
    for row in guest.get("results", []):
        rows[row["id"]] = row
    plan = guest_json(external, overlay, f"plan-{progress.get('batch')}.json")
    selected = plan.get("selected") or next(
        (event["selected"] for event in events if event.get("event") == "begin" and "selected" in event), [])
    pending = sorted(set(selected) - rows.keys())
    complete = bool(selected) and not pending and (bool(progress.get("complete"))
            or not progress and any(event.get("event") == "end" for event in events))
    results = [rows[name] for name in sorted(rows)]
    if progress:
        write_json(args.out / "progress.json", progress)
    if guest:
        write_json(args.out / "guest-report.json", guest)
    if attempts:
        write_json(args.out / "run-report.json", attempts[-1]["run_report"])
    report = {"schema": SCHEMA, "probe_version": guest.get("probe_version", progress.get("probe_version")),
              "runs": guest.get("runs", progress.get("run", len(attempts))),
              "environment": guest.get("environment", progress.get("environment", {})),
              "summary": summary_counts(results), "results": results,
              "host": {"complete": complete, "config": args.config, "expect": CONFIGS[args.config],
                       "filter": args.filter, "selftest": selftest, "interpreter": args.interpreter,
                       "selected": selected, "pending": pending, "attempts": attempts,
                       "framework_errors": [event for event in events if event.get("event") == "framework_error"],
                       "ledger": hits_for(results, ledger)}}
    write_json(args.out / "report.json", report)
    reference = load_report(args.compare_to) if getattr(args, "compare_to", None) else None
    (args.out / "summary.md").write_text(summary_markdown(report, optional_json(BASELINE), reference), encoding="utf-8")
    print(f"{args.out / 'report.json'}\n{json.dumps(report['summary'], ensure_ascii=False)}")
    return report


def run_device(args: argparse.Namespace) -> dict:
    apk = (SAMPLE / "out/ogplay-probe.apk").resolve(strict=True)
    sdk = os.environ.get("ANDROID_SDK_ROOT") or os.environ.get("ANDROID_HOME")
    adb = str(Path(sdk) / "platform-tools" / "adb") if sdk else shutil.which("adb")
    if not adb:
        raise ValueError("adb not found; set ANDROID_SDK_ROOT or add adb to PATH")
    out = (args.out or ROOT / ".local/probe" /
           (datetime.now().strftime("%Y%m%d-%H%M%S-%f") + "-device")).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        raise ValueError(f"run output must be empty: {out}")
    session = uuid4().hex
    remote_apk = f"/data/local/tmp/ogplay-probe-{session}.apk"
    remote_config = f"{DEVICE_DIR}/autorun-{session}.json"
    config = out / "autorun.json"
    write_json(config, {"session": session, "filter": args.filter, "expect": {},
                        "exit": True, "selftest": False})
    deadline = time.monotonic() + args.timeout

    def call(*command: str, check: bool = True, cleanup: bool = False) -> str:
        result = subprocess.run([adb, "-s", args.serial, *command], capture_output=True,
                                encoding="utf-8", errors="replace",
                                timeout=5 if cleanup else max(0.1, deadline - time.monotonic()))
        if check and result.returncode:
            raise RuntimeError(f"adb {command[0]} failed: {(result.stderr or result.stdout).strip()}")
        return result.stdout.strip() if result.returncode == 0 else ""

    def document(filename: str) -> dict:
        data = call("shell", "cat", f"{DEVICE_DIR}/{filename}", check=False)
        # API 19 adb shell does not propagate the remote command exit code.
        return json.loads(data) if data.startswith("{") else {}

    progress, guest, selected = {}, {}, []
    started = False
    timed_out = False
    try:
        call("push", str(apk), remote_apk)
        installed = call("shell", "su", "-c", f"'pm install -r {remote_apk}'")
        if "Success" not in installed.splitlines():
            raise RuntimeError(f"device APK install failed: {installed}")
        call("shell", "am", "force-stop", PACKAGE)
        call("shell", "mkdir", "-p", DEVICE_DIR)
        call("push", str(config), remote_config)
        call("shell", "mv", remote_config, f"{DEVICE_DIR}/autorun.json")
        started = True
        launch = call("shell", "am", "start", "-n", f"{PACKAGE}/.ProbeActivity")
        if "Error" in launch:
            raise RuntimeError(f"device Activity launch failed: {launch}")
        while time.monotonic() < deadline:
            snapshot = document("progress.json")
            if snapshot.get("session") == session:
                progress = snapshot
                if not selected:
                    selected = document(f"plan-{progress['batch']}.json").get("selected", [])
                if progress.get("complete"):
                    raw = out / "guest-report.json"
                    call("pull", f"{DEVICE_DIR}/report.json", str(raw))
                    guest = load_report(raw)
                    break
            time.sleep(0.25)
        else:
            timed_out = True
    except subprocess.TimeoutExpired:
        timed_out = True
    finally:
        try:
            if started and not guest:
                call("shell", "am", "force-stop", PACKAGE, check=False, cleanup=True)
            paths = [remote_apk, remote_config]
            if started:
                paths.append(f"{DEVICE_DIR}/autorun.json")
            call("shell", "rm", "-f", *paths, check=False, cleanup=True)
        except (OSError, subprocess.TimeoutExpired):
            pass

    results = sorted(guest.get("results", []), key=lambda row: row["id"])
    ids = {row["id"] for row in results}
    pending = sorted(set(selected) - ids)
    complete = (bool(selected) and set(selected) == ids and bool(progress.get("complete"))
                and guest.get("completed_at_ms") == progress.get("completed_at_ms"))
    if progress:
        write_json(out / "progress.json", progress)
    report = {"schema": SCHEMA, "probe_version": guest.get("probe_version", progress.get("probe_version")),
              "runs": guest.get("runs", int(started)), "environment": guest.get("environment", {}),
              "summary": summary_counts(results), "results": results,
              "host": {"complete": complete, "config": "device", "expect": {}, "filter": args.filter,
                       "selftest": False, "interpreter": "dalvik", "serial": args.serial,
                       "selected": selected, "pending": pending, "framework_errors": [], "ledger": [],
                       "attempts": [{"attempt": 1, "exit_code": None, "wall_timeout": timed_out, "stdout": None,
                                     "stderr": None, "ledger_samples": 0, "ledger_truncated": False,
                                     "run_report": {"end": "completed" if complete else "incomplete"}}]
                       if started else []}}
    write_json(out / "report.json", report)
    (out / "summary.md").write_text(summary_markdown(report, {}), encoding="utf-8")
    print(f"{out / 'report.json'}\n{json.dumps(report['summary'], ensure_ascii=False)}")
    return report


def check(reports: list[dict], update: bool) -> int:
    baseline = optional_json(BASELINE)
    if any(value != "pass" for value in baseline.values()):
        raise ValueError("baseline may contain only passes")
    regressions = set()
    passes = []
    for report in reports:
        host = report.get("host", {})
        if not host.get("complete", True) or not report["results"]:
            raise ValueError("check requires a complete report")
        if update and (host.get("selftest") or host.get("filter") or host.get("serial")):
            raise ValueError("baseline update requires unfiltered normal OGPlay reports")
        lost, gained = compare(report, baseline)
        regressions.update(lost)
        passes.append(set(gained))
    additions = set.intersection(*passes)
    for item in sorted(regressions):
        print(f"regression {item}")
    for item in sorted(additions):
        print(f"new pass {item}")
    if update and not regressions:
        updated = {name: "pass" for name in sorted(set(baseline) | set(additions))}
        write_json(BASELINE, updated)
        print(f"baseline contains {len(updated)} passes")
    return int(bool(regressions))


def load_report(path: Path) -> dict:
    document = read_json(path)
    if document.get("schema") != SCHEMA or not isinstance(document.get("results"), list):
        raise ValueError(f"{path} is not a Probe report")
    names = set()
    for row in document["results"]:
        if (not isinstance(row, dict) or not isinstance(row.get("id"), str) or row["id"] in names
                or row.get("status") not in STATUSES or "observed" not in row):
            raise ValueError(f"{path} contains an invalid or duplicate probe entry")
        names.add(row["id"])
    return document


def latest_report() -> Path:
    candidates = sorted((ROOT / ".local/probe").glob("*/report.json"),
                        key=lambda path: path.stat().st_mtime, reverse=True)
    for path in candidates:
        host = load_report(path).get("host", {})
        if host.get("complete") and not host.get("selftest") and not host.get("filter") and not host.get("serial"):
            return path
    raise ValueError("no complete unfiltered report under .local/probe; specify --report")


def diff(left: dict, right: dict) -> int:
    def index(report: dict) -> dict:
        return {row["id"]: {"status": row["status"], "observed": row.get("observed")} for row in report["results"]}

    before, after = index(left), index(right)
    changes = [{"id": name, "before": before.get(name), "after": after.get(name)}
               for name in sorted(before.keys() | after.keys()) if before.get(name) != after.get(name)]
    print(json.dumps(changes, ensure_ascii=False, indent=2))
    return int(bool(changes))


def positive(text: str) -> int:
    value = int(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return value


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
        epilog="Exit 0: success. Exit 1: regression, diff changes, selftest failure or incomplete run. "
               "Exit 2: invalid inputs or build/tool failure. A complete run may report capability gaps.")
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="build and verify the signed APK")
    build.add_argument("--sdk", type=Path)
    build.add_argument("--ndk", type=Path)
    build.add_argument("--build-tools")
    device = commands.add_parser("device", help="run the APK on an attached root-enabled Android device")
    device.add_argument("--serial", required=True)
    device.add_argument("--filter", nargs="+", default=[], metavar="PREFIX")
    device.add_argument("--out", type=Path)
    device.add_argument("--timeout", type=positive, default=3600, help="total wall-clock budget in seconds")
    for name, help_text in (("run", "run the APK and summarize evidence"),
                            ("selftest", "verify crash recovery, timeout isolation and continuation")):
        command = commands.add_parser(name, help=help_text)
        command.add_argument("--cli", type=Path, default=ROOT / "build/dev/ogplay-cli")
        command.add_argument("--apk", type=Path, default=SAMPLE / "out/ogplay-probe.apk")
        command.add_argument("--config", choices=CONFIGS, default="a")
        command.add_argument("--interpreter", choices=("switch", "threaded"), default="threaded")
        command.add_argument("--out", type=Path)
        command.add_argument("--timeout", type=positive, default=300, help="total wall-clock budget in seconds")
        command.add_argument("--frames", type=positive, default=12000, help="frame cap per launch")
        command.add_argument("--restarts", type=int, default=8, help="maximum restarts after the initial launch")
        if name == "run":
            command.add_argument("--filter", nargs="+", default=[], metavar="PREFIX")
            command.add_argument("--compare-to", type=Path, help="add status differences against another report to summary.md")
    check_parser = commands.add_parser("check", help="compare against the monotonic pass baseline")
    check_parser.add_argument("--report", type=Path, help="defaults to the latest complete unfiltered run")
    check_parser.add_argument("--also-report", action="append", type=Path, default=[],
                              help="require new baseline entries to pass in every supplied report")
    check_parser.add_argument("--update-baseline", action="store_true")
    diff_parser = commands.add_parser("diff", help="compare statuses and observations by probe ID")
    diff_parser.add_argument("before", type=Path)
    diff_parser.add_argument("after", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            cmd = [sys.executable, str(SAMPLE / "tools/build.py")]
            for name in ("sdk", "ndk", "build_tools"):
                if value := getattr(args, name):
                    cmd.extend(("--" + name.replace("_", "-"), str(value)))
            return 2 if subprocess.run(cmd, cwd=ROOT).returncode else 0
        if args.command == "check":
            return check([load_report(path) for path in [args.report or latest_report(), *args.also_report]],
                         args.update_baseline)
        if args.command == "diff":
            return diff(load_report(args.before), load_report(args.after))
        if args.command == "device":
            return int(not run_device(args)["host"]["complete"])
        if args.restarts < 0:
            raise ValueError("--restarts must be nonnegative")
        selftest = args.command == "selftest"
        report = run_probes(args, selftest)
        if not report["host"]["complete"]:
            return 1
        if selftest:
            expected = {"native.load": "pass", "selftest.crash": "crash", "selftest.hang": "timeout",
                        "device.build.fields": "pass", "storage.prefs.persist_across_restart": "pass"}
            actual = {row["id"]: row["status"] for row in report["results"]}
            errors = [f"{name}: expected {status}, got {actual.get(name, 'missing')}"
                      for name, status in expected.items() if actual.get(name) != status]
            launches = report["host"]["attempts"]
            if report["runs"] < 2 or len(launches) < 2 or launches[0]["run_report"].get("end") != "crashed":
                errors.append("expected an OGPlay crash followed by an actual relaunch")
            if launches[-1]["exit_code"] != 0:
                errors.append("expected the resumed process to exit successfully")
            for error in errors:
                print(error, file=sys.stderr)
            return int(bool(errors))
        return 0
    except (OSError, RuntimeError, ValueError, TypeError) as error:
        print(f"probe: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
