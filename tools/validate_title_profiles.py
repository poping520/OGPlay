#!/usr/bin/env python3
"""Validate OGPlay Title Profile v1 TOML files without third-party packages."""

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
ROOT_FIELDS = {"schema", "identity", "runtime", "data", "audio", "java",
               "quirks", "input"}
PACKAGE_PATTERN = re.compile(
    r"[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+\Z"
)
HASH_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
JAVA_CLASS_PATTERN = re.compile(
    r"[A-Za-z_$][A-Za-z0-9_$]*(?:/[A-Za-z_$][A-Za-z0-9_$]*)+\Z"
)
IMPL_PATTERN = re.compile(r"[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+\Z")
ID_PATTERN = re.compile(r"[a-z][a-z0-9_]*\Z")
SOURCES = {"apk", "obb", "external"}
LIFECYCLES = {"native_activity", "gl_surface_view", "custom_jni"}
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


def _validate_identity(value: Any, expected_package: str) -> None:
    table = _table(value, "identity")
    _keys(table, "identity", {"package", "name", "version_code", "so_sha256", "abi"},
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
    versions = _array(table["version_code"], "identity.version_code", non_empty=True)
    for index, version in enumerate(versions):
        _integer(version, f"identity.version_code[{index}]", 1, 0xFFFFFFFF)
    _unique(versions, "identity.version_code")
    hashes = _array(table["so_sha256"], "identity.so_sha256", non_empty=True)
    for index, digest in enumerate(hashes):
        if HASH_PATTERN.fullmatch(_string(
                digest, f"identity.so_sha256[{index}]")) is None:
            raise ProfileError(
                f"identity.so_sha256[{index}] must be 64 lowercase hex characters"
            )
    _unique(hashes, "identity.so_sha256")
    abi = _string(table["abi"], "identity.abi")
    if abi not in ABIS:
        raise ProfileError("identity.abi must be armeabi or armeabi-v7a")


def _validate_runtime(value: Any) -> None:
    table = _table(value, "runtime")
    _keys(table, "runtime", {"api_level", "lifecycle", "surface"},
          {"api_level", "lifecycle", "surface"})
    api = _integer(table["api_level"], "runtime.api_level", 1, 0xFFFFFFFF)
    if api not in {19, 22, 23}:
        raise ProfileError("runtime.api_level must be one of 19, 22 or 23")
    lifecycle = _string(table["lifecycle"], "runtime.lifecycle")
    if lifecycle not in LIFECYCLES:
        raise ProfileError(
            "runtime.lifecycle must be native_activity, gl_surface_view or custom_jni"
        )
    surface = _table(table["surface"], "runtime.surface")
    _keys(surface, "runtime.surface", {"width", "height"}, {"width", "height"})
    _integer(surface["width"], "runtime.surface.width", 1, 16384)
    _integer(surface["height"], "runtime.surface.height", 1, 16384)


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
    _keys(table, "audio", {"cover_music"})
    if "cover_music" not in table:
        return
    music = _table(table["cover_music"], "audio.cover_music")
    _keys(music, "audio.cover_music", {"source", "path", "loop"},
          {"source", "path", "loop"})
    if music["source"] not in SOURCES:
        raise ProfileError("audio.cover_music.source must be apk, obb or external")
    _relative_path(music["path"], "audio.cover_music.path")
    _boolean(music["loop"], "audio.cover_music.loop")


def _validate_java(value: Any) -> None:
    table = _table(value, "java")
    _keys(table, "java", {"class"}, {"class"})
    classes = _array(table["class"], "java.class", non_empty=True)
    class_names: list[str] = []
    for class_index, value in enumerate(classes):
        field = f"java.class[{class_index}]"
        java_class = _table(value, field)
        _keys(java_class, field, {"name", "method"}, {"name", "method"})
        name = _string(java_class["name"], f"{field}.name")
        if JAVA_CLASS_PATTERN.fullmatch(name) is None:
            raise ProfileError(f"{field}.name must be a slash-separated Java class")
        class_names.append(name)
        methods = _array(java_class["method"], f"{field}.method", non_empty=True)
        method_keys: list[tuple[str, str]] = []
        for method_index, method_value in enumerate(methods):
            method_field = f"{field}.method[{method_index}]"
            method = _table(method_value, method_field)
            _keys(method, method_field, {"name", "sig", "impl"},
                  {"name", "sig", "impl"})
            method_name = _string(method["name"], f"{method_field}.name")
            signature = _string(method["sig"], f"{method_field}.sig")
            if not signature.startswith("(") or ")" not in signature[1:]:
                raise ProfileError(f"{method_field}.sig must be a JNI method signature")
            impl = _string(method["impl"], f"{method_field}.impl")
            if IMPL_PATTERN.fullmatch(impl) is None:
                raise ProfileError(f"{method_field}.impl must be a namespaced binding id")
            method_keys.append((method_name, signature))
        _unique(method_keys, f"{field}.method name/signature pairs")
    _unique(class_names, "java.class names")


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


def validate_profile(document: Any, expected_package: str) -> dict[str, Any]:
    root = _table(document, "profile")
    _keys(root, "profile", ROOT_FIELDS, {"schema", "identity", "runtime"})
    if root["schema"] != 1:
        raise ProfileError("schema must be 1")
    _validate_identity(root["identity"], expected_package)
    _validate_runtime(root["runtime"])
    validators = {
        "data": _validate_data,
        "audio": _validate_audio,
        "java": _validate_java,
        "quirks": _validate_quirks,
        "input": _validate_input,
    }
    for field, validator in validators.items():
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
        raise ProfileError("profile schema root fields do not match the validator")
    if set(schema.get("required", [])) != {"schema", "identity", "runtime"}:
        raise ProfileError("profile schema required fields do not match the validator")
    if schema["properties"]["schema"].get("const") != 1:
        raise ProfileError("profile schema version must be 1")
    schema_abis = schema.get("$defs", {}).get("identity", {}).get(
        "properties", {}).get("abi", {}).get("enum", [])
    if set(schema_abis) != ABIS:
        raise ProfileError("profile schema ABI values do not match the validator")
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


def _valid_profile(package: str = "org.example.legacy") -> str:
    return f"""schema = 1

[identity]
package = "{package}"
name = "Generic Legacy Fixture"
version_code = [1, 2]
so_sha256 = ["{'0' * 64}", "{'1' * 64}"]
abi = "armeabi-v7a"

[runtime]
api_level = 19
lifecycle = "gl_surface_view"
surface = {{ width = 1280, height = 720 }}

[data]
mounts = [{{ guest = "/sdcard/game", source = "external", required = true }}]
working_directory = "/sdcard/game"
manifest = [{{ path = "files/archive.dat", required = true }}]

[audio]
cover_music = {{ source = "apk", path = "res/raw/music.ogg", loop = true }}

[[java.class]]
name = "org/example/Legacy"

[[java.class.method]]
name = "load"
sig = "(I)[B"
impl = "resource.load"

[quirks]
enabled = ["legacy_reads"]

[quirks.legacy_reads]
range = ["0x1000", "0x2000"]

[input]
profile = "generic_touch"
"""


def self_test(schema_path: Path) -> int:
    validate_schema(schema_path)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        valid = root / "org.example.legacy.profile.toml"
        valid.write_text(_valid_profile(), encoding="utf-8", newline="\n")
        document = load_profile(valid)
        assert document["runtime"]["surface"]["width"] == 1280
        valid.write_text(_valid_profile().replace(
            'abi = "armeabi-v7a"', 'abi = "armeabi"'
        ), encoding="utf-8", newline="\n")
        assert load_profile(valid)["identity"]["abi"] == "armeabi"

        cases = {
            "filename mismatch": _valid_profile("org.example.other"),
            "unknown field": _valid_profile().replace(
                "schema = 1", "schema = 1\nscript = \"run-me\""
            ),
            "bad lifecycle": _valid_profile().replace(
                'lifecycle = "gl_surface_view"', 'lifecycle = "per_game_loop"'
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
        }
        for label, content in cases.items():
            candidate = root / "org.example.legacy.profile.toml"
            candidate.write_text(content, encoding="utf-8", newline="\n")
            try:
                load_profile(candidate)
                raise AssertionError(f"{label} was accepted")
            except ProfileError:
                pass

        oversized = root / "org.example.legacy.profile.toml"
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
        validate_schema(args.schema)
        count = validate_directory(args.profiles)
        print(f"validated {count} Title Profile file(s)")
        return 0
    except ProfileError as error:
        print(f"Title Profile validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
