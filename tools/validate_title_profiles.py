#!/usr/bin/env python3
"""Validate OGPlay Title Profile v2/v3 TOML files without third-party packages."""

from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import tomllib
from typing import Any, Sequence


MAX_PROFILE_LINES = 200
ROOT_FIELDS = {"schema", "identity", "runtime", "data", "audio", "quirks",
               "input"}
PACKAGE_PATTERN = re.compile(
    r"[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+\Z"
)
HASH_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
BINARY_CLASS_PATTERN = re.compile(
    r"[A-Za-z_$][A-Za-z0-9_$]*(?:\.[A-Za-z_$][A-Za-z0-9_$]*)+\Z"
)
JAVA_MEMBER_PATTERN = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*\Z")
ID_PATTERN = re.compile(r"[a-z][a-z0-9_]*\Z")
SOURCES = {"apk", "obb", "external"}
ABIS = {"armeabi", "armeabi-v7a"}


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


def _boolean(value: Any, field: str) -> bool:
    if not isinstance(value, bool):
        raise ProfileError(f"{field} must be boolean")
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


def _guest_path(value: Any, field: str) -> str:
    path = _string(value, field)
    parsed = PurePosixPath(path)
    if (not path.startswith("/") or "//" in path or str(parsed) != path or
            any(component in {".", ".."} for component in path.split("/"))):
        raise ProfileError(f"{field} must be a normalized absolute guest path")
    return path


def _relative_path(value: Any, field: str) -> str:
    path = _string(value, field)
    parsed = PurePosixPath(path)
    if (parsed.is_absolute() or "\\" in path or "//" in path or
            str(parsed) != path or
            any(component in {".", ".."} for component in path.split("/"))):
        raise ProfileError(f"{field} must be a normalized relative path")
    return path


def _validate_identity(value: Any, expected_package: str, schema: int) -> None:
    table = _table(value, "identity")
    if schema == 3:
        _keys(table, "identity",
              {"package", "name", "version_code", "so_sha256"},
              {"package"})
    else:
        _keys(table, "identity",
              {"package", "name", "version_code", "so_sha256", "abi"},
              {"package", "version_code", "so_sha256", "abi"})
    package = _string(table["package"], "identity.package")
    if PACKAGE_PATTERN.fullmatch(package) is None:
        raise ProfileError("identity.package is not a valid Android package name")
    if package != expected_package:
        raise ProfileError(
            f"identity.package {package!r} does not match filename {expected_package!r}"
        )
    if "name" in table:
        _string(table["name"], "identity.name")
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
    if schema != 3:
        abi = _string(table["abi"], "identity.abi")
        if abi not in ABIS:
            raise ProfileError("identity.abi must be armeabi or armeabi-v7a")


def _validate_data(value: Any) -> None:
    table = _table(value, "data")
    _keys(table, "data", {"mounts", "working_directory", "manifest"})
    for index, value in enumerate(_array(table.get("mounts", []), "data.mounts")):
        field = f"data.mounts[{index}]"
        mount = _table(value, field)
        _keys(mount, field, {"guest", "source", "required"},
              {"guest", "source", "required"})
        _guest_path(mount["guest"], f"{field}.guest")
        if mount["source"] not in SOURCES:
            raise ProfileError(f"{field}.source must be apk, obb or external")
        _boolean(mount["required"], f"{field}.required")
    if "working_directory" in table:
        _guest_path(table["working_directory"], "data.working_directory")
    for index, value in enumerate(_array(table.get("manifest", []), "data.manifest")):
        field = f"data.manifest[{index}]"
        item = _table(value, field)
        _keys(item, field, {"path", "required"}, {"path", "required"})
        _relative_path(item["path"], f"{field}.path")
        _boolean(item["required"], f"{field}.required")


def _validate_audio(value: Any) -> None:
    table = _table(value, "audio")
    _keys(table, "audio", {"cover_music", "sound_pool"})
    if "cover_music" in table:
        music = _table(table["cover_music"], "audio.cover_music")
        _keys(music, "audio.cover_music", {"source", "path", "loop"},
              {"source", "path", "loop"})
        if music["source"] not in SOURCES:
            raise ProfileError("audio.cover_music.source must be apk, obb or external")
        _relative_path(music["path"], "audio.cover_music.path")
        _boolean(music["loop"], "audio.cover_music.loop")
    if "sound_pool" in table:
        pool = _table(table["sound_pool"], "audio.sound_pool")
        _keys(pool, "audio.sound_pool", {"source", "path_pattern"},
              {"source", "path_pattern"})
        if pool["source"] not in SOURCES:
            raise ProfileError("audio.sound_pool.source must be apk, obb or external")
        _relative_path(pool["path_pattern"], "audio.sound_pool.path_pattern")
        pattern = pool["path_pattern"]
        matches = re.findall(r"\{resource(?::0[1-9])?\}", pattern)
        if len(matches) != 1 or re.sub(r"\{resource(?::0[1-9])?\}", "", pattern).find("{") >= 0 or "}" in re.sub(r"\{resource(?::0[1-9])?\}", "", pattern):
            raise ProfileError("audio.sound_pool.path_pattern must contain one valid resource placeholder")


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


