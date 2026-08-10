#!/usr/bin/env python3
"""Strong Scenario v1 action/assertion/result models shared by AI and CI."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass, field
from enum import Enum
import json
from pathlib import Path
import sys
import tempfile
from typing import Any, Sequence

from validate_scenarios import (
    ScenarioError,
    _profile_identities,
    _profile_text,
    _scenario_text,
    load_scenario,
)


class ActionType(str, Enum):
    STEP = "step"
    CLICK = "click"
    SWIPE = "swipe"
    SUSPEND = "suspend"
    RESUME = "resume"
    SHUTDOWN = "shutdown"


class AssertionType(str, Enum):
    FRAME = "frame"
    MOVIE_REQUEST = "movie_request"
    LIFECYCLE = "lifecycle"
    PROCESS_EXIT = "process_exit"
    NO_GUEST_FAULT = "no_guest_fault"


class ResultStatus(str, Enum):
    PASSED = "passed"
    FAILED = "failed"


@dataclass(frozen=True)
class ScenarioAction:
    type: ActionType
    arguments: dict[str, int]


@dataclass(frozen=True)
class ScenarioAssertion:
    type: AssertionType
    expected: dict[str, Any]


@dataclass(frozen=True)
class ScenarioCheckpoint:
    id: str
    provider: str
    max_frames: int
    max_ticks: int
    wall_time_ms: int
    evidence: tuple[str, ...]
    action: ScenarioAction
    assertions: tuple[ScenarioAssertion, ...]


@dataclass(frozen=True)
class ScenarioPlan:
    id: str
    profile: dict[str, Any]
    fixtures: tuple[dict[str, Any], ...]
    limits: dict[str, int]
    checkpoints: tuple[ScenarioCheckpoint, ...]


@dataclass(frozen=True)
class AssertionResult:
    type: str
    passed: bool
    expected: dict[str, Any]
    actual: dict[str, Any]


@dataclass(frozen=True)
class CheckpointResult:
    id: str
    status: str
    startFrame: int
    endFrame: int
    startTicks: int
    endTicks: int
    wallTimeMs: int
    assertions: tuple[AssertionResult, ...]
    evidence: tuple[str, ...]
    error: str | None = None


@dataclass(frozen=True)
class ShutdownResult:
    requested: bool
    clean: bool
    error: str | None = None


@dataclass(frozen=True)
class ScenarioResult:
    scenarioId: str
    status: str
    profile: dict[str, Any]
    checkpoints: tuple[CheckpointResult, ...]
    firstFailure: str | None
    shutdown: ShutdownResult
    schema: int = field(default=1, init=False)

    def to_json(self) -> str:
        return json.dumps(asdict(self), ensure_ascii=False,
                          sort_keys=True, separators=(",", ":"))


def _action(value: dict[str, Any]) -> ScenarioAction:
    return ScenarioAction(
        ActionType(value["type"]),
        {key: item for key, item in value.items() if key != "type"},
    )


def _assertion(value: dict[str, Any]) -> ScenarioAssertion:
    return ScenarioAssertion(
        AssertionType(value["type"]),
        {key: item for key, item in value.items() if key != "type"},
    )


def load_plan(path: Path, profiles: Path) -> ScenarioPlan:
    document = load_scenario(path, _profile_identities(profiles))
    checkpoints = tuple(
        ScenarioCheckpoint(
            id=item["id"],
            provider=item["provider"],
            max_frames=item["max_frames"],
            max_ticks=item["max_ticks"],
            wall_time_ms=item["wall_time_ms"],
            evidence=tuple(item["evidence"]),
            action=_action(item["action"]),
            assertions=tuple(_assertion(value) for value in item["assertion"]),
        )
        for item in document["checkpoint"]
    )
    return ScenarioPlan(
        id=document["id"],
        profile=dict(document["profile"]),
        fixtures=tuple(dict(item) for item in document["fixture"]),
        limits=dict(document["limits"]),
        checkpoints=checkpoints,
    )


def validate_result_schema(path: Path) -> dict[str, Any]:
    try:
        schema = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ScenarioError(f"cannot read result schema {path}: {error}") from error
    required = {
        "schema", "scenarioId", "status", "profile", "checkpoints",
        "firstFailure", "shutdown",
    }
    if (schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema" or
            schema.get("type") != "object" or
            schema.get("additionalProperties") is not False or
            set(schema.get("required", [])) != required or
            set(schema.get("properties", {})) != required or
            schema["properties"]["schema"].get("const") != 1):
        raise ScenarioError("Scenario result schema root does not match the model")
    statuses = schema["properties"]["status"].get("enum", [])
    if set(statuses) != {item.value for item in ResultStatus}:
        raise ScenarioError("Scenario result statuses do not match the model")
    return schema


def self_test(result_schema: Path) -> int:
    validate_result_schema(result_schema)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        profiles = root / "profiles"
        profiles.mkdir()
        (profiles / "org.example.legacy.profile.toml").write_text(
            _profile_text(), encoding="utf-8", newline="\n"
        )
        scenario = root / "legacy.boot.scenario.toml"
        scenario.write_text(_scenario_text(), encoding="utf-8", newline="\n")
        plan = load_plan(scenario, profiles)
        assert plan.id == "legacy.boot"
        assert plan.checkpoints[0].action.type is ActionType.STEP
        assert plan.checkpoints[1].action.type is ActionType.CLICK
        assert plan.checkpoints[1].assertions[0].type is AssertionType.MOVIE_REQUEST

        result = ScenarioResult(
            scenarioId=plan.id,
            status=ResultStatus.PASSED.value,
            profile=plan.profile,
            checkpoints=(CheckpointResult(
                id="first_frame", status=ResultStatus.PASSED.value,
                startFrame=0, endFrame=1, startTicks=0, endTicks=1000,
                wallTimeMs=5, assertions=(AssertionResult(
                    type="frame", passed=True,
                    expected={"minimum_sequence": 1}, actual={"sequence": 1}),),
                evidence=("first_frame/state.json",)),),
            firstFailure=None,
            shutdown=ShutdownResult(requested=True, clean=True),
        )
        decoded = json.loads(result.to_json())
        assert decoded["schema"] == 1
        assert decoded["status"] == "passed"
        assert decoded["checkpoints"][0]["endFrame"] == 1
    print("automation Scenario strong model self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--result-schema", type=Path, required=True)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if not args.self_test:
        parser.error("--self-test is required")
    return args


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        return self_test(args.result_schema)
    except ScenarioError as error:
        print(f"Scenario model validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
