#!/usr/bin/env python3
"""Compare intrinsic own-member contracts with the pinned API19 ROM; never fix them.

Also owns the API19 evidence it compares against: own-member shapes read from the hash-pinned
ROM jars, and api19_platform_tables.json (ContextImpl service registrations and
System.initSystemProperties), regenerated from the local AOSP tree with --regenerate-tables.
"""
from __future__ import annotations

import argparse
from collections import Counter
import copy
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import dex_survey_lib as dex
from aosp_sources import api_dir, rom_dir

BASELINE = Path(__file__).with_name("framework_contract_baseline.json")
MASK = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x400
VISIBLE = 0x1 | 0x4
CATEGORIES = {
    "A1": "可覆写性",
    "A2": "其余修饰符",
    "B": "同族/构造器缺口",
    "C": "多余成员",
    "D": "层级",
    "E1": "未提供的注册服务",
    "E2": "缺失系统属性",
    "E3": "系统属性来源矛盾",
}
E_STATUS = "未测：select_rom_java.py 的纯 Java 准入判定耦合候选集、smali 和调用闭包，无独立可直接复用入口。"


def member_index(rows):
    result = {}
    for row in rows:
        key = row["name"], row["descriptor"]
        if key in result:
            raise ValueError("duplicate own member: " + str(key))
        if not isinstance(row["access_flags"], int):
            raise ValueError("invalid access_flags")
        result[key] = row
    return result


PLATFORM_TABLES = Path(__file__).with_name("api19_platform_tables.json")


# ---- API19 ROM own-member shapes ----

PINNED = {
    'core.jar': '60b6cdf3eded2fdf157c6166402477e4f72ade215b334817439960522c0e64ee',
    'framework.jar': '422e232e121425268f04044f5349072ce42d7b4403267d21569e8ce70d07bc4b',
    'framework2.jar': '5866d4e62e5fe44ffaf6ff441b6c361baeb1f8b91e30bb555481ef13ef837973',
    'telephony-common.jar': 'b1e4f98857dd07d9fa7bc1d6c2d6cdb49d41adc2973533db3982ef0d5348a908',
    'ext.jar': 'e95d35989f0e1b438b3b6fea2777a798a0398f457a32875cdadfe3e2573a4ca2',
}


def clinit_writes(data, code, catalog):
    units = struct.unpack_from('<%dH' % code.insns_size, data, code.insns_offset)
    pc = 0
    writes = set()
    while pc < len(units):
        op = units[pc] & 255
        if op == 0 and units[pc] >> 8:
            ident = units[pc]
            if ident == 0x100:
                width = 4 + 2 * units[pc + 1]
            elif ident == 0x200:
                width = 2 + 4 * units[pc + 1]
            else:
                width = 4 + (units[pc + 1] * (units[pc + 2] | units[pc + 3] << 16) + 1) // 2
        else:
            if 0x67 <= op <= 0x6d:
                writes.add(units[pc + 1])
            width = int(catalog[op]['format'][0])
        pc += width
    if pc != len(units):
        raise ValueError('invalid clinit instruction width')
    return writes