def _validate_input(value: Any) -> None:
    table = _table(value, "input")
    _keys(table, "input", {"profile"}, {"profile"})
    profile = _string(table["profile"], "input.profile")
    if ID_PATTERN.fullmatch(profile) is None:
        raise ProfileError("input.profile must be a template id")


def _validate_runtime(value: Any, schema: int) -> None:
    # dex_activity runtime with checked budgets
    # (docs/design/dexvm/04-integration.md §7).
    table = _table(value, "runtime")
    common = {"api_level", "maximum_ticks_per_call", "surface", "dexvm",
              "entry", "presets"}
    if schema == 3:
        _keys(table, "runtime", common, {"api_level"})
    else:
        _keys(table, "runtime", common | {"lifecycle"},
              {"api_level", "lifecycle", "surface"})
    api = _integer(table["api_level"], "runtime.api_level", 1, 0xFFFFFFFF)
    if api not in {19, 22, 23}:
        raise ProfileError("runtime.api_level must be one of 19, 22 or 23")
    if schema != 3 and _string(
            table["lifecycle"], "runtime.lifecycle") != "dex_activity":
        raise ProfileError("schema 2 requires lifecycle = dex_activity")
    if "maximum_ticks_per_call" in table:
        _integer(table["maximum_ticks_per_call"],
                 "runtime.maximum_ticks_per_call", 1, 10_000_000_000)
    if "surface" in table:
        surface = _table(table["surface"], "runtime.surface")
        _keys(surface, "runtime.surface", {"width", "height"},
              {"width", "height"})
        _integer(surface["width"], "runtime.surface.width", 1, 16384)
        _integer(surface["height"], "runtime.surface.height", 1, 16384)
    if "dexvm" in table:
        dexvm = _table(table["dexvm"], "runtime.dexvm")
        _keys(dexvm, "runtime.dexvm",
              {"heap_budget_bytes", "max_frames", "ticks_per_call"}, set())
        if "heap_budget_bytes" in dexvm:
            _integer(dexvm["heap_budget_bytes"],
                     "runtime.dexvm.heap_budget_bytes", 1 << 20, 1 << 30)
        if "max_frames" in dexvm:
            _integer(dexvm["max_frames"], "runtime.dexvm.max_frames", 16,
                     65536)
        if "ticks_per_call" in dexvm:
            _integer(dexvm["ticks_per_call"], "runtime.dexvm.ticks_per_call",
                     1, 10_000_000_000)
    if "entry" in table:
        entry = _table(table["entry"], "runtime.entry")
        _keys(entry, "runtime.entry", {"launch_activity"},
              {"launch_activity"})
        activity = _string(entry["launch_activity"],
                           "runtime.entry.launch_activity")
        if BINARY_CLASS_PATTERN.fullmatch(activity) is None:
            raise ProfileError(
                "runtime.entry.launch_activity must be a binary Java class name"
            )
    presets = _array(table.get("presets", []), "runtime.presets")
    if "presets" in table and not presets:
        raise ProfileError("runtime.presets must not be empty")
    keys: list[tuple[str, str]] = []
    for index, value in enumerate(presets):
        field = f"runtime.presets[{index}]"
        preset = _table(value, field)
        required = {"class", "field", "type", "value", "reason"}
        _keys(preset, field, required, required)
        owner = _string(preset["class"], f"{field}.class")
        member = _string(preset["field"], f"{field}.field")
        descriptor = _string(preset["type"], f"{field}.type")
        _string(preset["reason"], f"{field}.reason")
        if BINARY_CLASS_PATTERN.fullmatch(owner) is None:
            raise ProfileError(f"{field}.class must be a binary Java class name")
        if JAVA_MEMBER_PATTERN.fullmatch(member) is None:
            raise ProfileError(f"{field}.field must be a Java member name")
        keys.append((owner, member))
        preset_value = preset["value"]
        if descriptor == "Z":
            _boolean(preset_value, f"{field}.value")
        elif descriptor in {"B", "C", "S", "I", "J"}:
            limits = {
                "B": (-128, 127), "C": (0, 65535),
                "S": (-32768, 32767), "I": (-0x80000000, 0x7fffffff),
                "J": (-0x8000000000000000, 0x7fffffffffffffff),
            }
            _integer(preset_value, f"{field}.value", *limits[descriptor])
        elif descriptor in {"F", "D"}:
            if isinstance(preset_value, bool) or not isinstance(
                    preset_value, (int, float)):
                raise ProfileError(f"{field}.value must be numeric")
        elif descriptor == "Ljava/lang/String;":
            _string(preset_value, f"{field}.value")
        else:
            raise ProfileError(
                f"{field}.type must be primitive or java.lang.String"
            )
    _unique(keys, "runtime.presets class/field pairs")


