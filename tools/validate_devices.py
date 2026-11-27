#!/usr/bin/env python3
"""Validate the bundled device-preset catalog (data/devices, schema 1)."""
from __future__ import annotations

import argparse
import re
import sys
import tomllib
from pathlib import Path

MAX_BYTES = 16 * 1024
MAX_PRESETS = 128
ID = re.compile(r"[a-z][a-z0-9]*(?:-[a-z0-9]+)*")
# Android system property values hold at most PROP_VALUE_MAX - 1 = 91 bytes.
PROPERTY = 91
# ARMv7 MIDR as 0x + 8 lowercase hex digits; bits 19:16 must be 0xf (CPUID scheme).
MIDR = re.compile(r"0x[0-9a-f]{8}")
BUILD_TOKEN = re.compile(r"[A-Za-z0-9._-]+")
# Exact keys and scalar rules are the executable schema 1 definition.
# Integers use inclusive bounds; strings use UTF-8 byte limits or finite choices.
# ("prop", n) strings are guest-visible ASCII (system properties, cpuinfo, GL strings): printable, at most n bytes.
SCHEMA = {
    "schema": (int, 1, 1), "id": (str, 64), "label": (str, 128),
    "description": (str, 512),
    "product": {
        "manufacturer": ("prop", PROPERTY), "brand": ("prop", PROPERTY), "name": ("prop", PROPERTY),
        "model": ("prop", PROPERTY), "device": ("prop", PROPERTY), "board": ("prop", PROPERTY),
        "cpu_abi": (str, {"armeabi-v7a"}),
    },
    "build": {
        "product": ("prop", PROPERTY), "id": ("prop", PROPERTY), "version_incremental": ("prop", PROPERTY),
        "display_id": ("prop", PROPERTY),
        "fingerprint": ("prop", PROPERTY),
        "version_release": (str, {"4.4.4"}), "version_sdk": (int, 19, 19),
    },
    "cpu": {"name": (str, 128), "cpuinfo_hardware": ("prop", 64), "midr": ("prop", 10), "cores": (int, 1, 32)},
    "board": {"hardware": ("prop", PROPERTY), "platform": ("prop", PROPERTY)},
    "gpu": {"vendor": ("prop", 128), "renderer": ("prop", 128)},
    "display": {"width": (int, 240, 4096), "height": (int, 240, 4096),
                "density_dpi": (int, 72, 960)},
    "memory": {"ram_mb": (int, 128, 8192)},
}


class PresetError(ValueError):
    pass


def validate_fields(value: object, schema: dict, prefix: str = "") -> None:
    if type(value) is not dict:
        raise PresetError(f"{prefix or 'root'}: expected table")
    missing, extra = schema.keys() - value.keys(), value.keys() - schema.keys()
    if missing or extra:
        raise PresetError(f"{prefix or 'root'}: missing={sorted(missing)}, unknown={sorted(extra)}")
    for key, rule in schema.items():
        field, item = f"{prefix}.{key}" if prefix else key, value[key]
        if isinstance(rule, dict):
            validate_fields(item, rule, field)
            continue
        if rule[0] == "prop":
            if type(item) is not str or not item or item != item.strip() or len(item) > rule[1] \
                    or any(not 32 <= ord(c) <= 126 for c in item):
                raise PresetError(f"{field}: expected 1..{rule[1]} printable ASCII bytes without padding")
            continue
        if type(item) is not rule[0]:  # bool must not pass as an integer.
            raise PresetError(f"{field}: expected {rule[0].__name__}")
        if rule[0] is int:
            if not rule[1] <= item <= rule[2]:
                raise PresetError(f"{field}: expected {rule[1]}..{rule[2]}")
        else:
            if not item.strip() or item != item.strip() or any(ord(c) < 32 or ord(c) == 127 for c in item):
                raise PresetError(f"{field}: empty, padded or control-character text")
            if isinstance(rule[1], set):
                if item not in rule[1]:
                    raise PresetError(f"{field}: expected one of {sorted(rule[1])}")
            elif len(item.encode("utf-8")) > rule[1]:
                raise PresetError(f"{field}: exceeds {rule[1]} UTF-8 bytes")


def load_preset(path: Path) -> dict:
    try:
        if path.is_symlink() or not path.is_file():
            raise PresetError("expected regular file, no symlinks")
        with path.open("rb") as stream:
            raw = stream.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise PresetError(f"exceeds {MAX_BYTES} bytes")
        value = tomllib.loads(raw.decode("utf-8"))
        validate_fields(value, SCHEMA)
        if not ID.fullmatch(value["id"]) or path.name != value["id"] + ".toml":
            raise PresetError("id must be lowercase kebab-case and match filename")
        midr = value["cpu"]["midr"]
        if not MIDR.fullmatch(midr) or (int(midr, 16) >> 16) & 0xF != 0xF:
            raise PresetError("cpu.midr: expected 0x + 8 lowercase hex digits with architecture 0xf")
        product = value["product"]
        head = f"{product['brand']}/{product['name']}/{product['device']}"
        build = value["build"]
        for key in ("id", "version_incremental"):
            if not BUILD_TOKEN.fullmatch(build[key]):
                raise PresetError(f"build.{key}: expected [A-Za-z0-9._-]+")
        # The fingerprint restates the other fields, as Android's build system composes it.
        expected = f"{head}:4.4.4/{build['id']}/{build['version_incremental']}:user/release-keys"
        if build["fingerprint"] != expected:
            raise PresetError(f"build.fingerprint: expected {expected}")
        return value
    except (OSError, UnicodeError, ValueError) as error:
        raise PresetError(f"{path}: {error}") from error


def validate_catalog(directory: Path) -> list[dict]:
    if not directory.is_dir():
        raise PresetError(f"{directory}: missing catalog directory")
    paths = []
    try:
        for path in directory.iterdir():
            if path.suffix.lower() == ".toml":
                paths.append(path)
                if len(paths) > MAX_PRESETS:
                    raise PresetError(f"{directory}: exceeds {MAX_PRESETS} presets")
        if not paths:
            raise PresetError(f"{directory}: empty catalog")
        presets = [load_preset(path) for path in sorted(paths)]
        ids = [preset["id"] for preset in presets]
        if len(set(ids)) != len(ids):
            raise PresetError(f"{directory}: duplicate preset id")
        return presets
    except OSError as error:
        raise PresetError(f"{directory}: {error}") from error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devices", type=Path, default=Path(__file__).resolve().parents[1] / "data/devices")
    args = parser.parse_args()
    try:
        presets = validate_catalog(args.devices)
    except PresetError as error:
        print(f"device preset validation failed: {error}", file=sys.stderr)
        return 1
    print(f"validated {len(presets)} device presets (schema 1)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
