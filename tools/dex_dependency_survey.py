#!/usr/bin/env python3
"""Static DEX dependency survey for dexvm intrinsic calibration.

Measures, per APK: application class volume, Java thickness (same thresholds
as loader dex_analysis), the histogram of platform classes referenced by the
method/field constant pools, and declared native methods. Optionally folds in
the existing title profile `[[java.class]]` impl-id corpus which is the
design's calibration input for method-level takeover
(docs/design/dexvm/03-platform-intrinsics.md §3).

Output is a deterministic JSON report; APK-derived reports must stay local
(game data is never committed).
"""

from __future__ import annotations

import argparse
import io
import json
import re
import sys
import zipfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import dex_survey_lib as dexlib

ENGINE_CLASS_PREFIXES = {
    "Lcom/badlogic/gdx/": "libgdx",
    "Lorg/anddev/andengine/": "andengine",
    "Lorg/andengine/": "andengine",
    "Lorg/cocos2d/": "cocos2d-java",
    "Lorg/cocos2dx/": "cocos2d-x",
    "Lcom/unity3d/": "unity",
}


def classify_thickness(app_classes: int, instruction_units: int,
                       rendering_units: int) -> str:
    # Mirrors loader/dex_analysis.cpp Classify() thresholds.
    if app_classes == 0:
        return "none"
    if (app_classes >= 64 or instruction_units >= 8192 or
            rendering_units >= 512):
        return "thick"
    if (app_classes >= 16 or instruction_units >= 2048 or
            rendering_units >= 128):
        return "moderate"
    return "thin"


def survey_dex(data: bytes) -> dict:
    dex = dexlib.parse_dex(data)

    app_classes = 0
    bundled_platform_classes = 0
    declared_methods = 0
    native_methods = []
    instruction_units = 0
    rendering_units = 0
    engines: set[str] = set()

    defined_types = set()
    for parsed in dex.classes:
        descriptor = dex.type_name(parsed.type_index)
        defined_types.add(descriptor)
        for prefix, engine in ENGINE_CLASS_PREFIXES.items():
            if descriptor.startswith(prefix):
                engines.add(engine)
        if dexlib.is_platform_descriptor(descriptor):
            bundled_platform_classes += 1
            continue
        app_classes += 1
        for method in parsed.direct_methods + parsed.virtual_methods:
            declared_methods += 1
            owner, name, signature = dex.method_signature(method.method_index)
            if method.access_flags & dexlib.ACC_NATIVE:
                native_methods.append(f"{owner}->{name}{signature}")
            if method.code is not None:
                instruction_units += method.code.insns_size
                if name in ("onDrawFrame", "onDraw"):
                    rendering_units += method.code.insns_size

    platform_class_refs: Counter[str] = Counter()
    platform_method_refs: Counter[str] = Counter()
    for method_index in range(len(dex.method_ids)):
        owner, name, signature = dex.method_signature(method_index)
        if dexlib.is_platform_descriptor(owner):
            platform_class_refs[owner] += 1
            platform_method_refs[f"{owner}->{name}{signature}"] += 1
    for class_index, type_index, name_index in dex.field_ids:
        owner = dex.type_name(class_index)
        del type_index, name_index
        if dexlib.is_platform_descriptor(owner):
            platform_class_refs[owner] += 1

    return {
        "classes_total": len(dex.classes),
        "classes_application": app_classes,
        "classes_bundled_platform": bundled_platform_classes,
        "methods_declared": declared_methods,
        "methods_native": sorted(native_methods),
        "instruction_units": instruction_units,
        "java_thickness": classify_thickness(
            app_classes, instruction_units, rendering_units),
        "engine_fingerprints": sorted(engines),
        "platform_classes_referenced": dict(
            sorted(platform_class_refs.items())),
        "platform_methods_referenced": dict(
            sorted(platform_method_refs.items())),
    }


def survey_apk(path: Path) -> dict:
    with zipfile.ZipFile(path) as archive:
        with archive.open("classes.dex") as entry:
            data = entry.read()
    report = survey_dex(data)
    report["apk"] = path.name
    return report


IMPL_PATTERN = re.compile(r'impl\s*=\s*"([^"]+)"')


def survey_profiles(directory: Path) -> dict:
    corpus: Counter[str] = Counter()
    for profile in sorted(directory.glob("*.profile.toml")):
        for match in IMPL_PATTERN.finditer(
                profile.read_text(encoding="utf-8")):
            corpus[match.group(1)] += 1
    return dict(sorted(corpus.items()))


