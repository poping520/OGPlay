#!/usr/bin/env python3
"""Run one validated OGPlay Scenario v1 through the loopback MCP session.

Authoring aids (M9 usability batch):
  --fresh   wipe an existing evidence directory instead of failing;
  --watch   incremental authoring mode: execute the current checkpoints,
            keep the session alive, and when the scenario file changes
            execute only checkpoints appended after the unchanged prefix.
            Editing an already-executed checkpoint (or any failure)
            restarts the session and replays everything in a new
            gen<N> evidence directory. Ctrl-C shuts the session down
            cleanly and writes the final Result v1 for the last
            generation. Deterministic frame/tick budgets stay enforced;
            the *total* wall-time budget is reinterpreted per checkpoint
            run because an authoring session is open-ended.
Failed checkpoints always collect a failure_logs.txt evidence tail even
when the scenario did not request log evidence.
"""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass, replace
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
from typing import Any, Callable, Sequence
from urllib.error import URLError
from urllib.request import Request, urlopen

from scenario_model import (
    ActionType,
    AssertionResult,
    AssertionType,
    CheckpointResult,
    ResultStatus,
    ScenarioCheckpoint,
    ScenarioPlan,
    ScenarioResult,
    ShutdownResult,
    load_plan,
    validate_result_schema,
)
from validate_scenarios import _profile_text, _scenario_text


class RunnerError(RuntimeError):
    pass


