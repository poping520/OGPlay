#!/usr/bin/env python3
"""Validate the redistributable Android guest system-library payload."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import tempfile
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
    "lib/libcrypto.so",
    "lib/libdl.so",
    "lib/libgabi++.so",
    "lib/libicui18n.so",
    "lib/libicuuc.so",
    "lib/libm.so",
    "lib/libogplay_jni.so",
    "lib/libstdc++.so",
    "lib/libstlport.so",
    "lib/libz.so",
}
NOTICES = {
    "notices/libc.so.txt",
    "notices/libcrypto.so.txt",
    "notices/libdl.so.txt",
    "notices/libgabi++.so.txt",
    "notices/libm.so.txt",
    "notices/libstdc++.so.txt",
    "notices/libstlport.so.txt",
    "notices/libz.so.txt",
}
BOOT_DEX = "framework/bootdex.jar"
BOOT_DEX_NOTICE = "notices/bootdex.jar.txt"
ICU_DATA = "icu/icudt51l.dat"
ICU_NOTICES = {
    "notices/icu4c-license.html",
    "notices/icu4c-unicode-license.txt",
}
PAYLOAD_FILES = LIBRARIES | NOTICES | {
    BOOT_DEX, BOOT_DEX_NOTICE, ICU_DATA, "manifest.json",
    "source-manifest.xml"} | ICU_NOTICES
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


def _validate_elf(path: Path, label: str) -> tuple[str, list[str]]:
    image = path.read_bytes()
    header = image[:52]
    if len(header) != 52 or header[:4] != b"\x7fELF":
        raise PayloadError(f"{label} is not ELF")
    if header[4:6] != bytes((1, 1)):
        raise PayloadError(f"{label} must be little-endian ELF32")
    elf_type, machine = struct.unpack_from("<HH", header, 16)
    if elf_type != 3 or machine != 40:
        raise PayloadError(f"{label} must be an ARM shared object")
    phoff = struct.unpack_from("<I", header, 28)[0]
    phentsize, phnum = struct.unpack_from("<HH", header, 42)
    loads: list[tuple[int, int, int]] = []
    dynamic: tuple[int, int] | None = None
    for index in range(phnum):
        offset = phoff + index * phentsize
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<IIIII", image, offset)
        if p_type == 1:
            loads.append((p_vaddr, p_offset, p_filesz))
        elif p_type == 2:
            dynamic = (p_offset, p_filesz)
    if dynamic is None:
        raise PayloadError(f"{label} has no PT_DYNAMIC")
    entries: list[tuple[int, int]] = []
    for offset in range(dynamic[0], dynamic[0] + dynamic[1], 8):
        tag, value = struct.unpack_from("<II", image, offset)
        if tag == 0: break
        entries.append((tag, value))
    strtab_address = next((value for tag, value in entries if tag == 5), None)
    if strtab_address is None:
        raise PayloadError(f"{label} has no DT_STRTAB")
    strtab = next((file_offset + strtab_address - address
                   for address, file_offset, size in loads
                   if address <= strtab_address < address + size), None)
    if strtab is None:
        raise PayloadError(f"{label} DT_STRTAB is unmapped")
    def text_at(index: int) -> str:
        end = image.find(b"\0", strtab + index)
        if end < 0: raise PayloadError(f"{label} has an invalid dynamic string")
        return image[strtab + index:end].decode("ascii")
    soname_values = [text_at(value) for tag, value in entries if tag == 14]
    if len(soname_values) != 1:
        raise PayloadError(f"{label} must have exactly one DT_SONAME")
    return soname_values[0], [text_at(value) for tag, value in entries if tag == 1]


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
        "platform/abi/cpp": "18f1b5e28734183ff8073fe86dc46bc4ebba8a59",
        "platform/bionic": "081db840befec895fb86e709ae95832ade2d065c",
        "platform/external/apache-http":
            "d8895c4ccee0979e658a76d9c92e842e3f83d0cd",
        "platform/external/icu4c":
            "18668f3b015a110275f5cc9a8722b2f65f3333bf",
        "platform/external/openssl":
            "dd1da36b0baa39942f0aef42c4712ef0ad628a83",
        "platform/external/stlport":
            "628e14d37c5b239839a466e81c74bf66255b770b",
        "platform/external/zlib":
            "a5c7131da47c991585a6c6ac0c063b6d7d56e3fc",
        "platform/libcore": "d49420b1b7edf8b3f27dabd3e1b7512a5502595e",
        "platform/frameworks/base": "63ade05d76785975fc3292ca030abbaa1dda8891",
        "platform/dalvik": "36e356c96640775f0a3f167bd2426ea0f0093b8b",
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
        resource_recipe = bootdex.load_resources()
        resources = bootdex.resource_payload(resource_recipe)
    except (bootdex.BuildError, OSError) as error:
        raise PayloadError(f"BootDex recipe is invalid: {error}") from error
    try:
        jar_bytes = (root / BOOT_DEX).read_bytes()
        with zipfile.ZipFile(root / BOOT_DEX) as archive:
            expected_entries = ["META-INF/MANIFEST.MF", "classes.dex"] + \
                sorted(resources)
            if archive.namelist() != expected_entries:
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
           for source in recipe if source != "conscrypt.jar"):
        raise PayloadError("BootDex source project is not pinned")
    if len(dex) < 0x70 or dex[:8] != b"dex\n035\0" or \
            struct.unpack_from("<I", dex, 0x20)[0] != len(dex) or \
            struct.unpack_from("<I", dex, 0x28)[0] != 0x12345678:
        raise PayloadError("boot_dex classes.dex is not canonical DEX 035")
    if bootdex.make_jar(dex, resources) != jar_bytes:
        raise PayloadError("boot_dex archive metadata is not canonical")
    try:
        classes = bootdex.class_names(dex)
    except dex_survey_lib.DexFormatError as error:
        raise PayloadError(f"boot_dex DEX structure is invalid: {error}") \
            from error
    try:
        with tempfile.TemporaryDirectory(
                prefix="ogplay-payload-bootdex-java-") as work:
            selected = bootdex.compile_expected_class_names(recipe, Path(work))
    except (bootdex.BuildError, OSError) as error:
        raise PayloadError(
            f"BootDex custom Java class selection is invalid: {error}") from error
    if classes != selected:
        raise PayloadError("boot_dex exact class selection does not match")

    icu = _mapping(manifest.get("icu"), "icu")
    expected_icu = {
        "source_project": "platform/external/icu4c",
        "source_revision": "18668f3b015a110275f5cc9a8722b2f65f3333bf",
        "version": "51.1.0.1",
        "data_version": "51.1",
        "path": ICU_DATA,
        "size": 14146832,
        "sha256":
            "8275408cb7161606c9a1b55edf12df538a7110ad53103a00f8ac7ba5b092a96f",
    }
    for key, expected in expected_icu.items():
        if icu.get(key) != expected:
            raise PayloadError(f"icu.{key} does not match")
    _validate_digest(root / ICU_DATA, icu.get("size"), icu.get("sha256"),
                     "icu.data")
    with (root / ICU_DATA).open("rb") as source_file:
        if source_file.read(4) != bytes((0x20, 0x00, 0xda, 0x27)):
            raise PayloadError("icu.data is not an ICU common-data archive")
    notices = _items(icu.get("notices"), "icu.notices")
    notice_entries = {
        _text(_mapping(item, "icu.notice").get("path"), "icu.notice.path"):
        _mapping(item, "icu.notice") for item in notices
    }
    if set(notice_entries) != ICU_NOTICES:
        raise PayloadError("ICU notice file set does not match")
    for relative, entry in notice_entries.items():
        path = root / relative
        if not path.is_file() or _digest(path) != _text(
                entry.get("sha256"), f"icu.notice[{relative}].sha256"):
            raise PayloadError(f"ICU notice does not match: {relative}")

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
        soname, actual_needed = _validate_elf(path, label)
        if entry.get("soname") != Path(relative).name or soname != entry.get("soname"):
            raise PayloadError(f"{label}.soname does not match")
        needed = entry.get("needed")
        if not isinstance(needed, list) or not all(
                isinstance(item, str) and item for item in needed):
            raise PayloadError(f"{label}.needed must be a string array")
        if needed != actual_needed:
            raise PayloadError(f"{label}.needed does not match DT_NEEDED")
        if relative == "lib/libogplay_jni.so":
            build = entry.get("build")
            expected_sources = [
                "src/guest/crypto/crypto_jni.c",
                "src/guest/icu/icu_jni.c",
                "src/guest/icu/icu51_capi.h",
            ]
            if not isinstance(build, dict) or build.get("ndk_revision") != "25.2.9519653" or \
                    build.get("target") != "armv7a-linux-androideabi19" or \
                    build.get("language") != "C11" or \
                    build.get("generator") != "tools/bootdex/build_bootdex.py":
                raise PayloadError(f"{label}.build is not the pinned NDK r25c recipe")
            repository = root.parents[2]
            generator = repository / build["generator"]
            if build.get("generator_sha256") != _digest(generator):
                raise PayloadError(f"{label}.build generator SHA-256 does not match")
            sources = build.get("sources")
            if not isinstance(sources, list) or \
                    [item.get("path") for item in sources
                     if isinstance(item, dict)] != expected_sources:
                raise PayloadError(f"{label}.build sources do not match")
            for source in sources:
                source_path = repository / source["path"]
                if source.get("sha256") != _digest(source_path):
                    raise PayloadError(f"{label}.build source SHA-256 does not match")
            inputs = build.get("inputs")
            expected_inputs = {
                path: libraries[path]["sha256"]
                for path in ("lib/libcrypto.so", "lib/libicuuc.so",
                             "lib/libicui18n.so")
            }
            icu = _mapping(manifest.get("icu"), "icu")
            expected_inputs[icu["path"]] = icu["sha256"]
            if not isinstance(inputs, list) or any(
                    not isinstance(item, dict) for item in inputs) or \
                    {item.get("path"): item.get("sha256")
                     for item in inputs} != expected_inputs:
                raise PayloadError(f"{label}.build input SHA-256 does not match manifest")
        source_project = _text(entry.get("source_project"),
                               f"{label}.source_project")
        if source_project.startswith("platform/"):
            if source_project not in source_projects:
                raise PayloadError(f"{label}.source_project is not pinned")
        elif source_project != "ogplay":
            raise PayloadError(f"{label}.source_project is unknown")
        if source_project == "ogplay":
            continue
        notice = _text(entry.get("notice"), f"{label}.notice")
        expected_notice = f"notices/{Path(relative).name}.txt"
        if source_project == "platform/external/icu4c":
            expected_notice = "notices/icu4c-license.html"
        if notice != expected_notice:
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
    print("Android runtime payload validated: API 19, boot dex, ICU 51.1, 11 declared guest libraries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
