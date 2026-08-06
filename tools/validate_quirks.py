#!/usr/bin/env python3
"""Validate the OGPlay quirk registry, test references, and profile uses."""

from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import tomllib
from typing import Any, Sequence


FIELDS = {"summary", "reason", "risk", "test", "owner"}
ID_PATTERN = re.compile(r"[a-z][a-z0-9_]*\Z")
OWNER_PATTERN = re.compile(r"[a-z][a-z0-9_]*(?:/[a-z][a-z0-9_]*)+\Z")


class RegistryError(ValueError):
    pass


def _read_toml(path: Path) -> dict[str, Any]:
    try:
        document = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, tomllib.TOMLDecodeError) as error:
        raise RegistryError(f"cannot read {path}: {error}") from error
    if not isinstance(document, dict):
        raise RegistryError(f"{path} root must be a table")
    return document


def _non_empty_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise RegistryError(f"{field} must be a non-empty string")
    return value


def _test_reference(value: Any, field: str, root: Path) -> str:
    reference = _non_empty_string(value, field)
    path_text, separator, case_name = reference.partition(":")
    if not separator or not case_name.strip():
        raise RegistryError(f"{field} must be tests/<file>.cpp:<case-name>")
    relative = PurePosixPath(path_text)
    if (relative.is_absolute() or "\\" in path_text or
            relative.parts[:1] != ("tests",) or
            any(part in {"", ".", ".."} for part in relative.parts) or
            relative.suffix != ".cpp"):
        raise RegistryError(f"{field} must point to a normalized tests/*.cpp path")
    source_path = root.joinpath(*relative.parts)
    try:
        source = source_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise RegistryError(f"{field} cannot read {source_path}: {error}") from error
    if case_name not in source:
        raise RegistryError(f"{field} case name is absent from {source_path}")
    return reference


def validate_registry(document: Any, root: Path) -> dict[str, dict[str, str]]:
    if not isinstance(document, dict) or document.get("schema") != 1:
        raise RegistryError("quirk registry schema must be 1")
    definitions: dict[str, dict[str, str]] = {}
    for quirk_id, value in document.items():
        if quirk_id == "schema":
            continue
        if ID_PATTERN.fullmatch(quirk_id) is None:
            raise RegistryError(f"invalid quirk id {quirk_id!r}")
        if not isinstance(value, dict):
            raise RegistryError(f"{quirk_id} must be a table")
        missing, unknown = sorted(FIELDS - value.keys()), sorted(value.keys() - FIELDS)
        if missing:
            raise RegistryError(f"{quirk_id} is missing fields: {', '.join(missing)}")
        if unknown:
            raise RegistryError(f"{quirk_id} has unknown fields: {', '.join(unknown)}")
        definition = {
            "summary": _non_empty_string(value["summary"], f"{quirk_id}.summary"),
            "reason": _non_empty_string(value["reason"], f"{quirk_id}.reason"),
            "risk": _non_empty_string(value["risk"], f"{quirk_id}.risk"),
            "test": _test_reference(value["test"], f"{quirk_id}.test", root),
            "owner": _non_empty_string(value["owner"], f"{quirk_id}.owner"),
        }
        if OWNER_PATTERN.fullmatch(definition["owner"]) is None:
            raise RegistryError(f"{quirk_id}.owner must be a module path")
        definitions[quirk_id] = definition
    return definitions


def load_registry(path: Path, root: Path) -> dict[str, dict[str, str]]:
    return validate_registry(_read_toml(path), root)


def validate_profiles(profile_directory: Path,
                      definitions: dict[str, dict[str, str]]) -> int:
    if not profile_directory.is_dir():
        raise RegistryError(f"profile directory does not exist: {profile_directory}")
    count = 0
    for path in sorted(profile_directory.glob("*.profile.toml")):
        document = _read_toml(path)
        quirks = document.get("quirks")
        if quirks is None:
            continue
        if not isinstance(quirks, dict) or not isinstance(quirks.get("enabled"), list):
            raise RegistryError(f"{path} quirks.enabled must be an array")
        for index, quirk_id in enumerate(quirks["enabled"]):
            if not isinstance(quirk_id, str) or quirk_id not in definitions:
                raise RegistryError(
                    f"{path} quirks.enabled[{index}] is not registered: {quirk_id!r}"
                )
            count += 1
    return count


def _registry_text(test_path: str = "tests/quirk_tests.cpp",
                   case_name: str = "legacy_reads required when disabled",
                   owner: str = "runtime/memory") -> str:
    return f'''schema = 1

[legacy_reads]
summary = "Allow one historical read behavior"
reason = """
The fixture deliberately requires behavior that is unsafe as a global default.
The referenced test executes both enabled and disabled paths.
"""
risk = "May hide an invalid low-address read"
test = "{test_path}:{case_name}"
owner = "{owner}"
'''


def _profile_text(quirk_id: str = "legacy_reads") -> str:
    return f'''schema = 1
[identity]
package = "org.example.legacy"
version_code = [1]
so_sha256 = ["{'0' * 64}"]
abi = "armeabi-v7a"
[runtime]
api_level = 19
lifecycle = "native_activity"
surface = {{ width = 1, height = 1 }}
[quirks]
enabled = ["{quirk_id}"]
[quirks.{quirk_id}]
range = ["0x1000", "0x2000"]
'''


def self_test() -> int:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        tests = root / "tests"
        profiles = root / "data" / "profiles"
        tests.mkdir(parents=True)
        profiles.mkdir(parents=True)
        (tests / "quirk_tests.cpp").write_text(
            'TEST_CASE("legacy_reads required when disabled") {}',
            encoding="utf-8",
        )
        registry_path = root / "data" / "quirks.toml"
        registry_path.write_text(_registry_text(), encoding="utf-8")
        (profiles / "org.example.legacy.profile.toml").write_text(
            _profile_text(), encoding="utf-8"
        )
        definitions = load_registry(registry_path, root)
        assert set(definitions) == {"legacy_reads"}
        assert validate_profiles(profiles, definitions) == 1

        invalid_cases = {
            "missing field": _registry_text().replace(
                'risk = "May hide an invalid low-address read"\n', ""
            ),
            "missing test": _registry_text(case_name="absent case"),
            "unsafe path": _registry_text(test_path="../quirk_tests.cpp"),
            "invalid owner": _registry_text(owner="runtime"),
        }
        for label, content in invalid_cases.items():
            registry_path.write_text(content, encoding="utf-8")
            try:
                load_registry(registry_path, root)
                raise AssertionError(f"{label} was accepted")
            except RegistryError:
                pass

        registry_path.write_text(_registry_text(), encoding="utf-8")
        (profiles / "org.example.legacy.profile.toml").write_text(
            _profile_text("unregistered"), encoding="utf-8"
        )
        try:
            validate_profiles(profiles, load_registry(registry_path, root))
            raise AssertionError("unregistered profile quirk was accepted")
        except RegistryError:
            pass
    print("quirk registry validator self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--registry", type=Path)
    parser.add_argument("--profiles", type=Path)
    parser.add_argument("--root", type=Path)
    args = parser.parse_args(argv)
    if not args.self_test and any(
            value is None for value in (args.registry, args.profiles, args.root)):
        parser.error("--registry, --profiles and --root are required")
    return args


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        if args.self_test:
            return self_test()
        definitions = load_registry(args.registry, args.root)
        uses = validate_profiles(args.profiles, definitions)
        print(f"validated {len(definitions)} quirk definition(s) and {uses} profile use(s)")
        return 0
    except RegistryError as error:
        print(f"quirk registry validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