def load_rom(directory=None):
    """Return {descriptor: [super, interfaces, flags, methods, fields]} from the pinned jars.

    methods: [name, descriptor, flags]; fields: [name, descriptor, flags, kind, value] where
    kind 1 is a numeric and 2 a String static final constant no clinit writes.
    """
    directory = rom_dir(19) if directory is None else directory
    records = {}
    catalog = dex.load_catalog()
    for jar, digest in PINNED.items():
        raw = (Path(directory) / jar).read_bytes()
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError('pinned SHA256 mismatch: ' + jar)
        data = zipfile.ZipFile(io.BytesIO(raw)).read('classes.dex')
        image = dex.parse_dex(data)
        for cls in image.classes:
            owner = image.type_name(cls.type_index)
            if owner in records:
                raise ValueError('duplicate ROM class: ' + owner)
            methods = []
            writes = set()
            for method in cls.direct_methods + cls.virtual_methods:
                _, name, desc = image.method_signature(method.method_index)
                methods.append([name, desc, method.access_flags])
                if name == '<clinit>' and method.code:
                    writes |= clinit_writes(data, method.code, catalog)
            values = dex.static_values(data, image, cls)
            fields = []
            for i, field in enumerate(cls.static_fields + cls.instance_fields):
                _, type_index, name_index = image.field_ids[field.field_index]
                desc = image.type_name(type_index)
                kind = 0
                value = None
                if (i < len(cls.static_fields) and field.access_flags & 0x18 == 0x18
                        and field.field_index not in writes and i < len(values)):
                    encoded = values[i]
                    if desc == 'Ljava/lang/String;' and isinstance(encoded, str):
                        kind, value = 2, encoded
                    elif desc in 'ZBSCIJFD' and len(desc) == 1 and isinstance(encoded, (int, float)):
                        kind = 1
                        if desc in 'FD':
                            bits = struct.pack('<f' if desc == 'F' else '<d', encoded)
                            value = str(int.from_bytes(bits, 'little', signed=True))
                        else:
                            value = str(int(encoded))
                fields.append([image.strings[name_index], desc, field.access_flags, kind, value])
            parent = image.type_name(cls.superclass_index) if cls.superclass_index is not None else None
            interfaces = [image.type_name(t) for t in cls.interface_type_indices]
            records[owner] = [parent, interfaces, cls.access_flags, sorted(methods), sorted(fields)]
    return records


# ---- API19 platform tables evidence (generation only reads the local AOSP tree) ----

TABLE_SOURCES = {
    'context': 'frameworks/base/core/java/android/content/Context.java',
    'registry': 'frameworks/base/core/java/android/app/ContextImpl.java',
    'system': 'libcore/luni/src/main/java/java/lang/System.java',
    'binding': 'frameworks/base/core/java/android/app/ActivityThread.java',
}


def extract_platform_tables(root):
    texts = {key: (root / path).read_text(encoding='utf-8') for key, path in TABLE_SOURCES.items()}
    constants = dict(re.findall(r'\bString\s+(\w+)\s*=\s*"([^"]+)"', texts['context']))
    registrations = re.findall(r'\bregisterService\(\s*([A-Z][A-Z0-9_]+)\s*,', texts['registry'])
    if len(registrations) != len(set(registrations)):
        raise ValueError('duplicate service registration')
    services = {constants[name]: name for name in registrations}
    body = texts['system'].split('private static void initSystemProperties() {', 1)[1].split('systemProperties = p;', 1)[0]
    expressions = dict(re.findall(r'p\.put\("([^"]+)",\s*([^;]+)\);', body))
    properties = {}
    variables = {'projectName': 'The Android Project', 'projectUrl': 'http://www.android.com/'}
    for key, expression in expressions.items():
        entry = {'expression': expression}
        if expression.startswith('"'):
            entry['value'] = json.loads(expression)
        elif expression in variables:
            entry['value'] = variables[expression]
        elif expression.startswith('info.'):
            entry['source'] = 'uname.' + expression[5:]
        elif expression.startswith('passwd.'):
            entry['source'] = 'passwd.' + expression[7:]
        elif expression.startswith('ICU.'):
            entry['source'] = expression
        elif expression == 'javaHome':
            entry.update(source='getenv.JAVA_HOME', fallback='/system')
        elif expression == 'ldLibraryPath':
            entry.update(source='getenv.LD_LIBRARY_PATH', conditional=True)
        else:
            entry['source'] = expression
        properties[key] = entry
    # Keep the ActivityThread override separately: /tmp is correct before binding.
    if 'System.setProperty("java.io.tmpdir", cacheDir.getAbsolutePath())' not in texts['binding']:
        raise ValueError('missing API19 cache binding rule')
    properties['java.io.tmpdir']['application_binding'] = 'Context.getCacheDir().getAbsolutePath()'
    if len(services) != 44 or len(properties) != 37:
        raise ValueError(f'unexpected pinned source inventory: {len(services)} services, {len(properties)} properties')
    return {
        'schema': 1, 'api': 19, 'reference': 'AOSP android-4.4.4_r2.0.1',
        'sources': {key: {'path': path, 'sha256': hashlib.sha256((root/path).read_bytes()).hexdigest()} for key, path in TABLE_SOURCES.items()},
        'system_services': dict(sorted(services.items())),
        'system_properties': dict(sorted(properties.items())),
    }