class McpClient:
    def __init__(self, endpoint: str, timeout: float = 5.0) -> None:
        self.endpoint = endpoint
        self.timeout = timeout
        self.next_id = 1

    def call(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        request_id = self.next_id
        self.next_id += 1
        body = json.dumps({
            "jsonrpc": "2.0", "id": request_id, "method": "tools/call",
            "params": {"name": name, "arguments": arguments or {}},
        }, separators=(",", ":")).encode("utf-8")
        request = Request(
            self.endpoint, data=body, method="POST",
            headers={
                "Content-Type": "application/json",
                "Accept": "application/json, text/event-stream",
            },
        )
        with urlopen(request, timeout=self.timeout) as response:
            document = json.loads(response.read().decode("utf-8"))
        if document.get("id") != request_id or "error" in document:
            raise RunnerError(f"invalid MCP response for {name}: {document}")
        result = document.get("result")
        if not isinstance(result, dict):
            raise RunnerError(f"MCP {name} omitted result")
        if result.get("isError") is True:
            content = result.get("content", [])
            message = content[0].get("text", "tool failed") if content else "tool failed"
            raise RunnerError(f"MCP {name}: {message}")
        if result.get("isError") is not False:
            raise RunnerError(f"MCP {name} omitted isError=false")
        return result

    def structured(self, name: str,
                   arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        result = self.call(name, arguments)
        structured = result.get("structuredContent")
        if not isinstance(structured, dict):
            raise RunnerError(f"MCP {name} omitted structuredContent")
        return structured

    def capture_overlay_png(self) -> bytes:
        """Coordinate-grid screenshot used by watch-mode click authoring."""
        result = self.call("frame_capture",
                           {"format": "png", "overlay": "coordinates"})
        images = [item for item in result.get("content", [])
                  if isinstance(item, dict) and item.get("type") == "image"]
        if len(images) != 1:
            raise RunnerError("MCP frame_capture overlay returned no image")
        return base64.b64decode(images[0]["data"], validate=True)

    def capture_png(self) -> tuple[dict[str, Any], bytes]:
        result = self.call("frame_capture", {"format": "png"})
        structured = result.get("structuredContent")
        if not isinstance(structured, dict):
            raise RunnerError("MCP frame_capture omitted structuredContent")
        images = [item for item in result.get("content", [])
                  if isinstance(item, dict) and item.get("type") == "image"]
        if len(images) != 1 or images[0].get("mimeType") != "image/png":
            raise RunnerError("MCP frame_capture did not return one PNG image")
        try:
            return structured, base64.b64decode(images[0]["data"], validate=True)
        except (KeyError, ValueError) as error:
            raise RunnerError("MCP frame_capture returned invalid Base64") from error


@dataclass
class LaunchedSession:
    process: subprocess.Popen[bytes]
    stdout_file: Any
    stderr_file: Any

    def close_logs(self) -> None:
        self.stdout_file.close()
        self.stderr_file.close()


def _milliseconds(start: float) -> int:
    return max(0, round((time.monotonic() - start) * 1000.0))


def _tail_lines(path: Path, count: int = 200) -> str:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    lines = text.splitlines()
    return "\n".join(lines[-count:])


def _checkpoint_signature(checkpoint: ScenarioCheckpoint) -> str:
    # Dataclass repr is deterministic for identical content across reloads.
    return repr(checkpoint)


def _unchanged_prefix(executed_signatures: Sequence[str],
                      checkpoints: Sequence[ScenarioCheckpoint]) -> bool:
    if len(checkpoints) < len(executed_signatures):
        return False
    return all(_checkpoint_signature(checkpoints[index]) == signature
               for index, signature in enumerate(executed_signatures))


def _state(client: McpClient) -> dict[str, Any]:
    state = client.structured("session_state")
    required = {
        "lifecycle", "frame", "guestTicks", "presentedFrame", "movieRequest",
        "processExit", "guestFault", "shutdownRequested",
    }
    if set(state) != required:
        raise RunnerError("session_state fields do not match the runner contract")
    return state


class ScenarioExecutor:
    def __init__(self, plan: ScenarioPlan, client: McpClient, process: Any,
                 evidence_dir: Path, stdout_path: Path, stderr_path: Path,
                 started_at: float) -> None:
        self.plan = plan
        self.client = client
        self.process = process
        self.evidence_dir = evidence_dir
        self.stdout_path = stdout_path
        self.stderr_path = stderr_path
        self.started_at = started_at
        self.initial_state: dict[str, Any] | None = None
        self.last_state: dict[str, Any] | None = None
        self.shutdown_sent = False
        self.capture_cache: tuple[int, dict[str, Any], bytes] | None = None

    def _check_total(self, state: dict[str, Any]) -> None:
        initial = self.initial_state or {"frame": 0, "guestTicks": 0}
        limits = self.plan.limits
        if state["frame"] - initial["frame"] > limits["total_frames"]:
            raise RunnerError("scenario total frame budget exceeded")
        if state["guestTicks"] - initial["guestTicks"] > limits["total_ticks"]:
            raise RunnerError("scenario total guest tick budget exceeded")
        if _milliseconds(self.started_at) > limits["total_wall_time_ms"]:
            raise RunnerError("scenario total wall-time budget exceeded")

    def _check_checkpoint(self, checkpoint: ScenarioCheckpoint,
                          start_state: dict[str, Any], started: float,
                          state: dict[str, Any]) -> None:
        self._check_total(state)
        if state["frame"] - start_state["frame"] > checkpoint.max_frames:
            raise RunnerError(f"checkpoint {checkpoint.id} frame budget exceeded")
        if state["guestTicks"] - start_state["guestTicks"] > checkpoint.max_ticks:
            raise RunnerError(f"checkpoint {checkpoint.id} guest tick budget exceeded")
        if _milliseconds(started) > checkpoint.wall_time_ms:
            raise RunnerError(f"checkpoint {checkpoint.id} wall-time budget exceeded")
        if state["guestFault"] is not None:
            raise RunnerError(f"guest fault: {state['guestFault']}")

    def _wait_state(self, predicate: Callable[[dict[str, Any]], bool],
                    checkpoint: ScenarioCheckpoint | None = None,
                    start_state: dict[str, Any] | None = None,
                    started: float | None = None) -> dict[str, Any]:
        while True:
            if self.process.poll() is not None:
                raise RunnerError(
                    f"OGPlay exited before checkpoint completion: {self.process.poll()}")
            state = _state(self.client)
            self.last_state = state
            if checkpoint is not None and start_state is not None and started is not None:
                self._check_checkpoint(checkpoint, start_state, started, state)
            if predicate(state):
                return state
            time.sleep(0.005)

    def wait_startup(self) -> dict[str, Any]:
        limits = self.plan.limits
        while True:
            if self.process.poll() is not None:
                raise RunnerError(f"OGPlay exited during startup: {self.process.poll()}")
            try:
                state = _state(self.client)
            except (URLError, ConnectionError, TimeoutError):
                state = None
            if state is not None:
                if state["guestFault"] is not None:
                    raise RunnerError(f"startup guest fault: {state['guestFault']}")
                if state["frame"] > limits["startup_frames"]:
                    raise RunnerError("startup frame budget exceeded")
                if state["guestTicks"] > limits["startup_ticks"]:
                    raise RunnerError("startup guest tick budget exceeded")
                if state["lifecycle"] == "running":
                    self.initial_state = state
                    self.last_state = state
                    return state
            if _milliseconds(self.started_at) > limits["startup_wall_time_ms"]:
                raise RunnerError("startup wall-time budget exceeded")
            time.sleep(0.01)

    def _wait_target(self, target: int, checkpoint: ScenarioCheckpoint,
                     start_state: dict[str, Any], started: float) -> dict[str, Any]:
        return self._wait_state(
            lambda state: state["frame"] >= target,
            checkpoint, start_state, started)

    def _action(self, checkpoint: ScenarioCheckpoint,
                start_state: dict[str, Any], started: float) -> dict[str, Any]:
        action = checkpoint.action
        if action.type is ActionType.STEP:
            response = self.client.structured("step", action.arguments)
            return self._wait_target(response["targetFrame"], checkpoint,
                                     start_state, started)
        if action.type is ActionType.CLICK:
            self.client.structured("click", action.arguments)
        elif action.type is ActionType.SWIPE:
            arguments = {
                "startX": action.arguments["start_x"],
                "startY": action.arguments["start_y"],
                "endX": action.arguments["end_x"],
                "endY": action.arguments["end_y"],
                "steps": action.arguments["steps"],
            }
            self.client.structured("swipe", arguments)
        elif action.type in {ActionType.SUSPEND, ActionType.RESUME}:
            expected = "suspended" if action.type is ActionType.SUSPEND else "running"
            self.client.structured("lifecycle", {"action": action.type.value})
            return self._wait_state(
                lambda state: state["lifecycle"] == expected,
                checkpoint, start_state, started)
        elif action.type is ActionType.SHUTDOWN:
            self.client.structured("shutdown")
            self.shutdown_sent = True
        return self.last_state or start_state

    def _frame(self) -> tuple[dict[str, Any], bytes]:
        sequence = int((self.last_state or {}).get("presentedFrame") or 0)
        if self.capture_cache is None or self.capture_cache[0] != sequence:
            metadata, image = self.client.capture_png()
            self.capture_cache = (metadata["sequence"], metadata, image)
        return self.capture_cache[1], self.capture_cache[2]

    def _assertions(self, checkpoint: ScenarioCheckpoint,
                    state: dict[str, Any]) -> tuple[AssertionResult, ...]:
        results: list[AssertionResult] = []
        for assertion in checkpoint.assertions:
            expected = assertion.expected
            passed = False
            actual: dict[str, Any]
            if assertion.type is AssertionType.FRAME:
                sequence = state["presentedFrame"] or 0
                actual = {"sequence": sequence}
                passed = sequence >= expected["minimum_sequence"]
                if passed and "sha256" in expected:
                    _, image = self._frame()
                    digest = hashlib.sha256(image).hexdigest()
                    actual["sha256"] = digest
                    passed = digest == expected["sha256"]
            elif assertion.type is AssertionType.MOVIE_REQUEST:
                movie = state["movieRequest"]
                actual = movie or {"sequence": 0, "name": None}
                passed = movie is not None and movie["sequence"] >= expected["minimum_sequence"]
                if passed and "name" in expected:
                    passed = movie["name"] == expected["name"]
            elif assertion.type is AssertionType.LIFECYCLE:
                actual = {"state": state["lifecycle"]}
                passed = state["lifecycle"] == expected["state"]
            elif assertion.type is AssertionType.PROCESS_EXIT:
                return_code = self.process.poll()
                exited = return_code is not None
                actual = {"exited": exited, "returnCode": return_code}
                passed = exited == expected["expected"]
            else:
                actual = {"guestFault": state["guestFault"]}
                passed = state["guestFault"] is None
            results.append(AssertionResult(
                type=assertion.type.value, passed=passed,
                expected=expected, actual=actual))
        return tuple(results)

    def _evidence(self, checkpoint: ScenarioCheckpoint,
                  state: dict[str, Any]) -> tuple[str, ...]:
        directory = self.evidence_dir / checkpoint.id
        directory.mkdir(parents=True, exist_ok=True)
        references: list[str] = []
        for kind in checkpoint.evidence:
            if kind == "state":
                path = directory / "state.json"
                path.write_text(json.dumps(state, ensure_ascii=False, sort_keys=True,
                                           separators=(",", ":")) + "\n",
                                encoding="utf-8", newline="\n")
            elif kind == "frame":
                _, image = self._frame()
                path = directory / "frame.png"
                path.write_bytes(image)
            elif kind == "logs":
                for path in (self.stdout_path, self.stderr_path):
                    references.append(path.relative_to(self.evidence_dir).as_posix())
                continue
            else:
                raise RunnerError("gpu_trace evidence is not available through MCP")
            references.append(path.relative_to(self.evidence_dir).as_posix())
        return tuple(references)

    def checkpoint(self, checkpoint: ScenarioCheckpoint) -> CheckpointResult:
        start_state = _state(self.client)
        self.last_state = start_state
        started = time.monotonic()
        error: str | None = None
        results: tuple[AssertionResult, ...] = ()
        evidence: tuple[str, ...] = ()
        try:
            state = self._action(checkpoint, start_state, started)
            while True:
                if checkpoint.action.type is ActionType.SHUTDOWN:
                    try:
                        self.process.wait(timeout=checkpoint.wall_time_ms / 1000.0)
                    except subprocess.TimeoutExpired as timeout:
                        raise RunnerError("shutdown checkpoint wall-time budget exceeded") from timeout
                    state = self.last_state or start_state
                else:
                    state = _state(self.client)
                    self.last_state = state
                    self._check_checkpoint(checkpoint, start_state, started, state)
                results = self._assertions(checkpoint, state)
                if all(item.passed for item in results):
                    break
                if checkpoint.action.type is ActionType.SHUTDOWN:
                    raise RunnerError("shutdown assertions were not satisfied")
                if state["lifecycle"] != "running":
                    raise RunnerError("unsatisfied assertions cannot advance a non-running session")
                if state["frame"] - start_state["frame"] >= checkpoint.max_frames:
                    raise RunnerError(f"checkpoint {checkpoint.id} frame budget exhausted")
                response = self.client.structured("step", {"frames": 1})
                state = self._wait_target(
                    response["targetFrame"], checkpoint, start_state, started)
            evidence = self._evidence(checkpoint, state)
        except (RunnerError, URLError, TimeoutError, KeyError, ValueError) as caught:
            error = str(caught)
            state = self.last_state or start_state
            try:
                results = results or self._assertions(checkpoint, state)
                evidence = self._evidence(checkpoint, state)
            except (RunnerError, URLError, TimeoutError, KeyError, ValueError):
                pass
            # Failures always keep a log tail next to the checkpoint even
            # when the scenario did not request log evidence: the failing
            # moment is exactly when the diagnostics matter.
            try:
                directory = self.evidence_dir / checkpoint.id
                directory.mkdir(parents=True, exist_ok=True)
                tail_path = directory / "failure_logs.txt"
                tail_path.write_text(
                    "=== stderr tail ===\n" +
                    _tail_lines(self.stderr_path) +
                    "\n=== stdout tail ===\n" +
                    _tail_lines(self.stdout_path) + "\n",
                    encoding="utf-8", newline="\n")
                evidence = evidence + (
                    tail_path.relative_to(self.evidence_dir).as_posix(),)
            except OSError:
                pass
        status = ResultStatus.PASSED.value if error is None else ResultStatus.FAILED.value
        return CheckpointResult(
            id=checkpoint.id, status=status,
            startFrame=start_state["frame"], endFrame=state["frame"],
            startTicks=start_state["guestTicks"], endTicks=state["guestTicks"],
            wallTimeMs=_milliseconds(started), assertions=results,
            evidence=evidence, error=error)

    def shutdown(self) -> ShutdownResult:
        error: str | None = None
        requested = self.shutdown_sent
        if self.process.poll() is None and not requested:
            try:
                self.client.structured("shutdown")
                self.shutdown_sent = True
                requested = True
            except (RunnerError, URLError, TimeoutError) as caught:
                error = str(caught)
        if self.process.poll() is None:
            try:
                self.process.wait(timeout=10.0)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=2.0)
                error = error or "OGPlay required forced process cleanup"
        return_code = self.process.poll()
        clean = return_code == 0 and error is None
        if not clean and error is None:
            error = f"OGPlay exited with status {return_code}"
        return ShutdownResult(requested=requested, clean=clean, error=error)

    def run(self) -> ScenarioResult:
        checkpoints: list[CheckpointResult] = []
        first_failure: str | None = None
        try:
            self.wait_startup()
            for checkpoint in self.plan.checkpoints:
                result = self.checkpoint(checkpoint)
                checkpoints.append(result)
                if result.status == ResultStatus.FAILED.value:
                    first_failure = f"{checkpoint.id}: {result.error}"
                    break
        except (RunnerError, URLError, TimeoutError, KeyError, ValueError) as error:
            first_failure = f"startup: {error}"
        shutdown = self.shutdown()
        if not shutdown.clean and first_failure is None:
            first_failure = f"shutdown: {shutdown.error}"
        status = (ResultStatus.PASSED.value if first_failure is None
                  else ResultStatus.FAILED.value)
        return ScenarioResult(
            scenarioId=self.plan.id, status=status, profile=self.plan.profile,
            checkpoints=tuple(checkpoints), firstFailure=first_failure,
            shutdown=shutdown)


def _parse_fixtures(values: Sequence[str]) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for value in values:
        fixture_id, separator, path = value.partition("=")
        if not separator or not fixture_id or not path or fixture_id in result:
            raise RunnerError("--fixture requires unique ID=PATH values")
        result[fixture_id] = Path(path).resolve()
    return result


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def _launch(plan: ScenarioPlan, fixtures: dict[str, Path], ogplay: Path,
            system_dir: Path, profiles: Path, evidence_dir: Path,
            port: int) -> LaunchedSession:
    expected = {item["id"] for item in plan.fixtures if item["required"]}
    missing = sorted(expected - fixtures.keys())
    unknown = sorted(fixtures.keys() - {item["id"] for item in plan.fixtures})
    if missing or unknown:
        raise RunnerError(f"fixture mapping mismatch: missing={missing}, unknown={unknown}")
    apk_paths = [fixtures[item["id"]] for item in plan.fixtures
                 if item["kind"] == "apk" and item["id"] in fixtures]
    if len(apk_paths) != 1:
        raise RunnerError("runner requires exactly one mapped APK fixture")
    for path in fixtures.values():
        if not path.exists():
            raise RunnerError(f"fixture path does not exist: {path}")
    command = [
        str(ogplay), "run-apk", str(apk_paths[0]), "--system-dir", str(system_dir),
        "--profiles-dir", str(profiles), "--mcp-port", str(port),
        "--mcp-manual-step",
        # Scenario results have to be reproducible, so a run never inherits
        # or leaves behind saved state (ADR-0020 02 §4). Persistence is
        # covered by CTest, not by scenarios.
        "--ephemeral-sandbox",
    ]
    external = [fixtures[item["id"]] for item in plan.fixtures
                if item["kind"] == "external" and item["id"] in fixtures]
    if len(external) > 1:
        raise RunnerError("runner supports at most one external fixture")
    if external:
        command.extend(["--external-dir", str(external[0])])
    if any(item["kind"] == "obb" and item["id"] in fixtures for item in plan.fixtures):
        raise RunnerError("OBB fixture launch is not implemented")
    stdout_path = evidence_dir / "stdout.log"
    stderr_path = evidence_dir / "stderr.log"
    stdout_file = stdout_path.open("wb")
    stderr_file = stderr_path.open("wb")
    environment = os.environ.copy()
    environment.setdefault("SDL_AUDIODRIVER", "dummy")
    try:
        process = subprocess.Popen(
            command, cwd=ogplay.parent, env=environment,
            stdout=stdout_file, stderr=stderr_file)
    except Exception:
        stdout_file.close()
        stderr_file.close()
        raise
    return LaunchedSession(process, stdout_file, stderr_file)


class _FakeProcess:
    def __init__(self) -> None:
        self.return_code: int | None = None

    def poll(self) -> int | None:
        return self.return_code

    def wait(self, timeout: float | None = None) -> int:
        if self.return_code is None:
            raise subprocess.TimeoutExpired("fake", timeout)
        return self.return_code

    def terminate(self) -> None:
        self.return_code = -15

    def kill(self) -> None:
        self.return_code = -9


def self_test(result_schema: Path) -> int:
    validate_result_schema(result_schema)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        profiles = root / "profiles"
        profiles.mkdir()
        (profiles / "org.example.legacy.profile.toml").write_text(
            _profile_text(), encoding="utf-8", newline="\n")
        scenario = root / "legacy.boot.scenario.toml"
        scenario.write_text(_scenario_text(), encoding="utf-8", newline="\n")
        plan = load_plan(scenario, profiles)
        process = _FakeProcess()
        state: dict[str, Any] = {
            "lifecycle": "running", "frame": 0, "guestTicks": 0,
            "presentedFrame": None, "movieRequest": None,
            "processExit": False, "guestFault": None,
            "shutdownRequested": False,
        }
        png = b"\x89PNG\r\n\x1a\nfixture"

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self) -> None:
                length = int(self.headers["Content-Length"])
                request = json.loads(self.rfile.read(length))
                name = request["params"]["name"]
                arguments = request["params"]["arguments"]
                structured: dict[str, Any] = {}
                content: list[dict[str, Any]] = []
                if name == "session_state":
                    structured = dict(state)
                elif name == "step":
                    start = state["frame"]
                    state["frame"] += arguments["frames"]
                    state["guestTicks"] += arguments["frames"] * 1000
                    state["presentedFrame"] = state["frame"]
                    if state["frame"] >= 2:
                        state["movieRequest"] = {"sequence": 1, "name": "logo.mp4"}
                    structured = {"requestSequence": 1, "startingFrame": start,
                                  "targetFrame": state["frame"],
                                  "frames": arguments["frames"]}
                elif name in {"click", "swipe"}:
                    structured = {"requestSequence": 1}
                elif name == "shutdown":
                    state["shutdownRequested"] = True
                    process.return_code = 0
                    structured = {"requestSequence": 2, "startingFrame": state["frame"],
                                  "action": "shutdown"}
                elif name == "frame_capture":
                    structured = {"sequence": state["frame"], "width": 1,
                                  "height": 1, "format": "png", "overlay": "none"}
                    content = [{"type": "image", "mimeType": "image/png",
                                "data": base64.b64encode(png).decode("ascii")}]
                result = {"content": content, "structuredContent": structured,
                          "isError": False}
                response = json.dumps({"jsonrpc": "2.0", "id": request["id"],
                                       "result": result}).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(response)))
                self.end_headers()
                self.wfile.write(response)

            def log_message(self, *_: Any) -> None:
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        evidence = root / "evidence"
        evidence.mkdir()
        stdout_path = evidence / "stdout.log"
        stderr_path = evidence / "stderr.log"
        stdout_path.write_text("started\n", encoding="utf-8")
        stderr_path.write_text("", encoding="utf-8")
        executor = ScenarioExecutor(
            plan, McpClient(f"http://127.0.0.1:{server.server_port}/mcp"),
            process, evidence, stdout_path, stderr_path, time.monotonic())
        result = executor.run()
        assert result.status == "passed"
        assert len(result.checkpoints) == 2
        assert result.checkpoints[1].endFrame == 2
        assert result.checkpoints[1].assertions[0].actual["name"] == "logo.mp4"
        assert result.shutdown.clean
        assert (evidence / "first_frame" / "frame.png").read_bytes() == png
        assert json.loads(result.to_json())["schema"] == 1

        process = _FakeProcess()
        state.clear()
        state.update({
            "lifecycle": "running", "frame": 0, "guestTicks": 0,
            "presentedFrame": None, "movieRequest": None,
            "processExit": False, "guestFault": None,
            "shutdownRequested": False,
        })
        failure_plan = replace(
            plan,
            checkpoints=(replace(plan.checkpoints[1], max_frames=1),),
        )
        failed_evidence = root / "failed"
        failed_evidence.mkdir()
        failed_stdout = failed_evidence / "stdout.log"
        failed_stderr = failed_evidence / "stderr.log"
        failed_stdout.write_text("started\n", encoding="utf-8")
        failed_stderr.write_text("", encoding="utf-8")
        failed = ScenarioExecutor(
            failure_plan,
            McpClient(f"http://127.0.0.1:{server.server_port}/mcp"),
            process, failed_evidence, failed_stdout, failed_stderr,
            time.monotonic()).run()
        assert failed.status == "failed"
        assert failed.checkpoints[0].endFrame == 1
        assert "frame budget exhausted" in (failed.firstFailure or "")
        assert failed.shutdown.clean
        # Failures always collect a log tail as extra evidence.
        failure_tail = failed_evidence / failure_plan.checkpoints[0].id / \
            "failure_logs.txt"
        assert failure_tail.is_file()
        assert "started" in failure_tail.read_text(encoding="utf-8")
        assert any(reference.endswith("failure_logs.txt")
                   for reference in failed.checkpoints[0].evidence)

        # Watch-mode prefix comparison and tail helper.
        checkpoints = plan.checkpoints
        signatures = [_checkpoint_signature(item) for item in checkpoints]
        assert _unchanged_prefix(signatures, checkpoints)
        assert _unchanged_prefix(signatures[:1], checkpoints)
        assert not _unchanged_prefix(signatures, checkpoints[:1])
        edited = (replace(checkpoints[0], max_frames=checkpoints[0].max_frames + 1),) + checkpoints[1:]
        assert not _unchanged_prefix(signatures, edited)
        tail_probe = root / "tail.txt"
        tail_probe.write_text("\n".join(str(index) for index in range(300)),
                              encoding="utf-8")
        tail = _tail_lines(tail_probe, 200)
        assert tail.splitlines()[0] == "100" and tail.splitlines()[-1] == "299"
        assert _tail_lines(root / "absent.txt") == ""

        # --fresh evidence handling.
        fresh_dir = root / "fresh"
        fresh_dir.mkdir()
        (fresh_dir / "stale.txt").write_text("old", encoding="utf-8")
        prepared = _prepare_evidence_dir(fresh_dir, fresh=True)
        assert prepared.is_dir() and not any(prepared.iterdir())
        try:
            _prepare_evidence_dir(fresh_dir, fresh=False)
            raise AssertionError("existing evidence dir was accepted")
        except (FileExistsError, OSError):
            pass

        try:
            _parse_fixtures(["duplicate=/one", "duplicate=/two"])
            raise AssertionError("duplicate fixture mapping was accepted")
        except RunnerError:
            pass
        server.shutdown()
        server.server_close()
        worker.join()
    print("automation Scenario runner self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--result-schema", type=Path,
                        default=Path(__file__).parent.parent / "data/scenarios/scenario-result-v1.schema.json")
    parser.add_argument("--scenario", type=Path)
    parser.add_argument("--profiles", type=Path)
    parser.add_argument("--ogplay", type=Path)
    parser.add_argument("--system-dir", type=Path)
    parser.add_argument("--fixture", action="append", default=[])
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--fresh", action="store_true",
                        help="wipe an existing evidence directory")
    parser.add_argument("--watch", action="store_true",
                        help="incremental authoring mode (see module doc)")
    return parser.parse_args(argv)


