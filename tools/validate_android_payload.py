#!/usr/bin/env python3
"""Validate the redistributable Android guest system-library payload."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))
sys.path.insert(0, str(TOOLS_DIR / "bootdex"))

import build_bootdex as bootdex
import dex_survey_lib


LIBRARIES = {
    "lib/libc.so",
    "lib/libdl.so",
    "lib/libm.so",
    "lib/libstdc++.so",
    "lib/libz.so",
}
NOTICES = {f"notices/{Path(path).name}.txt" for path in LIBRARIES}
BOOT_DEX = "framework/bootdex.jar"
BOOT_DEX_NOTICE = "notices/bootdex.jar.txt"
PAYLOAD_FILES = LIBRARIES | NOTICES | {
    BOOT_DEX, BOOT_DEX_NOTICE, "manifest.json", "source-manifest.xml"}
SHA256 = re.compile(r"[0-9a-f]{64}")


class PayloadError(RuntimeError):
    pass


def _mapping(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise PayloadError(f"{name} must be an object")
    return value


def _items(value: Any, name: str) -> list[Any]:
    if not isinstance(value, list):
        raise PayloadError(f"{name} must be an array")
    return value


def _text(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value:
        raise PayloadError(f"{name} must be a non-empty string")
    return value


def _digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def _validate_digest(path: Path, size: Any, digest: Any, label: str) -> None:
    if not path.is_file():
        raise PayloadError(f"{label} is missing")
    if not isinstance(size, int) or size <= 0 or path.stat().st_size != size:
        raise PayloadError(f"{label} size does not match")
    expected = _text(digest, f"{label}.sha256")
    if SHA256.fullmatch(expected) is None or _digest(path) != expected:
        raise PayloadError(f"{label} SHA-256 does not match")


def _validate_elf(path: Path, label: str) -> None:
    with path.open("rb") as source:
        header = source.read(20)
    if len(header) != 20 or header[:4] != b"\x7fELF":
        raise PayloadError(f"{label} is not ELF")
    if header[4:6] != bytes((1, 1)):
        raise PayloadError(f"{label} must be little-endian ELF32")
    elf_type, machine = struct.unpack_from("<HH", header, 16)
    if elf_type != 3 or machine != 40:
        raise PayloadError(f"{label} must be an ARM shared object")


def _validate_source_manifest(root: Path, source: dict[str, Any]) -> set[str]:
    relative = _text(source.get("pinned_manifest"), "source.pinned_manifest")
    if relative != "source-manifest.xml":
        raise PayloadError("source manifest must use the canonical payload path")
    try:
        manifest = ET.parse(root / relative).getroot()
    except (OSError, ET.ParseError) as error:
        raise PayloadError(f"source manifest is invalid: {error}") from error
    default = manifest.find("default")
    if default is None or default.get("revision") != \
            "refs/tags/android-4.4.4_r2.0.1":
        raise PayloadError("source manifest does not pin the target AOSP tag")
    projects = {
        project.get("name"): project.get("revision")
        for project in manifest.findall("project")
    }
    expected = {
        "platform/bionic": "081db840befec895fb86e709ae95832ade2d065c",
        "platform/external/zlib":
            "a5c7131da47c991585a6c6ac0c063b6d7d56e3fc",
        "platform/libcore": "d49420b1b7edf8b3f27dabd3e1b7512a5502595e",
    }
    for name, revision in expected.items():
        if projects.get(name) != revision:
            raise PayloadError(f"source manifest does not pin {name}")
    return {name for name in projects if name is not None}


def validate(root: Path) -> None:
    root = root.resolve()
    actual_files = {
        path.relative_to(root).as_posix()
        for path in root.rglob("*") if path.is_file()
    }
    if actual_files != PAYLOAD_FILES:
        missing = sorted(PAYLOAD_FILES - actual_files)
        extra = sorted(actual_files - PAYLOAD_FILES)
        raise PayloadError(f"payload file set differs; missing={missing}, extra={extra}")

    try:
        manifest = _mapping(
            json.loads((root / "manifest.json").read_text(encoding="utf-8")),
            "manifest")
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PayloadError(f"manifest.json is invalid: {error}") from error
    if manifest.get("schema_version") != 1 or manifest.get("api_level") != 19 \
            or manifest.get("android_release") != "4.4.4":
        raise PayloadError("manifest does not describe Android 4.4.4 API 19")
    if _mapping(manifest.get("abi"), "abi") != {
            "name": "armeabi-v7a", "elf_class": "ELF32",
            "endianness": "little", "machine": "ARM", "type": "DYN"}:
        raise PayloadError("manifest ABI is not the supported ARM payload")

    source = _mapping(manifest.get("source"), "source")
    if source.get("kind") != "aosp-source-build" or \
            source.get("tag") != "android-4.4.4_r2.0.1" or \
            source.get("manifest_kind") != "minimal-pinned" or \
            source.get("worktrees_clean") is not True:
        raise PayloadError("source provenance is incomplete")
    source_projects = _validate_source_manifest(root, source)

    try:
        recipe = bootdex.load_recipe()
    except (bootdex.BuildError, OSError) as error:
        raise PayloadError(f"BootDex recipe is invalid: {error}") from error
    try:
        jar_bytes = (root / BOOT_DEX).read_bytes()
        with zipfile.ZipFile(root / BOOT_DEX) as archive:
            if archive.namelist() != ["META-INF/MANIFEST.MF", "classes.dex"]:
                raise PayloadError("boot_dex archive entries do not match")
            if archive.comment or \
                    archive.read("META-INF/MANIFEST.MF") != \
                    bootdex.JAR_MANIFEST:
                raise PayloadError("boot_dex JAR manifest is not canonical")
            dex = archive.read("classes.dex")
    except (OSError, zipfile.BadZipFile, KeyError) as error:
        raise PayloadError(f"boot_dex archive is invalid: {error}") from error
    try:
        expected_metadata = bootdex.boot_metadata(jar_bytes, dex, recipe)
    except OSError as error:
        raise PayloadError(f"BootDex metadata input is missing: {error}") from error
    if _mapping(manifest.get("boot_dex"), "boot_dex") != expected_metadata:
        raise PayloadError("boot_dex metadata does not match its recipe")
    if any(bootdex.SOURCES[source][0] not in source_projects
           for source in recipe):
        raise PayloadError("BootDex source project is not pinned")
    if len(dex) < 0x70 or dex[:8] != b"dex\n035\0" or \
            struct.unpack_from("<I", dex, 0x20)[0] != len(dex) or \
            struct.unpack_from("<I", dex, 0x28)[0] != 0x12345678:
        raise PayloadError("boot_dex classes.dex is not canonical DEX 035")
    if bootdex.make_jar(dex) != jar_bytes:
        raise PayloadError("boot_dex archive metadata is not canonical")
    try:
        classes = bootdex.class_names(dex)
    except dex_survey_lib.DexFormatError as error:
        raise PayloadError(f"boot_dex DEX structure is invalid: {error}") \
            from error
    selected = tuple(sorted(item for values in recipe.values() for item in values))
    if classes != selected:
        raise PayloadError("boot_dex exact class selection does not match")

    build = _mapping(manifest.get("build"), "build")
    expected_build = {
        "lunch_target": "aosp_arm-user",
        "build_type": "release",
        "target_product": "aosp_arm",
        "target_arch_variant": "armv7-a",
        "target_cpu_variant": "generic",
        "build_id": "KTU84Q",
    }
    for key, expected in expected_build.items():
        if build.get(key) != expected:
            raise PayloadError(f"build.{key} does not match")
    if build.get("build_number") is not None or \
            build.get("build_fingerprint") is not None:
        raise PayloadError("unavailable build facts must remain null")

    libraries: dict[str, dict[str, Any]] = {}
    for index, raw in enumerate(_items(manifest.get("libraries"), "libraries")):
        entry = _mapping(raw, f"libraries[{index}]")
        relative = _text(entry.get("path"), f"libraries[{index}].path")
        if relative in libraries:
            raise PayloadError(f"duplicate library: {relative}")
        libraries[relative] = entry
    if set(libraries) != LIBRARIES:
        raise PayloadError("manifest library set does not match")

    for relative, entry in libraries.items():
        label = f"libraries[{relative}]"
        path = root / relative
        _validate_digest(path, entry.get("size"), entry.get("sha256"), label)
        _validate_elf(path, label)
        if entry.get("soname") != Path(relative).name:
            raise PayloadError(f"{label}.soname does not match")
        needed = entry.get("needed")
        if not isinstance(needed, list) or not all(
                isinstance(item, str) and item for item in needed):
            raise PayloadError(f"{label}.needed must be a string array")
        source_project = _text(entry.get("source_project"),
                               f"{label}.source_project")
        if source_project not in source_projects:
            raise PayloadError(f"{label}.source_project is not pinned")
        notice = _text(entry.get("notice"), f"{label}.notice")
        if notice != f"notices/{Path(relative).name}.txt":
            raise PayloadError(f"{label}.notice does not match")
        notice_path = root / notice
        if not notice_path.is_file():
            raise PayloadError(f"{label} NOTICE is missing")
        notice_hash = _text(entry.get("notice_sha256"),
                            f"{label}.notice_sha256")
        if SHA256.fullmatch(notice_hash) is None or \
                _digest(notice_path) != notice_hash:
            raise PayloadError(f"{label} NOTICE SHA-256 does not match")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    try:
        validate(args.root)
    except (OSError, PayloadError) as error:
        parser.error(str(error))
    print("Android runtime payload validated: API 19, boot dex, 5 libraries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