def validate_profile(document: Any, expected_package: str) -> dict[str, Any]:
    root = _table(document, "profile")
    _keys(root, "profile", ROOT_FIELDS, {"schema", "identity", "runtime"})
    schema = root["schema"]
    if schema not in {2, 3}:
        raise ProfileError("schema must be 2 or 3")
    _validate_identity(root["identity"], expected_package, schema)
    _validate_runtime(root["runtime"], schema)
    validators = {
        "data": _validate_data,
        "audio": _validate_audio,
        "quirks": _validate_quirks,
        "input": _validate_input,
    }
    for field, validator in validators.items():
        if field in root:
            validator(root[field])
    scoped = "entry" in root["runtime"] or "presets" in root["runtime"]
    required_manifest = any(
        item.get("required") is True
        for item in root.get("data", {}).get("manifest", [])
    )
    if scoped and not required_manifest:
        raise ProfileError(
            "runtime entry scope requires a required data manifest fact"
        )
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
        raise ProfileError("profile schema root fields do not match the validator")
    if set(schema.get("required", [])) != {"schema", "identity", "runtime"}:
        raise ProfileError("profile schema required fields do not match the validator")
    version = schema["properties"]["schema"].get("const")
    if version not in {2, 3}:
        raise ProfileError("profile schema version must be 2 or 3")
    identity = schema.get("$defs", {}).get("identity", {})
    identity_properties = identity.get("properties", {})
    if version == 2:
        schema_abis = identity_properties.get("abi", {}).get("enum", [])
        if set(schema_abis) != ABIS:
            raise ProfileError(
                "profile schema ABI values do not match the validator")
    else:
        if "abi" in identity_properties:
            raise ProfileError("profile schema v3 must not expose ABI")
        if set(identity.get("required", [])) != {"package"}:
            raise ProfileError("profile schema v3 identity guard is not optional")
        runtime = schema.get("$defs", {}).get("runtime", {})
        runtime_properties = runtime.get("properties", {})
        if "lifecycle" in runtime_properties or "root_library" in runtime_properties:
            raise ProfileError(
                "profile schema v3 must not expose lifecycle or root library")
        if set(runtime.get("required", [])) != {"api_level"}:
            raise ProfileError("profile schema v3 runtime defaults do not match")
    return schema


def validate_schema_family(schema_path: Path) -> None:
    validate_schema(schema_path)
    sibling = schema_path.with_name("title-profile-v3.schema.json")
    if sibling != schema_path:
        validate_schema(sibling)


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
    return f"""schema = 2

[identity]
package = "{package}"
name = "Generic DexVM Fixture"
version_code = [1, 2]
so_sha256 = ["{'0' * 64}", "{'1' * 64}"]
abi = "armeabi-v7a"

[runtime]
api_level = 19
lifecycle = "dex_activity"
surface = {{ width = 1280, height = 720 }}
[runtime.entry]
launch_activity = "org.example.game.MainActivity"
[[runtime.presets]]
class = "org.example.game.InstallState"
field = "ready"
type = "Z"
value = true
reason = "fixture data is provisioned"

[data]
mounts = [{{ guest = "/sdcard/game", source = "external", required = true }}]
working_directory = "/sdcard/game"
manifest = [{{ path = "files/archive.dat", required = true }}]

[audio]
cover_music = {{ source = "apk", path = "res/raw/music.ogg", loop = true }}

[quirks]
enabled = ["legacy_reads"]

[quirks.legacy_reads]
range = ["0x1000", "0x2000"]

[input]
profile = "generic_touch"
"""


def _valid_v3_profile(package: str = "org.example.game") -> str:
    return f"""schema = 3

[identity]
package = "{package}"
name = "Optional Compatibility Fixture"

[runtime]
api_level = 19
"""