def _prepare_evidence_dir(path: Path, fresh: bool) -> Path:
    evidence = path.resolve()
    if fresh and evidence.exists():
        import shutil
        shutil.rmtree(evidence)
    evidence.mkdir(parents=True, exist_ok=False)
    return evidence


def _write_result(evidence: Path, result: ScenarioResult) -> None:
    (evidence / "result.json").write_text(
        result.to_json() + "\n", encoding="utf-8", newline="\n")


def _run_once(plan: ScenarioPlan, fixtures: dict[str, Path], ogplay: Path,
              system_dir: Path, profiles: Path,
              evidence: Path) -> ScenarioResult:
    port = _free_port()
    started = time.monotonic()
    launched = _launch(plan, fixtures, ogplay, system_dir, profiles,
                       evidence, port)
    try:
        executor = ScenarioExecutor(
            plan, McpClient(f"http://127.0.0.1:{port}/mcp"), launched.process,
            evidence, evidence / "stdout.log", evidence / "stderr.log",
            started)
        return executor.run()
    finally:
        launched.close_logs()


class _WatchGeneration:
    """One live session executing a growing checkpoint prefix."""

    def __init__(self, plan: ScenarioPlan, fixtures: dict[str, Path],
                 ogplay: Path, system_dir: Path, profiles: Path,
                 evidence: Path) -> None:
        self.evidence = evidence
        self.port = _free_port()
        self.launched = _launch(plan, fixtures, ogplay, system_dir, profiles,
                                evidence, self.port)
        self.client = McpClient(f"http://127.0.0.1:{self.port}/mcp")
        self.executor = ScenarioExecutor(
            plan, self.client, self.launched.process, evidence,
            evidence / "stdout.log", evidence / "stderr.log",
            time.monotonic())
        self.plan = plan
        self.signatures: list[str] = []
        self.results: list[CheckpointResult] = []
        self.failed = False
        self.progress_path = evidence / "watch_progress.jsonl"
        self.executor.wait_startup()

    def _note(self, message: str) -> None:
        print(f"[watch] {message}", flush=True)

    def _record(self, result: CheckpointResult) -> None:
        with self.progress_path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps({
                "id": result.id, "status": result.status,
                "endFrame": result.endFrame, "endTicks": result.endTicks,
                "error": result.error,
            }, separators=(",", ":")) + "\n")

    def _overlay(self, checkpoint_id: str) -> None:
        try:
            image = self.client.capture_overlay_png()
        except (RunnerError, URLError, TimeoutError, ValueError):
            return
        directory = self.evidence / checkpoint_id
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "frame_overlay.png").write_bytes(image)

    def execute_from(self, checkpoints: Sequence[ScenarioCheckpoint]) -> None:
        for checkpoint in checkpoints[len(self.signatures):]:
            # An open-ended authoring session reinterprets the total
            # wall-time budget per checkpoint run; frame/tick budgets are
            # deterministic and stay absolute.
            self.executor.started_at = time.monotonic()
            result = self.executor.checkpoint(checkpoint)
            self.signatures.append(_checkpoint_signature(checkpoint))
            self.results.append(result)
            self._record(result)
            state = self.executor.last_state or {}
            self._note(f"checkpoint {result.id}: {result.status} "
                       f"frame={result.endFrame} ticks={result.endTicks} "
                       f"presented={state.get('presentedFrame')}"
                       + (f" error={result.error}" if result.error else ""))
            self._overlay(checkpoint.id)
            if result.status == ResultStatus.FAILED.value:
                self.failed = True
                self._note("session stays alive; edit the scenario to "
                           "retry (any change restarts the session)")
                break

    def finish(self) -> ScenarioResult:
        shutdown = self.executor.shutdown()
        self.launched.close_logs()
        first_failure = None
        for result in self.results:
            if result.status == ResultStatus.FAILED.value:
                first_failure = f"{result.id}: {result.error}"
                break
        if first_failure is None and not shutdown.clean:
            first_failure = f"shutdown: {shutdown.error}"
        status = (ResultStatus.PASSED.value if first_failure is None
                  else ResultStatus.FAILED.value)
        return ScenarioResult(
            scenarioId=self.plan.id, status=status, profile=self.plan.profile,
            checkpoints=tuple(self.results), firstFailure=first_failure,
            shutdown=shutdown)

    def abort(self) -> None:
        try:
            self.executor.shutdown()
        finally:
            self.launched.close_logs()


