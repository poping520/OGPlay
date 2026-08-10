#!/usr/bin/env python3
"""Validate OGPlay exact-APK automation Scenario v1 TOML files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import tempfile
import tomllib
from typing import Any, Sequence

from validate_title_profiles import ProfileError, load_profile


MAX_SCENARIO_LINES = 400
MAX_FIXTURES = 32
MAX_CHECKPOINTS = 64
MAX_FRAMES = 1_000_000
MAX_TOTAL_FRAMES = 10_000_000
MAX_TICKS = 10_000_000_000
MAX_TOTAL_TICKS = 100_000_000_000
MAX_WALL_TIME_MS = 600_000
ROOT_FIELDS = {"schema", "id", "profile", "fixture", "limits", "checkpoint"}
PROFILE_FIELDS = {"package", "version_code", "so_sha256", "abi"}
FIXTURE_FIELDS = {"id", "kind", "required"}
LIMIT_FIELDS = {
    "startup_frames",
    "startup_ticks",
    "startup_wall_time_ms",
    "total_frames",
    "total_ticks",
    "total_wall_time_ms",
}
CHECKPOINT_FIELDS = {
    "id",
    "provider",
    "max_frames",
    "max_ticks",
    "wall_time_ms",
    "evidence",
}
ABIS = {"armeabi", "armeabi-v7a"}
FIXTURE_KINDS = {"apk", "obb", "external"}
CHECKPOINT_PROVIDERS = {
    "frame",
    "lifecycle",
    "movie_request",
    "process_exit",
    "gpu",
    "hle",
    "filesystem",
    "audio",
    "guest_fault",
}
EVIDENCE_KINDS = {"frame", "state", "logs", "gpu_trace"}
ID_PATTERN = re.compile(r"[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)*\Z")
PACKAGE_PATTERN = re.compile(
    r"[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+\Z"
)
HASH_PATTERN = re.compile(r"[0-9a-f]{64}\Z")


class ScenarioError(ValueError):
    pass


def _table(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ScenarioError(f"{field} must be a table")
    return value


def _array(value: Any, field: str, maximum: int) -> list[Any]:
    if not isinstance(value, list) or not value:
        raise ScenarioError(f"{field} must be a non-empty array")
    if len(value) > maximum:
        raise ScenarioError(f"{field} must contain at most {maximum} items")
    return value


def _keys(table: dict[str, Any], field: str, allowed: set[str],
          required: set[str]) -> None:
    missing = sorted(required - table.keys())
    unknown = sorted(table.keys() - allowed)
    if missing:
        raise ScenarioError(f"{field} is missing fields: {', '.join(missing)}")
    if unknown:
        raise ScenarioError(f"{field} has unknown fields: {', '.join(unknown)}")


def _string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise ScenarioError(f"{field} must be a non-empty string")
    return value


def _identifier(value: Any, field: str) -> str:
    identifier = _string(value, field)
    if ID_PATTERN.fullmatch(identifier) is None:
        raise ScenarioError(f"{field} must be a normalized namespaced id")
    return identifier


def _integer(value: Any, field: str, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ScenarioError(f"{field} must be an integer")
    if value < 1 or value > maximum:
        raise ScenarioError(f"{field} must be in 1..{maximum}")
    return value


def _boolean(value: Any, field: str) -> bool:
    if not isinstance(value, bool):
        raise ScenarioError(f"{field} must be boolean")
    return value


def _profile_identities(profile_directory: Path) -> set[tuple[str, int, str, str]]:
    if not profile_directory.is_dir():
        raise ScenarioError(
            f"profile directory does not exist: {profile_directory}"
        )
    identities: set[tuple[str, int, str, str]] = set()
    for path in sorted(profile_directory.glob("*.profile.toml")):
        try:
            document = load_profile(path)
        except ProfileError as error:
            raise ScenarioError(f"invalid Title Profile {path}: {error}") from error
        identity = document["identity"]
        for version_code in identity["version_code"]:
            for digest in identity["so_sha256"]:
                key = (
                    identity["package"],
                    version_code,
                    digest,
                    identity["abi"],
                )
                if key in identities:
                    raise ScenarioError(
                        f"duplicate exact Profile identity in {profile_directory}"
                    )
                identities.add(key)
    return identities


def _validate_profile(value: Any,
                      profile_identities: set[tuple[str, int, str, str]]) -> None:
    profile = _table(value, "profile")
    _keys(profile, "profile", PROFILE_FIELDS, PROFILE_FIELDS)
    package = _string(profile["package"], "profile.package")
    if PACKAGE_PATTERN.fullmatch(package) is None:
        raise ScenarioError("profile.package is not a valid Android package name")
    version_code = _integer(
        profile["version_code"], "profile.version_code", 0xFFFFFFFF
    )
    digest = _string(profile["so_sha256"], "profile.so_sha256")
    if HASH_PATTERN.fullmatch(digest) is None:
        raise ScenarioError(
            "profile.so_sha256 must be 64 lowercase hex characters"
        )
    abi = _string(profile["abi"], "profile.abi")
    if abi not in ABIS:
        raise ScenarioError("profile.abi must be armeabi or armeabi-v7a")
    if (package, version_code, digest, abi) not in profile_identities:
        raise ScenarioError("profile does not match an exact Title Profile identity")


def _validate_fixtures(value: Any) -> None:
    fixtures = _array(value, "fixture", MAX_FIXTURES)
    ids: set[str] = set()
    has_required_apk = False
    for index, item in enumerate(fixtures):
        field = f"fixture[{index}]"
        fixture = _table(item, field)
        _keys(fixture, field, FIXTURE_FIELDS, FIXTURE_FIELDS)
        fixture_id = _identifier(fixture["id"], f"{field}.id")
        if fixture_id in ids:
            raise ScenarioError("fixture ids must be unique")
        ids.add(fixture_id)
        kind = _string(fixture["kind"], f"{field}.kind")
        if kind not in FIXTURE_KINDS:
            raise ScenarioError(f"{field}.kind is unsupported")
        required = _boolean(fixture["required"], f"{field}.required")
        has_required_apk = has_required_apk or kind == "apk" and required
    if not has_required_apk:
        raise ScenarioError("fixture must contain at least one required apk")


def _validate_limits(value: Any) -> dict[str, int]:
    limits = _table(value, "limits")
    _keys(limits, "limits", LIMIT_FIELDS, LIMIT_FIELDS)
    result = {
        "startup_frames": _integer(
            limits["startup_frames"], "limits.startup_frames", MAX_FRAMES
        ),
        "startup_ticks": _integer(
            limits["startup_ticks"], "limits.startup_ticks", MAX_TICKS
        ),
        "startup_wall_time_ms": _integer(
            limits["startup_wall_time_ms"],
            "limits.startup_wall_time_ms",
            MAX_WALL_TIME_MS,
        ),
        "total_frames": _integer(
            limits["total_frames"], "limits.total_frames", MAX_TOTAL_FRAMES
        ),
        "total_ticks": _integer(
            limits["total_ticks"], "limits.total_ticks", MAX_TOTAL_TICKS
        ),
        "total_wall_time_ms": _integer(
            limits["total_wall_time_ms"],
            "limits.total_wall_time_ms",
            MAX_WALL_TIME_MS,
        ),
    }
    if result["startup_frames"] > result["total_frames"]:
        raise ScenarioError("startup frame budget exceeds total frame budget")
    if result["startup_ticks"] > result["total_ticks"]:
        raise ScenarioError("startup tick budget exceeds total tick budget")
    if result["startup_wall_time_ms"] > result["total_wall_time_ms"]:
        raise ScenarioError("startup wall-time budget exceeds total wall-time budget")
    return result


def _validate_checkpoints(value: Any, limits: dict[str, int]) -> None:
    checkpoints = _array(value, "checkpoint", MAX_CHECKPOINTS)
    ids: set[str] = set()
    used_frames = limits["startup_frames"]
    used_ticks = limits["startup_ticks"]
    used_wall_time = limits["startup_wall_time_ms"]
    for index, item in enumerate(checkpoints):
        field = f"checkpoint[{index}]"
        checkpoint = _table(item, field)
        _keys(checkpoint, field, CHECKPOINT_FIELDS, CHECKPOINT_FIELDS)
        checkpoint_id = _identifier(checkpoint["id"], f"{field}.id")
        if checkpoint_id in ids:
            raise ScenarioError("checkpoint ids must be unique")
        ids.add(checkpoint_id)
        provider = _string(checkpoint["provider"], f"{field}.provider")
        if provider not in CHECKPOINT_PROVIDERS:
            raise ScenarioError(f"{field}.provider is unsupported")
        used_frames += _integer(
            checkpoint["max_frames"], f"{field}.max_frames", MAX_FRAMES
        )
        used_ticks += _integer(
            checkpoint["max_ticks"], f"{field}.max_ticks", MAX_TICKS
        )
        used_wall_time += _integer(
            checkpoint["wall_time_ms"],
            f"{field}.wall_time_ms",
            MAX_WALL_TIME_MS,
        )
        evidence = _array(checkpoint["evidence"], f"{field}.evidence", 4)
        evidence_values: set[str] = set()
        for evidence_index, evidence_value in enumerate(evidence):
            evidence_field = f"{field}.evidence[{evidence_index}]"
            kind = _string(evidence_value, evidence_field)
            if kind not in EVIDENCE_KINDS:
                raise ScenarioError(f"{evidence_field} is unsupported")
            if kind in evidence_values:
                raise ScenarioError(f"{field}.evidence must not contain duplicates")
            evidence_values.add(kind)
    if used_frames > limits["total_frames"]:
        raise ScenarioError("checkpoint frame budgets exceed total frame budget")
    if used_ticks > limits["total_ticks"]:
        raise ScenarioError("checkpoint tick budgets exceed total tick budget")
    if used_wall_time > limits["total_wall_time_ms"]:
        raise ScenarioError("checkpoint wall-time budgets exceed total budget")


def validate_scenario(
    document: Any,
    expected_id: str,
    profile_identities: set[tuple[str, int, str, str]],
) -> dict[str, Any]:
    scenario = _table(document, "scenario")
    _keys(scenario, "scenario", ROOT_FIELDS, ROOT_FIELDS)
    if scenario["schema"] != 1:
        raise ScenarioError("schema must be 1")
    scenario_id = _identifier(scenario["id"], "id")
    if scenario_id != expected_id:
        raise ScenarioError(
            f"id {scenario_id!r} does not match filename {expected_id!r}"
        )
    _validate_profile(scenario["profile"], profile_identities)
    _validate_fixtures(scenario["fixture"])
    limits = _validate_limits(scenario["limits"])
    _validate_checkpoints(scenario["checkpoint"], limits)
    return scenario


def validate_schema(path: Path) -> dict[str, Any]:
    try:
        schema = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ScenarioError(f"cannot read schema {path}: {error}") from error
    if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        raise ScenarioError("scenario schema must use JSON Schema draft 2020-12")
    if schema.get("type") != "object" or schema.get("additionalProperties") is not False:
        raise ScenarioError("scenario schema root must be a closed object")
    if set(schema.get("properties", {})) != ROOT_FIELDS:
        raise ScenarioError("scenario schema root fields do not match the validator")
    if set(schema.get("required", [])) != ROOT_FIELDS:
        raise ScenarioError("scenario schema required fields do not match the validator")
    if schema["properties"]["schema"].get("const") != 1:
        raise ScenarioError("scenario schema version must be 1")
    definitions = schema.get("$defs", {})

    def closed_definition(name: str, fields: set[str]) -> dict[str, Any]:
        definition = definitions.get(name, {})
        if (definition.get("type") != "object" or
                definition.get("additionalProperties") is not False or
                set(definition.get("properties", {})) != fields or
                set(definition.get("required", [])) != fields):
            raise ScenarioError(
                f"scenario schema {name} fields do not match the validator"
            )
        return definition

    profile_definition = closed_definition("profile", PROFILE_FIELDS)
    fixture_definition = closed_definition("fixture", FIXTURE_FIELDS)
    limits_definition = closed_definition("limits", LIMIT_FIELDS)
    checkpoint_definition = closed_definition("checkpoint", CHECKPOINT_FIELDS)
    if schema["properties"]["fixture"].get("maxItems") != MAX_FIXTURES:
        raise ScenarioError("scenario schema fixture limit does not match the validator")
    if schema["properties"]["checkpoint"].get("maxItems") != MAX_CHECKPOINTS:
        raise ScenarioError(
            "scenario schema checkpoint limit does not match the validator"
        )

    numeric_limits = {
        "startup_frames": MAX_FRAMES,
        "startup_ticks": MAX_TICKS,
        "startup_wall_time_ms": MAX_WALL_TIME_MS,
        "total_frames": MAX_TOTAL_FRAMES,
        "total_ticks": MAX_TOTAL_TICKS,
        "total_wall_time_ms": MAX_WALL_TIME_MS,
    }
    for field, maximum in numeric_limits.items():
        definition = limits_definition["properties"][field]
        if definition.get("minimum") != 1 or definition.get("maximum") != maximum:
            raise ScenarioError(
                f"scenario schema {field} limit does not match the validator"
            )
    checkpoint_limits = {
        "max_frames": MAX_FRAMES,
        "max_ticks": MAX_TICKS,
        "wall_time_ms": MAX_WALL_TIME_MS,
    }
    for field, maximum in checkpoint_limits.items():
        definition = checkpoint_definition["properties"][field]
        if definition.get("minimum") != 1 or definition.get("maximum") != maximum:
            raise ScenarioError(
                f"scenario schema checkpoint {field} limit does not match the validator"
            )

    profile_abis = profile_definition["properties"]["abi"].get("enum", [])
    fixture_kinds = fixture_definition["properties"]["kind"].get("enum", [])
    checkpoint_providers = checkpoint_definition["properties"]["provider"].get(
        "enum", []
    )
    evidence_kinds = checkpoint_definition["properties"]["evidence"].get(
        "items", {}
    ).get("enum", [])
    if set(profile_abis) != ABIS:
        raise ScenarioError("scenario schema ABI values do not match the validator")
    if set(fixture_kinds) != FIXTURE_KINDS:
        raise ScenarioError("scenario schema fixture kinds do not match the validator")
    if set(checkpoint_providers) != CHECKPOINT_PROVIDERS:
        raise ScenarioError(
            "scenario schema checkpoint providers do not match the validator"
        )
    if set(evidence_kinds) != EVIDENCE_KINDS:
        raise ScenarioError(
            "scenario schema evidence kinds do not match the validator"
        )
    return schema


def load_scenario(
    path: Path,
    profile_identities: set[tuple[str, int, str, str]],
) -> dict[str, Any]:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise ScenarioError(f"cannot read {path}: {error}") from error
    if len(text.splitlines()) > MAX_SCENARIO_LINES:
        raise ScenarioError(f"{path} exceeds {MAX_SCENARIO_LINES} lines")
    suffix = ".scenario.toml"
    if not path.name.endswith(suffix):
        raise ScenarioError(f"{path.name} must end in {suffix}")
    expected_id = path.name[:-len(suffix)]
    try:
        document = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        raise ScenarioError(f"invalid TOML in {path}: {error}") from error
    return validate_scenario(document, expected_id, profile_identities)


def validate_directory(scenario_directory: Path, profile_directory: Path) -> int:
    if not scenario_directory.is_dir():
        raise ScenarioError(
            f"scenario directory does not exist: {scenario_directory}"
        )
    profile_identities = _profile_identities(profile_directory)
    scenarios = sorted(scenario_directory.glob("*.scenario.toml"))
    seen_ids: set[str] = set()
    for path in scenarios:
        document = load_scenario(path, profile_identities)
        if document["id"] in seen_ids:
            raise ScenarioError(f"duplicate scenario id {document['id']!r}")
        seen_ids.add(document["id"])
    return len(scenarios)


def _profile_text() -> str:
    return f'''schema = 1

[identity]
package = "org.example.legacy"
version_code = [7]
so_sha256 = ["{'a' * 64}"]
abi = "armeabi-v7a"

[runtime]
api_level = 19
lifecycle = "gl_surface_view"
surface = {{ width = 800, height = 480 }}
'''


def _scenario_text() -> str:
    return f'''schema = 1
id = "legacy.boot"

[profile]
package = "org.example.legacy"
version_code = 7
so_sha256 = "{'a' * 64}"
abi = "armeabi-v7a"

[[fixture]]
id = "legacy.apk"
kind = "apk"
required = true

[limits]
startup_frames = 100
startup_ticks = 1000000
startup_wall_time_ms = 1000
total_frames = 300
total_ticks = 3000000
total_wall_time_ms = 3000

[[checkpoint]]
id = "first_frame"
provider = "frame"
max_frames = 100
max_ticks = 1000000
wall_time_ms = 1000
evidence = ["frame", "state"]

[[checkpoint]]
id = "movie_requested"
provider = "movie_request"
max_frames = 100
max_ticks = 1000000
wall_time_ms = 1000
evidence = ["state", "logs"]
'''


def self_test(schema_path: Path) -> int:
    validate_schema(schema_path)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        profiles = root / "profiles"
        scenarios = root / "scenarios"
        profiles.mkdir()
        scenarios.mkdir()
        (profiles / "org.example.legacy.profile.toml").write_text(
            _profile_text(), encoding="utf-8", newline="\n"
        )
        scenario_path = scenarios / "legacy.boot.scenario.toml"
        scenario_path.write_text(
            _scenario_text(), encoding="utf-8", newline="\n"
        )
        assert validate_directory(scenarios, profiles) == 1

        invalid_cases = {
            "filename mismatch": _scenario_text().replace(
                'id = "legacy.boot"', 'id = "legacy.other"'
            ),
            "unknown field": _scenario_text().replace(
                "schema = 1", 'schema = 1\nscript = "run-me"'
            ),
            "profile mismatch": _scenario_text().replace(
                "version_code = 7", "version_code = 8"
            ),
            "host path fixture": _scenario_text().replace(
                'id = "legacy.apk"', 'id = "/tmp/legacy.apk"'
            ),
            "missing required apk": _scenario_text().replace(
                'required = true', 'required = false', 1
            ),
            "duplicate fixture id": _scenario_text().replace(
                "[limits]",
                '''[[fixture]]
id = "legacy.apk"
kind = "external"
required = true

[limits]''',
            ),
            "zero checkpoint budget": _scenario_text().replace(
                "max_frames = 100", "max_frames = 0", 1
            ),
            "total budget exceeded": _scenario_text().replace(
                "total_frames = 300", "total_frames = 299"
            ),
            "duplicate checkpoint": _scenario_text().replace(
                'id = "movie_requested"', 'id = "first_frame"'
            ),
            "unknown provider": _scenario_text().replace(
                'provider = "movie_request"', 'provider = "ocr"'
            ),
            "duplicate evidence": _scenario_text().replace(
                'evidence = ["frame", "state"]',
                'evidence = ["frame", "frame"]',
            ),
            "empty checkpoint": (
                _scenario_text().split("[[checkpoint]]", 1)[0]
                + "checkpoint = []\n"
            ),
        }
        identities = _profile_identities(profiles)
        for label, content in invalid_cases.items():
            scenario_path.write_text(content, encoding="utf-8", newline="\n")
            try:
                load_scenario(scenario_path, identities)
                raise AssertionError(f"{label} was accepted")
            except ScenarioError:
                pass

        scenario_path.write_text(
            _scenario_text() + "\n" * MAX_SCENARIO_LINES,
            encoding="utf-8",
            newline="\n",
        )
        try:
            load_scenario(scenario_path, identities)
            raise AssertionError("oversized scenario was accepted")
        except ScenarioError:
            pass

        scenario_path.write_text("schema = [", encoding="utf-8", newline="\n")
        try:
            load_scenario(scenario_path, identities)
            raise AssertionError("malformed TOML was accepted")
        except ScenarioError:
            pass
    print("automation Scenario v1 validator self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--scenarios", type=Path)
    parser.add_argument("--profiles", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if not args.self_test and (args.scenarios is None or args.profiles is None):
        parser.error("--scenarios and --profiles are required unless --self-test is used")
    return args


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        if args.self_test:
            return self_test(args.schema)
        validate_schema(args.schema)
        count = validate_directory(args.scenarios, args.profiles)
        print(f"validated {count} automation Scenario v1 file(s)")
        return 0
    except ScenarioError as error:
        print(f"Scenario validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