def analyze_platform_tables(surface, reference):
    tables = surface.get("platform_tables")
    if not isinstance(tables, dict):
        raise ValueError("missing platform_tables in dump-surface")
    services, properties = tables["system_services"], tables["system_properties"]
    if not isinstance(services, dict) or not isinstance(properties, dict):
        raise ValueError("invalid platform_tables")
    if any(value not in ("provided", "unsupported") for value in services.values()):
        raise ValueError("invalid system service status")
    result = {}
    for name in reference["system_services"]:
        if services.get(name) != "provided":
            result[f"E1|@system_services|{name}|"] = ["ROM registers service; OGPlay does not provide it"]
    for key, rule in reference["system_properties"].items():
        if key not in properties:
            result[f"E2|@system_properties|{key}|"] = ["ROM initSystemProperties key is missing"]
            continue
        actual = properties[key]
        if not isinstance(actual, str):
            raise ValueError("invalid system property value/source")
        expected = rule.get("value", "source: " + rule.get("source", ""))
        # Binding is not represented by this pre-application defaults dump.
        if actual != expected and not ("fallback" in rule and actual == rule["fallback"]):
            result[f"E3|@system_properties|{key}|"] = [f"ROM rule {expected!r}, runtime {actual!r}"]
    return result


def analyze(surface, rom, platform_reference=None):
    if surface.get("startup_link_error"):
        raise ValueError("dump-surface startup linking failed: " + surface["startup_link_error"])
    if surface["schema"] != 1 or not surface["classes"]:
        raise ValueError("expected nonempty dump-surface schema 1")
    deviations = {}
    owners = set()
    missing_rom = []
    seen = set()

    def add(category, owner, member, desc, detail):
        key = "|".join((category, owner, member, desc))
        deviations.setdefault(key, []).append(detail)

    for cls in surface["classes"]:
        owner = cls["descriptor"]
        if owner in seen:
            raise ValueError("duplicate surface class: " + owner)
        seen.add(owner)
        if cls["source"] != "intrinsic":
            continue
        methods = member_index(cls["methods"])
        fields = member_index(cls["fields"])
        for method in methods.values():
            if method["origin"] not in ("handwritten", "failure", "bytecode"):
                raise ValueError("invalid method origin")
            if type(method["overridable"]) is not bool:
                raise ValueError("invalid method overridable")
            if method["status"] not in ("implemented", "unimplemented-ledger"):
                raise ValueError("invalid method status")
        record = rom.get(owner)
        if record is None:
            missing_rom.append(owner)
            continue
        owners.add(owner)
        parent, interfaces, flags, rom_methods, rom_fields = record
        class_final_added = not flags & 0x10 and bool(cls["access_flags"] & 0x10)
        if class_final_added:
            add("A1", owner, "@class", "", "ROM non-final class, runtime final")
        class_mask = MASK & ~0x10 if class_final_added else MASK
        if (cls["access_flags"] & class_mask) != (flags & class_mask):
            add(
                "A2",
                owner,
                "@class",
                "",
                f'class flags runtime=0x{cls["access_flags"] & class_mask:x}, ROM=0x{flags & class_mask:x}',
            )
        # DEX encodes Object as an interface's superclass; reflection reports null.
        interface_object_null = bool(flags & 0x200) and {
            cls["superclass"], parent
        } == {None, "Ljava/lang/Object;"}
        if cls["superclass"] != parent and not interface_object_null:
            add(
                "D",
                owner,
                "@superclass",
                "",
                f'runtime={cls["superclass"]}, ROM={parent}',
            )
        if set(cls["interfaces"]) != set(interfaces):
            add(
                "D",
                owner,
                "@interfaces",
                "",
                f'runtime={sorted(cls["interfaces"])}, ROM={sorted(interfaces)}',
            )
        families = {
            m["name"]
            for m in methods.values()
            if m["origin"] == "handwritten" and m["status"] == "implemented"
        }
        for kind, actual, expected_rows in (
            ("method", methods, rom_methods),
            ("field", fields, rom_fields),
        ):
            expected = {(r[0], r[1]): r[2] for r in expected_rows}
            for (name, desc), member in sorted(actual.items()):
                rom_flags = expected.get((name, desc))
                if rom_flags is None:
                    # Failure fields can only come from ROM own declarations;
                    # thus a field absent in ROM on an intrinsic class is handwritten.
                    handwritten = kind == "field" or member["origin"] == "handwritten"
                    if handwritten and member["access_flags"] & VISIBLE:
                        add(
                            "C",
                            owner,
                            name,
                            desc,
                            kind + " absent from ROM own members",
                        )
                    continue
                can_override = (
                    kind == "method"
                    and not flags & 0x10
                    and name not in ("<init>", "<clinit>")
                    and not rom_flags & (0x2 | 0x8 | 0x10)
                )
                final_added = can_override and bool(member["access_flags"] & 0x10)
                if can_override and (not member["overridable"] or final_added):
                    add(
                        "A1",
                        owner,
                        name,
                        desc,
                        "ROM overridable, runtime overridable=false or final",
                    )
                modifier_mask = MASK & ~0x10 if final_added else MASK
                actual_flags = member["access_flags"] & modifier_mask
                if actual_flags != rom_flags & modifier_mask:
                    add(
                        "A2",
                        owner,
                        name,
                        desc,
                        f"{kind} flags runtime=0x{actual_flags:x}, ROM=0x{rom_flags & modifier_mask:x}",
                    )
                if kind == "method":
                    if (
                        (name in families or (name == "<init>" and bool(families)))
                        and rom_flags & VISIBLE
                        and member["status"] == "unimplemented-ledger"
                    ):
                        add(
                            "B",
                            owner,
                            name,
                            desc,
                            "ROM public/protected overload or constructor beside handwritten implemented member is unimplemented-ledger",
                        )
    if not owners and not missing_rom:
        raise ValueError("no intrinsic classes in surface")
    if platform_reference is not None:
        deviations.update(analyze_platform_tables(surface, platform_reference))
    return {
        "deviations": dict(sorted(deviations.items())),
        # ADR-0154: a class remains accounted for when its owner moves to BootDex.
        "bootdex_classes": sorted(c["descriptor"] for c in surface["classes"]
                                  if c["source"] == "bootdex" and c["descriptor"] in rom),
        "intrinsic_classes": sorted(owners),
        "private_classes": sorted(missing_rom),
        "E": E_STATUS,
    }