def _run_watch(args: argparse.Namespace, fixtures: dict[str, Path],
               ogplay: Path, evidence: Path) -> int:
    # Background jobs of non-interactive shells inherit an ignored SIGINT,
    # in which case Python never raises KeyboardInterrupt; restore both
    # SIGINT and SIGTERM so the graceful-finish path always works.
    import signal

    def _finish(_signum: int, _frame: Any) -> None:
        raise KeyboardInterrupt

    signal.signal(signal.SIGINT, _finish)
    signal.signal(signal.SIGTERM, _finish)

    scenario = args.scenario.resolve()
    profiles = args.profiles.resolve()
    system_dir = args.system_dir.resolve()

    def load() -> ScenarioPlan:
        return load_plan(scenario, profiles)

    plan = load()
    digest = hashlib.sha256(scenario.read_bytes()).hexdigest()
    generation = 0
    current: _WatchGeneration | None = None
    try:
        while True:
            if current is None:
                generation += 1
                gen_dir = evidence / f"gen{generation:02d}"
                gen_dir.mkdir(parents=True, exist_ok=False)
                print(f"[watch] generation {generation}: replaying "
                      f"{len(plan.checkpoints)} checkpoint(s) into "
                      f"{gen_dir}", flush=True)
                current = _WatchGeneration(plan, fixtures, ogplay,
                                           system_dir, profiles, gen_dir)
                current.execute_from(plan.checkpoints)
                print("[watch] waiting for scenario changes "
                      "(Ctrl-C to finish)", flush=True)
            time.sleep(0.5)
            try:
                new_digest = hashlib.sha256(scenario.read_bytes()).hexdigest()
            except OSError:
                continue
            if new_digest == digest:
                continue
            digest = new_digest
            try:
                plan = load()
            except Exception as error:  # validation feedback, keep waiting
                print(f"[watch] scenario is invalid: {error}", flush=True)
                continue
            if (not current.failed and
                    _unchanged_prefix(current.signatures, plan.checkpoints)):
                appended = len(plan.checkpoints) - len(current.signatures)
                print(f"[watch] prefix unchanged; executing {appended} "
                      f"appended checkpoint(s)", flush=True)
                current.plan = plan
                current.executor.plan = plan
                current.execute_from(plan.checkpoints)
                print("[watch] waiting for scenario changes "
                      "(Ctrl-C to finish)", flush=True)
            else:
                reason = ("a previous checkpoint failed" if current.failed
                          else "the executed prefix changed")
                print(f"[watch] {reason}; restarting the session",
                      flush=True)
                current.abort()
                current = None
    except KeyboardInterrupt:
        print("[watch] finishing: clean shutdown + final result",
              flush=True)
        if current is None:
            return 2
        result = current.finish()
        _write_result(current.evidence, result)
        _write_result(evidence, result)
        print(result.to_json())
        return 0 if result.status == ResultStatus.PASSED.value else 1


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return self_test(args.result_schema)
    required = {name: getattr(args, name) for name in
                ("scenario", "profiles", "ogplay", "system_dir", "evidence_dir")}
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RunnerError("missing runner arguments: " + ", ".join(missing))
    validate_result_schema(args.result_schema)
    plan = load_plan(args.scenario.resolve(), args.profiles.resolve())
    fixtures = _parse_fixtures(args.fixture)
    ogplay = args.ogplay.resolve()
    if not ogplay.is_file():
        raise RunnerError(f"OGPlay executable does not exist: {ogplay}")
    evidence = _prepare_evidence_dir(args.evidence_dir, args.fresh)
    if args.watch:
        return _run_watch(args, fixtures, ogplay, evidence)
    result = _run_once(plan, fixtures, ogplay, args.system_dir.resolve(),
                       args.profiles.resolve(), evidence)
    _write_result(evidence, result)
    print(result.to_json())
    return 0 if result.status == ResultStatus.PASSED.value else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (RunnerError, OSError, ValueError) as error:
        # One machine-readable line on stdout (invalid runs previously
        # printed bare text, breaking any JSON-consuming wrapper), plus the
        # human-readable line on stderr.
        print(json.dumps({"schema": 1, "status": "invalid",
                          "reason": str(error)}, separators=(",", ":")))
        print(f"Scenario runner failed: {error}", file=sys.stderr)
        raise SystemExit(2)
