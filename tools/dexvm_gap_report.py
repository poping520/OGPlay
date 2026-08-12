#!/usr/bin/env python3
"""dexvm intrinsic gap report (HANDOFF-TITLES §4 recomputation tool).

Statically compares an APK's platform-class references against the intrinsic
declarations in the dexvm catalogs and reports the gap in two layers:

  * link_blocking: platform classes referenced as superclass/interface by an
    application class but absent from the catalogs. These fail DexClassLinker
    linking immediately (the linker itself also reports the full set now).
  * runtime: platform-owned method_ids/field_ids referenced by the constant
    pool but not declared by the catalogs. These are *potential* runtime
    hits; only executed code paths actually fail, so treat this layer as a
    prioritized worklist rather than a hard requirement.

The catalog declaration set is extracted from the C++ sources by pattern
matching (`descriptor = "L...;"` blocks and `{"name", "(sig)ret", ...}`
method entries). This is advisory diagnostics, not a build input; the
machine-authoritative failure source remains the linker/interpreter.

APK-derived reports must stay local (game data is never committed).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import dex_survey_lib as lib

DEFAULT_CATALOG_SOURCES = (
    "src/runtime/dexvm/core_catalog.cpp",
    "src/runtime/integration/dexvm_android.cpp",
)

# org.xml.sax lives outside PLATFORM_PREFIXES but is still a platform class
# for gap purposes (DUNQ references DefaultHandler).
EXTRA_PLATFORM_PREFIXES = ("Lorg/xml/", "Lorg/json/", "Lorg/w3c/")

CLASS_PATTERN = re.compile(r'descriptor\s*=\s*"(L[^"]+;)"')
METHOD_PATTERN = re.compile(r'\{"([^"]+)",\s*\n?\s*"(\([^"]*\)[^"]*)"')
FIELD_PATTERN = re.compile(r'\{"([^"]+)",\s*"([^(][^"]*)"')
PLACEHOLDER_PATTERN = re.compile(r'\{"(L[^"]+;)",\s*(?:true|false)')


def is_platform(descriptor: str) -> bool:
    return descriptor.startswith(lib.PLATFORM_PREFIXES) or \
        descriptor.startswith(EXTRA_PLATFORM_PREFIXES)


def parse_catalog_sources(paths: list[Path]) -> tuple[set[str], set[tuple[str, str, str]]]:
    """Returns (declared class descriptors, declared (class, member, sig))."""
    classes: set[str] = set()
    members: set[tuple[str, str, str]] = set()
    for path in paths:
        text = path.read_text(encoding="utf-8")
        # Walk descriptor-anchored blocks: members between one descriptor
        # assignment and the next belong to that class.
        events: list[tuple[int, str, tuple[str, str] | None]] = []
        for match in CLASS_PATTERN.finditer(text):
            events.append((match.start(), "class", (match.group(1), "")))
        for match in PLACEHOLDER_PATTERN.finditer(text):
            events.append((match.start(), "class", (match.group(1), "")))
        for match in METHOD_PATTERN.finditer(text):
            events.append((match.start(), "method",
                           (match.group(1), match.group(2))))
        for match in FIELD_PATTERN.finditer(text):
            name, sig = match.group(1), match.group(2)
            # Field signatures are bare type descriptors; skip method rows
            # and impl-id strings that the loose pattern also matches.
            if re.fullmatch(r"\[*(?:[ZBSCIJFD]|L[^\s\";]+;)", sig):
                events.append((match.start(), "field", (name, sig)))
        events.sort(key=lambda item: item[0])
        current: str | None = None
        for _, kind, payload in events:
            if kind == "class":
                current = payload[0]
                classes.add(current)
            elif current is not None:
                members.add((current, payload[0], payload[1]))
    return classes, members


def load_dex(apk: Path) -> lib.DexFile:
    with zipfile.ZipFile(apk) as archive:
        return lib.parse_dex(archive.read("classes.dex"))


def build_report(dex: lib.DexFile, declared_classes: set[str],
                 declared_members: set[tuple[str, str, str]]) -> dict:
    app_types = {dex.type_name(c.type_index) for c in dex.classes}

    link_blocking: dict[str, list[str]] = {}
    for dex_class in dex.classes:
        requiring = dex.type_name(dex_class.type_index)
        hierarchy: list[str] = []
        if dex_class.superclass_index is not None:
            hierarchy.append(dex.type_name(dex_class.superclass_index))
        hierarchy += [dex.type_name(i)
                      for i in dex_class.interface_type_indices]
        for name in hierarchy:
            if name in app_types or name in declared_classes:
                continue
            link_blocking.setdefault(name, []).append(requiring)

    runtime: dict[str, dict] = {}

    def class_entry(owner: str) -> dict:
        return runtime.setdefault(owner, {
            "class_declared": owner in declared_classes,
            "methods": [], "fields": [],
        })

    for index in range(len(dex.method_ids)):
        owner, name, descriptor = dex.method_signature(index)
        if owner in app_types or owner.startswith("["):
            continue
        if not is_platform(owner):
            continue
        if (owner, name, descriptor) in declared_members:
            continue
        class_entry(owner)["methods"].append(name + descriptor)

    for class_index, type_index, name_index in dex.field_ids:
        owner = dex.type_name(class_index)
        if owner in app_types or not is_platform(owner):
            continue
        name = dex.strings[name_index]
        field_type = dex.type_name(type_index)
        if (owner, name, field_type) in declared_members:
            continue
        class_entry(owner)["fields"].append(name + ":" + field_type)

    for entry in runtime.values():
        entry["methods"] = sorted(set(entry["methods"]))
        entry["fields"] = sorted(set(entry["fields"]))

    return {
        "schema": 1,
        "link_blocking": {
            name: sorted(set(requiring))
            for name, requiring in sorted(link_blocking.items())
        },
        "runtime": dict(sorted(runtime.items())),
        "summary": {
            "link_blocking_classes": len(link_blocking),
            "runtime_classes": len(runtime),
            "runtime_methods": sum(
                len(e["methods"]) for e in runtime.values()),
            "runtime_fields": sum(
                len(e["fields"]) for e in runtime.values()),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apk", required=True, type=Path)
    parser.add_argument("--repo", type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()

    sources = [arguments.repo / p for p in DEFAULT_CATALOG_SOURCES]
    declared_classes, declared_members = parse_catalog_sources(sources)
    report = build_report(load_dex(arguments.apk), declared_classes,
                          declared_members)
    rendered = json.dumps(report, indent=2, sort_keys=False)
    if arguments.output:
        arguments.output.write_text(rendered + "\n", encoding="utf-8")
    else:
        print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