def build_self_test_dex() -> bytes:
    """Assemble a tiny but structurally valid dex in memory."""
    import hashlib
    import struct
    import zlib

    def mutf8(text: str) -> bytes:
        out = bytearray()
        for char in text:
            code = ord(char)
            if 1 <= code <= 0x7F:
                out.append(code)
            elif code < 0x800:
                out.append(0xC0 | (code >> 6))
                out.append(0x80 | (code & 0x3F))
            else:
                out.append(0xE0 | (code >> 12))
                out.append(0x80 | ((code >> 6) & 0x3F))
                out.append(0x80 | (code & 0x3F))
        return bytes(out)

    strings = sorted(["LSample;", "Ljava/lang/Object;", "V", "run", "()V",
                      "onDrawFrame"])
    types = sorted(["LSample;", "Ljava/lang/Object;", "V"])
    # proto: ()V   shorty "V" is strings entry "V"
    header = bytearray(0x70)
    body = bytearray()

    string_data_offsets = []
    string_section = bytearray()

    def uleb(value: int) -> bytes:
        out = bytearray()
        while True:
            byte = value & 0x7F
            value >>= 7
            if value:
                out.append(byte | 0x80)
            else:
                out.append(byte)
                return bytes(out)

    # Layout: header | string_ids | type_ids | proto_ids | field_ids(0)
    #         | method_ids | class_defs | data(string data, class_data)
    string_ids_off = 0x70
    type_ids_off = string_ids_off + len(strings) * 4
    proto_ids_off = type_ids_off + len(types) * 4
    method_ids_off = proto_ids_off + 12
    class_defs_off = method_ids_off + 8
    data_off = class_defs_off + 32

    cursor = data_off
    for value in strings:
        string_data_offsets.append(cursor)
        encoded = uleb(len(value)) + mutf8(value) + b"\x00"
        string_section += encoded
        cursor += len(encoded)

    class_data_off = cursor
    # 0 static fields, 0 instance, 1 direct method, 0 virtual
    class_data = (uleb(0) + uleb(0) + uleb(1) + uleb(0) +
                  uleb(0) + uleb(0x0100 | 0x0008) + uleb(0))
    cursor += len(class_data)

    file_size = cursor

    def string_index(value: str) -> int:
        return strings.index(value)

    def type_index(value: str) -> int:
        return types.index(value)

    ids = bytearray()
    for offset in string_data_offsets:
        ids += struct.pack("<I", offset)
    for value in types:
        ids += struct.pack("<I", string_index(value))
    ids += struct.pack("<III", string_index("V"), type_index("V"), 0)
    ids += struct.pack("<HHI", type_index("LSample;"), 0,
                       string_index("run"))
    ids += struct.pack("<IIIIIIII", type_index("LSample;"), 0x0001,
                       type_index("Ljava/lang/Object;"), 0, 0xFFFFFFFF, 0,
                       class_data_off, 0)

    body = ids + string_section + class_data

    struct.pack_into("<8s", header, 0, dexlib.DEX_MAGIC_035)
    struct.pack_into("<I", header, 0x20, file_size)
    struct.pack_into("<I", header, 0x24, 0x70)
    struct.pack_into("<I", header, 0x28, dexlib.ENDIAN_CONSTANT)
    struct.pack_into("<II", header, 0x38, len(strings), string_ids_off)
    struct.pack_into("<II", header, 0x40, len(types), type_ids_off)
    struct.pack_into("<II", header, 0x48, 1, proto_ids_off)
    struct.pack_into("<II", header, 0x50, 0, 0)
    struct.pack_into("<II", header, 0x58, 1, method_ids_off)
    struct.pack_into("<II", header, 0x60, 1, class_defs_off)
    struct.pack_into("<II", header, 0x68, file_size - data_off, data_off)

    everything = bytes(header) + bytes(body)
    signature = hashlib.sha1(everything[0x20:]).digest()
    everything = everything[:0x0C] + signature + everything[0x20:]
    checksum = zlib.adler32(everything[0x0C:]) & 0xFFFFFFFF
    everything = (everything[:0x08] + struct.pack("<I", checksum) +
                  everything[0x0C:])
    return everything


def self_test() -> int:
    data = build_self_test_dex()
    report = survey_dex(data)
    expectations = {
        "classes_total": 1,
        "classes_application": 1,
        "classes_bundled_platform": 0,
        "methods_declared": 1,
        "methods_native": ["LSample;->run()V"],
        "java_thickness": "thin",
    }
    for key, expected in expectations.items():
        if report[key] != expected:
            print(f"self-test: {key} = {report[key]!r}, expected "
                  f"{expected!r}", file=sys.stderr)
            return 1
    if dexlib.is_platform_descriptor("Lcom/example/Foo;"):
        print("self-test: platform prefix misclassified", file=sys.stderr)
        return 1
    if not dexlib.is_platform_descriptor("Landroid/os/Bundle;"):
        print("self-test: android prefix missed", file=sys.stderr)
        return 1
    if dexlib.is_platform_descriptor(
            "Landroid/support/v4/content/LocalBroadcastManager;"):
        print("self-test: Android Support class misclassified",
              file=sys.stderr)
        return 1
    value, offset = dexlib.read_uleb128(bytes([0xE5, 0x8E, 0x26]), 0)
    if value != 624485 or offset != 3:
        print("self-test: uleb128 decode broken", file=sys.stderr)
        return 1
    print("self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apk", type=Path, action="append", default=[])
    parser.add_argument("--profiles", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    if arguments.self_test:
        return self_test()
    if not arguments.apk and arguments.profiles is None:
        parser.error("provide --apk and/or --profiles, or --self-test")

    report: dict = {"titles": [], "profile_impl_corpus": {}}
    for apk in arguments.apk:
        report["titles"].append(survey_apk(apk))
    if arguments.profiles is not None:
        report["profile_impl_corpus"] = survey_profiles(arguments.profiles)

    text = json.dumps(report, indent=2, sort_keys=True)
    if arguments.output:
        arguments.output.write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
