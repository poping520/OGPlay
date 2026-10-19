#!/usr/bin/env python3
"""Validate OGPlay Title Profile v4 TOML files without third-party packages."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import tempfile
import tomllib
from typing import Any, Sequence


MAX_PROFILE_LINES = 200
ROOT_FIELDS = {"schema", "identity", "quirks"}
PACKAGE_PATTERN = re.compile(
    r"[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+\Z"
)
HASH_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
ID_PATTERN = re.compile(r"[a-z][a-z0-9_]*\Z")


class ProfileError(ValueError):
    pass


def _table(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ProfileError(f"{field} must be a table")
    return value


def _array(value: Any, field: str, *, non_empty: bool = False) -> list[Any]:
    if not isinstance(value, list) or non_empty and not value:
        suffix = "a non-empty array" if non_empty else "an array"
        raise ProfileError(f"{field} must be {suffix}")
    return value


def _string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise ProfileError(f"{field} must be a non-empty string")
    return value


def _integer(value: Any, field: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise ProfileError(f"{field} must be an integer")
    if value < minimum or value > maximum:
        raise ProfileError(f"{field} must be in {minimum}..{maximum}")
    return value


def _keys(table: dict[str, Any], field: str, allowed: set[str],
          required: set[str] = frozenset()) -> None:
    missing = sorted(required - table.keys())
    unknown = sorted(table.keys() - allowed)
    if missing:
        raise ProfileError(f"{field} is missing fields: {', '.join(missing)}")
    if unknown:
        raise ProfileError(f"{field} has unknown fields: {', '.join(unknown)}")


def _unique(values: list[Any], field: str) -> None:
    if len(values) != len(set(values)):
        raise ProfileError(f"{field} must not contain duplicates")


def _validate_identity(value: Any, expected_package: str) -> None:
    table = _table(value, "identity")
    _keys(table, "identity", {"package", "version_code", "so_sha256"}, {"package"})
    package = _string(table["package"], "identity.package")
    if PACKAGE_PATTERN.fullmatch(package) is None:
        raise ProfileError("identity.package is not a valid Android package name")
    if package != expected_package:
        raise ProfileError(
            f"identity.package {package!r} does not match filename {expected_package!r}"
        )
    versions = _array(table.get("version_code", []), "identity.version_code",
                      non_empty="version_code" in table)
    for index, version in enumerate(versions):
        _integer(version, f"identity.version_code[{index}]", 1, 0xFFFFFFFF)
    _unique(versions, "identity.version_code")
    hashes = _array(table.get("so_sha256", []), "identity.so_sha256",
                    non_empty="so_sha256" in table)
    for index, digest in enumerate(hashes):
        if HASH_PATTERN.fullmatch(_string(
                digest, f"identity.so_sha256[{index}]")) is None:
            raise ProfileError(
                f"identity.so_sha256[{index}] must be 64 lowercase hex characters"
            )
    _unique(hashes, "identity.so_sha256")


def _validate_pure_data(value: Any, field: str, depth: int = 0) -> None:
    if depth > 4:
        raise ProfileError(f"{field} nesting exceeds four tables")
    if value is None or isinstance(value, (bool, int, float, str)):
        return
    if isinstance(value, list):
        for index, item in enumerate(value):
            _validate_pure_data(item, f"{field}[{index}]", depth + 1)
        return
    if isinstance(value, dict):
        for key, item in value.items():
            if ID_PATTERN.fullmatch(key) is None:
                raise ProfileError(f"{field} has invalid parameter name {key!r}")
            _validate_pure_data(item, f"{field}.{key}", depth + 1)
        return
    raise ProfileError(f"{field} contains a non-data TOML value")


def _validate_quirks(value: Any) -> None:
    table = _table(value, "quirks")
    if "enabled" not in table:
        raise ProfileError("quirks is missing fields: enabled")
    enabled = _array(table["enabled"], "quirks.enabled")
    for index, quirk in enumerate(enabled):
        if ID_PATTERN.fullmatch(_string(quirk, f"quirks.enabled[{index}]")) is None:
            raise ProfileError(f"quirks.enabled[{index}] must be a quirk id")
    _unique(enabled, "quirks.enabled")
    parameters = set(table) - {"enabled"}
    unknown = sorted(parameters - set(enabled))
    missing = sorted(set(enabled) - parameters)
    if unknown:
        raise ProfileError(
            "quirks has parameters for disabled quirks: " + ", ".join(unknown)
        )
    if missing:
        raise ProfileError(
            "quirks is missing parameter tables: " + ", ".join(missing)
        )
    for quirk in sorted(parameters):
        _validate_pure_data(_table(table[quirk], f"quirks.{quirk}"),
                            f"quirks.{quirk}")


def validate_profile(document: Any, expected_package: str) -> dict[str, Any]:
    root = _table(document, "profile")
    _keys(root, "profile", ROOT_FIELDS, {"schema", "identity"})
    _integer(root["schema"], "schema", 4, 4)
    _validate_identity(root["identity"], expected_package)
    for field, validator in {"quirks": _validate_quirks}.items():
        if field in root:
            validator(root[field])
    return root


def validate_schema(path: Path) -> dict[str, Any]:
    try:
        schema = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProfileError(f"cannot read schema {path}: {error}") from error
    if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        raise ProfileError("profile schema must use JSON Schema draft 2020-12")
    if schema.get("type") != "object" or schema.get("additionalProperties") is not False:
        raise ProfileError("profile schema root must be a closed object")
    if set(schema.get("properties", {})) != ROOT_FIELDS:
        raise ProfileError("profile schema fields do not match validator")
    if set(schema.get("required", [])) != {"schema", "identity"}:
        raise ProfileError("profile schema required fields do not match validator")
    if schema["properties"]["schema"].get("const") != 4:
        raise ProfileError("profile schema version must be 4")
    identity = schema["$defs"]["identity"]
    if set(identity["properties"]) != {"package", "version_code", "so_sha256"}:
        raise ProfileError("profile schema identity fields do not match validator")
    if set(identity["required"]) != {"package"}:
        raise ProfileError("profile schema applicability guards must be optional")
    return schema


def load_profile(path: Path) -> dict[str, Any]:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise ProfileError(f"cannot read {path}: {error}") from error
    if len(text.splitlines()) > MAX_PROFILE_LINES:
        raise ProfileError(f"{path} exceeds {MAX_PROFILE_LINES} lines")
    suffix = ".profile.toml"
    if not path.name.endswith(suffix):
        raise ProfileError(f"{path.name} must end in {suffix}")
    package = path.name[:-len(suffix)]
    try:
        document = tomllib.loads(text)
    except tomllib.TOMLDecodeError as error:
        raise ProfileError(f"invalid TOML in {path}: {error}") from error
    return validate_profile(document, package)


def validate_directory(path: Path) -> int:
    if not path.is_dir():
        raise ProfileError(f"profile directory does not exist: {path}")
    profiles = sorted(path.glob("*.profile.toml"))
    for profile in profiles:
        load_profile(profile)
    return len(profiles)


def _valid_profile(package: str = "org.example.game") -> str:
    return f'''schema = 4
[identity]
package = "{package}"
version_code = [1, 2]
so_sha256 = ["{'0' * 64}"]
[quirks]
enabled = ["legacy_reads"]
[quirks.legacy_reads]
range = ["0x1000", "0x2000"]
'''


def self_test(schema_path: Path) -> int:
    validate_schema(schema_path)
    document = tomllib.loads(_valid_profile())
    validate_profile(document, "org.example.game")
    validate_profile({"schema": 4, "identity": {"package": "org.example.game"}},
                     "org.example.game")
    cases = [_valid_profile("org.example.other"),
             _valid_profile() + "\n" * MAX_PROFILE_LINES,
             _valid_profile().replace("[1, 2]", "[1, 1]"),
             _valid_profile().replace("0" * 64, "G" * 64),
             _valid_profile().replace('enabled = ["legacy_reads"]', 'enabled = []')]
    cases += [_valid_profile().replace("schema = 4", f"schema = {v}") for v in (1, 2, 3, 5)]
    cases += [_valid_profile() + f"\n[{field}]\n" for field in ("runtime", "data", "audio", "java", "input")]
    cases += [_valid_profile().replace("[identity]", f'[identity]\n{field} = "x"')
              for field in ("name", "abi", "root_library")]
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "org.example.game.profile.toml"
        path.write_text(_valid_profile(), encoding="utf-8")
        load_profile(path)
        for content in cases:
            path.write_text(content, encoding="utf-8")
            try:
                load_profile(path)
            except ProfileError:
                continue
            raise AssertionError("invalid profile accepted")
    print("Title Profile v4 validator self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--profiles", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if not args.self_test and args.profiles is None:
        parser.error("--profiles is required unless --self-test is used")
    return args


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        if args.self_test:
            return self_test(args.schema)
        validate_schema(args.schema)
        count = validate_directory(args.profiles)
        print(f"validated {count} Title Profile file(s)")
        return 0
    except ProfileError as error:
        print(f"Title Profile validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
