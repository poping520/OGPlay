#!/usr/bin/env python3
"""dexvm intrinsic gap report (step 1 of docs/playbook/NEW-TITLE.md).

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

# Globs, so splitting a catalog into more batch files never silently shrinks
# the declared set (which would report already-implemented classes as gaps).
DEFAULT_CATALOG_GLOBS = (
    "src/runtime/dexvm/intrinsics/*.cpp",
    "src/runtime/integration/dexvm_android/*.cpp",
)

# org.xml.sax lives outside PLATFORM_PREFIXES but is still a platform class
# for gap purposes (DUNQ references DefaultHandler).
EXTRA_PLATFORM_PREFIXES = ("Lorg/xml/", "Lorg/json/", "Lorg/w3c/")

CLASS_PATTERN = re.compile(r'descriptor\s*=\s*"(L[^"]+;)"')
# Catalog helpers also declare classes positionally (Exception("L…;", super),
# container loops, placeholder tables). Any bare class-descriptor literal in a
# catalog source is a declared type: the linker rejects references to
# unregistered superclasses/interfaces, so these never over-collect.
BARE_CLASS_PATTERN = re.compile(r'"(L[a-zA-Z0-9_$/]+;)"')
METHOD_PATTERN = re.compile(r'\{"([^"]+)",\s*\n?\s*"(\([^"]*\)[^"]*)"')
FIELD_PATTERN = re.compile(r'\{"([^"]+)",\s*"([^(][^"]*)"')
PLACEHOLDER_PATTERN = re.compile(r'\{"(L[^"]+;)",\s*(?:true|false)')
BUILDER_CLASS_PATTERN = re.compile(
    r'auto\s+([A-Za-z_]\w*)\s*=\s*(?:dx::)?IntrinsicClassBuilder::'
    r'(?:RootClass|Class|Interface)\s*\(\s*"(L[^"]+;)"')
# Constructor-style calls take the descriptor first; the optional second
# quoted argument is the descriptor of the named method forms.
BUILDER_METHOD_PATTERN = re.compile(
    r'([A-Za-z_]\w*)\.(?:Constructor|StaticMethod|VirtualMethod|FinalMethod|'
    r'OverrideMethod|FinalOverrideMethod|'
    r'UnimplementedStatic|UnimplementedVirtual|UnimplementedFinal|'
    r'UnimplementedConstructor)\s*\(\s*"([^"]+)"(?:\s*,\s*"(\([^"]*\)[^"]*)")?')
BUILDER_FIELD_PATTERN = re.compile(
    r'([A-Za-z_]\w*)\.(?:InstanceField|StaticField)\s*\('
    r'\s*"([^"]+)"\s*,\s*"([^"]+)"')


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
        # Bare literals only widen the declared-class set; they must not
        # re-anchor member attribution, so they are collected separately.
        classes.update(match.group(1)
                       for match in BARE_CLASS_PATTERN.finditer(text))
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

        # DVM-32..38 migrated catalogs to IntrinsicClassBuilder. Attribute
        # builder calls to the most recent construction of the same local
        # variable; this also handles aggregation files that declare several
        # classes in source order.
        builder_events: list[tuple[int, str, tuple[str, ...]]] = []
        for match in BUILDER_CLASS_PATTERN.finditer(text):
            builder_events.append(
                (match.start(), "class", (match.group(1), match.group(2))))
        for match in BUILDER_METHOD_PATTERN.finditer(text):
            if match.group(3) is None:
                builder_events.append(
                    (match.start(), "method",
                     (match.group(1), "<init>", match.group(2))))
            else:
                builder_events.append(
                    (match.start(), "method",
                     (match.group(1), match.group(2), match.group(3))))
        for match in BUILDER_FIELD_PATTERN.finditer(text):
            builder_events.append(
                (match.start(), "field",
                 (match.group(1), match.group(2), match.group(3))))
        builder_events.sort(key=lambda item: item[0])
        builders: dict[str, str] = {}
        for _, kind, payload in builder_events:
            variable = payload[0]
            if kind == "class":
                builders[variable] = payload[1]
                classes.add(payload[1])
            elif variable in builders:
                members.add((builders[variable], payload[1], payload[2]))
    return classes, members


def load_dex(apk: Path) -> lib.DexFile:
    with zipfile.ZipFile(apk) as archive:
        return lib.parse_dex(archive.read("classes.dex"))


def build_report(dex: lib.DexFile, declared_classes: set[str],
                 declared_members: set[tuple[str, str, str]]) -> dict:
    app_types = {dex.type_name(c.type_index) for c in dex.classes}

    # role is what the placeholder declaration must be: a class used as a
    # superclass needs <init>, an interface must be declared as one.
    link_blocking: dict[str, dict] = {}
    for dex_class in dex.classes:
        requiring = dex.type_name(dex_class.type_index)
        hierarchy: list[tuple[str, str]] = []
        if dex_class.superclass_index is not None:
            hierarchy.append((dex.type_name(dex_class.superclass_index),
                              "superclass"))
        hierarchy += [(dex.type_name(i), "interface")
                      for i in dex_class.interface_type_indices]
        for name, role in hierarchy:
            if name in app_types or name in declared_classes:
                continue
            entry = link_blocking.setdefault(
                name, {"role": role, "required_by": []})
            if entry["role"] != role:
                entry["role"] = "both"
            entry["required_by"].append(requiring)

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
        "schema": 2,
        "link_blocking": {
            name: {"role": entry["role"],
                   "required_by": sorted(set(entry["required_by"]))}
            for name, entry in sorted(link_blocking.items())
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
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--repo", type=Path,
                        default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    sources = sorted(path for glob in DEFAULT_CATALOG_GLOBS
                     for path in arguments.repo.glob(glob))
    if not sources:
        parser.error("no catalog sources matched; check --repo")
    declared_classes, declared_members = parse_catalog_sources(sources)
    if arguments.self_test:
        expected_class = "Landroid/view/View;"
        expected_member = (expected_class, "setVisibility", "(I)V")
        if expected_class not in declared_classes or \
                expected_member not in declared_members:
            parser.error("builder catalog discovery self-test failed")
        print(json.dumps({"status": "passed", "sources": len(sources),
                          "classes": len(declared_classes),
                          "members": len(declared_members)}))
        return 0
    if arguments.apk is None:
        parser.error("--apk is required unless --self-test is used")
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