def compare_baseline(baseline, current):
    before, after = set(baseline["keys"]), set(current["deviations"])
    def categories(keys):
        result = {}
        for key in keys:
            category, member = key.split("|", 1)
            result.setdefault(member, set()).add(category)
        return result

    old, new = categories(before), categories(after)
    return {
        "new": sorted(key for key in after - before if key.split("|", 1)[1] not in old),
        "removed": sorted(before - after),
        "reclassified": [
            {"member": member, "before": sorted(old[member]), "after": sorted(new[member])}
            for member in sorted(old.keys() & new.keys())
            if old[member] != new[member]
        ],
        "missing_classes": sorted(
            set(baseline["intrinsic_classes"]) - set(current["intrinsic_classes"])
            - set(current.get("bootdex_classes", []))
        ),
    }


def save_baseline(path, current, rom_sha256):
    if path.exists():
        baseline = json.loads(path.read_text(encoding="utf-8"))
        changes = compare_baseline(baseline, current)
        if (
            changes["new"]
            or changes["missing_classes"]
            or baseline["rom_sha256"] != rom_sha256
        ):
            return False
    data = {
        "schema": 1,
        "rom_sha256": rom_sha256,
        "intrinsic_classes": current["intrinsic_classes"],
        "keys": sorted(current["deviations"]),
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return True


def render(result):
    rows = result["deviations"]
    counts = Counter(key.split("|")[0] for key in rows)
    text = [
        "# Framework 契约差分（API19 ROM）",
        "",
        f'检查有 ROM 判据的 intrinsic 类：{len(result["intrinsic_classes"])}；偏差键合计：{len(rows)}。',
        "",
        "| 类别 | 偏差数 |",
        "| --- | ---: |",
    ]
    text += [f"| {cat} {name} | {counts[cat]} |" for cat, name in CATEGORIES.items()]
    text += [
        "",
        "## 按包前 15 名",
        "",
        "| 包 | " + " | ".join(CATEGORIES) + " | 合计 |",
        "| --- | " + " | ".join("---:" for _ in CATEGORIES) + " | ---: |",
    ]
    packages = {}
    for key in rows:
        cat, owner, _, _ = key.split("|")
        package = owner[1:-1].rsplit("/", 1)[0] if "/" in owner else "(default)"
        packages.setdefault(package, Counter())[cat] += 1
    for package, counts in sorted(
        packages.items(), key=lambda item: (-sum(item[1].values()), item[0])
    )[:15]:
        text.append(
            "| "
            + package
            + " | "
            + " | ".join(str(counts[c]) for c in CATEGORIES)
            + f" | {sum(counts.values())} |"
        )
    for cat, name in CATEGORIES.items():
        text += ["", f"## {cat} {name}（最多 10 例）", ""]
        samples = [key for key in rows if key.startswith(cat + "|")][:10]
        text += [f"- `{key}`：" + "; ".join(rows[key]) for key in samples] or ["无。"]
    text += [
        "",
        "## 纯 Java intrinsic（独立信息项，非 E 类）",
        "",
        result["E"],
        "",
        "## OGPlay 私有类",
        "",
        f'ROM 无对应类，不进偏差基线：{len(result["private_classes"])}。',
    ]
    text += ["- `" + owner + "`" for owner in result["private_classes"]]
    return "\n".join(text) + "\n"


def self_test():
    reference = {"system_services": {"display": "DISPLAY_SERVICE", "account": "ACCOUNT_SERVICE"},
                 "system_properties": {"os.name": {"source": "uname.sysname"},
                                       "java.version": {"value": "0"}, "file.encoding": {"value": "UTF-8"}}}
    tables = {"platform_tables": {"system_services": {"display": "provided", "account": "unsupported"},
                                  "system_properties": {"os.name": "source: uname.sysname", "java.version": "17"}}}
    assert set(analyze_platform_tables(tables, reference)) == {
        "E1|@system_services|account|", "E2|@system_properties|file.encoding|", "E3|@system_properties|java.version|"}
    tables["platform_tables"]["system_properties"]["os.name"] = "Darwin"
    assert "E3|@system_properties|os.name|" in analyze_platform_tables(tables, reference)
    try:
        analyze_platform_tables({}, reference)
    except ValueError:
        pass
    else:
        raise AssertionError("missing platform inventory accepted")
    def method(
        name,
        desc="()V",
        flags=1,
        origin="handwritten",
        status="implemented",
        overridable=True,
    ):
        return dict(
            name=name,
            descriptor=desc,
            access_flags=flags,
            origin=origin,
            status=status,
            overridable=overridable,
        )

    owner = "Ltest/C;"
    rom = {
        owner: [
            "Ljava/lang/Object;",
            ["Ltest/I;"],
            1,
            [
                ["m", "()V", 1],
                ["m", "(I)V", 4],
                ["<init>", "()V", 1],
                ["<init>", "(I)V", 1],
                ["privateMethod", "()V", 2],
                ["staticMethod", "()V", 9],
                ["finalMethod", "()V", 17],
            ],
            [["value", "I", 1, 0, None]],
        ]
    }
    cls = dict(
        descriptor=owner,
        source="intrinsic",
        access_flags=17,
        superclass="Ltest/Wrong;",
        interfaces=[],
        methods=[
            method("m", flags=17, overridable=False),
            method("m", "(I)V", 4, "failure", "unimplemented-ledger"),
            method("<init>", overridable=False),
            method("<init>", "(I)V", 1, "failure", "unimplemented-ledger", False),
            method("extra"),
            method("hidden", flags=2),
            method("privateMethod", flags=2, overridable=False),
            method("staticMethod", flags=9, overridable=False),
            method("finalMethod", flags=17, overridable=False),
        ],
        fields=[
            dict(name="value", descriptor="I", access_flags=9),
            dict(name="extra", descriptor="I", access_flags=4),
        ],
    )
    surface = dict(schema=1, classes=[cls])
    broken_startup = dict(surface, startup_link_error="unresolved hierarchy")
    try:
        analyze(broken_startup, rom)
    except ValueError as error:
        assert "startup linking failed" in str(error)
    else:
        raise AssertionError("unlinked surface accepted")
    result = analyze(surface, rom)
    expected = {
        "A1|Ltest/C;|@class|",
        "A1|Ltest/C;|m|()V",
        "A2|Ltest/C;|value|I",
        "B|Ltest/C;|m|(I)V",
        "B|Ltest/C;|<init>|(I)V",
        "C|Ltest/C;|extra|()V",
        "C|Ltest/C;|extra|I",
        "D|Ltest/C;|@superclass|",
        "D|Ltest/C;|@interfaces|",
    }
    assert set(result["deviations"]) == expected
    assert len(result["deviations"]["A1|Ltest/C;|m|()V"]) == 1
    ignored = copy.deepcopy(cls)
    for source in ("bootdex",):
        ignored["source"] = source
        ignored["descriptor"] = "Ltest/Ignored;"
        assert analyze(dict(schema=1, classes=[cls, ignored]), rom) == result
    no_family = copy.deepcopy(surface)
    for m in no_family["classes"][0]["methods"]:
        m["origin"] = "bytecode"
    assert not any(k.startswith("B|") for k in analyze(no_family, rom)["deviations"])
    # Only null/Object on an interface is equivalent, in either direction.
    interface = dict(descriptor="Ltest/I;", source="intrinsic", access_flags=0x601,
                     superclass=None, interfaces=[], methods=[], fields=[])
    interface_rom = {"Ltest/I;": ["Ljava/lang/Object;", [], 0x601, [], []]}
    interface_surface = dict(schema=1, classes=[interface])
    assert not analyze(interface_surface, interface_rom)["deviations"]
    interface["superclass"] = "Ljava/lang/Object;"
    interface_rom["Ltest/I;"][0] = None
    assert not analyze(interface_surface, interface_rom)["deviations"]
    interface["superclass"] = "Ltest/Other;"
    assert "D|Ltest/I;|@superclass|" in analyze(interface_surface, interface_rom)["deviations"]
    interface["superclass"] = None
    interface["access_flags"] = interface_rom["Ltest/I;"][2] = 1
    interface_rom["Ltest/I;"][0] = "Ljava/lang/Object;"
    assert "D|Ltest/I;|@superclass|" in analyze(interface_surface, interface_rom)["deviations"]
    # Overridability alone must count, without a final flag mismatch.
    only_override = copy.deepcopy(surface)
    only_override["classes"][0]["methods"][0]["access_flags"] = 1
    assert "A1|Ltest/C;|m|()V" in analyze(only_override, rom)["deviations"]
    # Final alone blocks overriding even if overridable=true; do not double-count it as A2.
    only_final = copy.deepcopy(surface)
    only_final["classes"][0]["methods"][0]["overridable"] = True
    split = analyze(only_final, rom)["deviations"]
    assert "A1|Ltest/C;|m|()V" in split and "A2|Ltest/C;|m|()V" not in split
    # A final ROM class cannot be subclassed; method flags still belong to A2.
    final_class_rom = copy.deepcopy(rom)
    final_class_rom[owner][2] |= 0x10
    split = analyze(surface, final_class_rom)["deviations"]
    assert not any(key.startswith("A1|") for key in split)
    assert "A2|Ltest/C;|m|()V" in split
    both = copy.deepcopy(surface)
    both["classes"][0]["methods"][0]["access_flags"] = 20
    both["classes"][0]["access_flags"] = 20
    split = analyze(both, rom)["deviations"]
    assert all(
        k in split
        for k in (
            "A1|Ltest/C;|m|()V",
            "A2|Ltest/C;|m|()V",
            "A1|Ltest/C;|@class|",
            "A2|Ltest/C;|@class|",
        )
    )
    # No handwritten constructor: another implemented handwritten method suffices.
    ctor_gap = copy.deepcopy(surface)
    ctor_gap["classes"][0]["methods"][2] = method(
        "<init>", origin="failure", status="unimplemented-ledger", overridable=False
    )
    split = analyze(ctor_gap, rom)["deviations"]
    assert "B|Ltest/C;|<init>|()V" in split and "B|Ltest/C;|<init>|(I)V" in split
    # Private helpers, including their public methods/fields, never enter the baseline.
    private = copy.deepcopy(cls)
    private["descriptor"] = "Ltest/Private;"
    with_private = analyze(dict(schema=1, classes=[cls, private]), rom)
    assert with_private["deviations"] == result["deviations"]
    assert with_private["intrinsic_classes"] == result["intrinsic_classes"]
    assert with_private["private_classes"] == ["Ltest/Private;"]
    reordered = copy.deepcopy(surface)
    reordered["classes"][0]["methods"].reverse()
    assert analyze(reordered, rom) == result
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp) / "baseline.json"
        assert save_baseline(path, result, "rom")
        original = path.read_bytes()
        reclassified = copy.deepcopy(result)
        reclassified["deviations"]["A2|Ltest/C;|m|()V"] = reclassified["deviations"].pop("A1|Ltest/C;|m|()V")
        changes = compare_baseline(json.loads(original), reclassified)
        assert not changes["new"] and len(changes["reclassified"]) == 1
        assert save_baseline(path, reclassified, "rom")
        path.write_bytes(original)
        assert save_baseline(path, result, "rom")
        assert not save_baseline(path, result, "other-rom")
        worse = copy.deepcopy(result)
        worse["deviations"]["C|Ltest/C;|new|()V"] = ["new"]
        assert not save_baseline(path, worse, "rom")
        lost = copy.deepcopy(result)
        lost["intrinsic_classes"] = []
        assert not save_baseline(path, lost, "rom")
        assert path.read_bytes() == original
        migrated = copy.deepcopy(lost)
        migrated["bootdex_classes"] = list(result["intrinsic_classes"])
        assert not compare_baseline(json.loads(original), migrated)["missing_classes"]
        assert save_baseline(path, migrated, "rom")
        path.write_bytes(original)
        # Fallback or absent export must still fail (not a migration).
        fallback = copy.deepcopy(lost)
        fallback["bootdex_classes"] = []
        assert compare_baseline(json.loads(original), fallback)["missing_classes"]
        assert not save_baseline(path, fallback, "rom")
        better = copy.deepcopy(result)
        better["deviations"].pop("C|Ltest/C;|extra|I")
        changes = compare_baseline(json.loads(original), better)
        assert not changes["new"] and len(changes["removed"]) == 1
        assert save_baseline(path, better, "rom")
        assert not save_baseline(path, result, "rom")
    assert all(cat + " " + name in render(result) for cat, name in CATEGORIES.items())
    print(
        "framework_contract self-test passed (A-D, own members, origins, monotone baseline)"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--surface", type=Path, help="JSON from ogplay-cli dexvm dump-surface"
    )
    parser.add_argument("--rom", type=Path, default=rom_dir(19))
    parser.add_argument("--aosp", type=Path, default=api_dir(19),
                        help="AOSP source root for --regenerate-tables")
    parser.add_argument("--regenerate-tables", action="store_true",
                        help="rewrite api19_platform_tables.json from --aosp and exit")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--report", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check-baseline", action="store_true")
    mode.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.regenerate_tables:
        tables = extract_platform_tables(args.aosp)
        PLATFORM_TABLES.write_text(json.dumps(tables, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"Extracted {len(tables['system_services'])} services and {len(tables['system_properties'])} properties")
        return 0
    if not args.surface:
        parser.error("--surface is required")
    result = analyze(
        json.loads(args.surface.read_text(encoding="utf-8")), load_rom(args.rom),
        json.loads(PLATFORM_TABLES.read_text(encoding="utf-8")))
    # Bind the pinned jars and frozen platform evidence into the ROM identity.
    digest = hashlib.sha256(
        json.dumps(PINNED, sort_keys=True).encode() + PLATFORM_TABLES.read_bytes()
    ).hexdigest()
    report = render(result)
    print(report, end="")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(report, encoding="utf-8")
    if args.check_baseline or args.update_baseline:
        if args.baseline.exists():
            baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
            if baseline["schema"] != 1 or baseline["rom_sha256"] != digest:
                raise ValueError("baseline schema / ROM hash mismatch")
            changes = compare_baseline(baseline, result)
            print("Baseline changes: " + json.dumps(changes, ensure_ascii=False))
            print(f'Reclassified members: {len(changes["reclassified"])}')
            if changes["new"] or changes["missing_classes"]:
                return 1
            if changes["removed"]:
                print("偏差减少；请执行 --update-baseline。")
        elif not args.update_baseline:
            raise ValueError("baseline missing; initialize with --update-baseline")
        if args.update_baseline and not save_baseline(args.baseline, result, digest):
            return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, KeyError, TypeError, OSError) as error:
        print("framework_contract: " + str(error), file=sys.stderr)
        raise SystemExit(2)