def self_test(schema_path: Path) -> int:
    validate_schema_family(schema_path)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        valid = root / "org.example.game.profile.toml"
        valid.write_text(_valid_profile(), encoding="utf-8", newline="\n")
        document = load_profile(valid)
        assert document["runtime"]["surface"]["width"] == 1280
        budgeted = _valid_profile().replace(
            'lifecycle = "dex_activity"',
            'lifecycle = "dex_activity"\nmaximum_ticks_per_call = 10000000000'
        )
        valid.write_text(budgeted, encoding="utf-8", newline="\n")
        assert load_profile(valid)["runtime"]["maximum_ticks_per_call"] == 10_000_000_000
        valid.write_text(_valid_profile().replace(
            'abi = "armeabi-v7a"', 'abi = "armeabi"'
        ), encoding="utf-8", newline="\n")
        assert load_profile(valid)["identity"]["abi"] == "armeabi"
        valid.write_text(_valid_v3_profile(), encoding="utf-8", newline="\n")
        assert "so_sha256" not in load_profile(valid)["identity"]

        v3_cases = {
            "v3 forced ABI": _valid_v3_profile().replace(
                'name = "Optional Compatibility Fixture"',
                'name = "Optional Compatibility Fixture"\nabi = "armeabi"'
            ),
            "v3 root library": _valid_v3_profile().replace(
                "api_level = 19", 'api_level = 19\nroot_library = "libgame.so"'
            ),
            "v3 lifecycle": _valid_v3_profile().replace(
                "api_level = 19", 'api_level = 19\nlifecycle = "dex_activity"'
            ),
        }
        for label, content in v3_cases.items():
            valid.write_text(content, encoding="utf-8", newline="\n")
            try:
                load_profile(valid)
                raise AssertionError(f"{label} was accepted")
            except ProfileError:
                pass

        cases = {
            "filename mismatch": _valid_profile("org.example.other"),
            "schema v1": _valid_profile().replace("schema = 2", "schema = 1"),
            "unknown field": _valid_profile().replace(
                "schema = 2", "schema = 2\nscript = \"run-me\""
            ),
            "bad lifecycle": _valid_profile().replace(
                'lifecycle = "dex_activity"', 'lifecycle = "per_game_loop"'
            ),
            "unbounded call budget": _valid_profile().replace(
                'lifecycle = "dex_activity"',
                'lifecycle = "dex_activity"\nmaximum_ticks_per_call = 10000000001'
            ),
            "zero call budget": _valid_profile().replace(
                'lifecycle = "dex_activity"',
                'lifecycle = "dex_activity"\nmaximum_ticks_per_call = 0'
            ),
            "text call budget": _valid_profile().replace(
                'lifecycle = "dex_activity"',
                'lifecycle = "dex_activity"\nmaximum_ticks_per_call = "10000000000"'
            ),
            "unsupported ABI": _valid_profile().replace(
                'abi = "armeabi-v7a"', 'abi = "x86"'
            ),
            "duplicate hash": _valid_profile().replace(
                f'"{"1" * 64}"', f'"{"0" * 64}"'
            ),
            "unsafe path": _valid_profile().replace(
                'guest = "/sdcard/game"', 'guest = "/sdcard/../game"'
            ),
            "non-normalized path": _valid_profile().replace(
                'path = "files/archive.dat"', 'path = "files/./archive.dat"'
            ),
            "disabled quirk parameters": _valid_profile().replace(
                'enabled = ["legacy_reads"]', "enabled = []"
            ),
            "reference preset": _valid_profile().replace(
                'type = "Z"', 'type = "Ljava/lang/Object;"'
            ),
            "empty reason": _valid_profile().replace(
                'reason = "fixture data is provisioned"', 'reason = ""'
            ),
            "preset mismatch": _valid_profile().replace(
                "value = true", "value = 1"
            ),
        }
        for label, content in cases.items():
            candidate = root / "org.example.game.profile.toml"
            candidate.write_text(content, encoding="utf-8", newline="\n")
            try:
                load_profile(candidate)
                raise AssertionError(f"{label} was accepted")
            except ProfileError:
                pass

        oversized = root / "org.example.game.profile.toml"
        oversized.write_text(_valid_profile() + "\n" * MAX_PROFILE_LINES,
                             encoding="utf-8", newline="\n")
        try:
            load_profile(oversized)
            raise AssertionError("oversized profile was accepted")
        except ProfileError:
            pass
    print("Title Profile validator self-test passed")
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
        validate_schema_family(args.schema)
        count = validate_directory(args.profiles)
        print(f"validated {count} Title Profile file(s)")
        return 0
    except ProfileError as error:
        print(f"Title Profile validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
