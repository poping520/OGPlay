#!/usr/bin/env python3
"""Compare intrinsic own-member contracts with the pinned API19 ROM; never fix them."""
from __future__ import annotations

import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import api19_skeleton as skeleton

BASELINE = Path(__file__).with_name("framework_contract_baseline.json")
MASK = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x400
VISIBLE = 0x1 | 0x4
CATEGORIES = {
    "A1": "可覆写性",
    "A2": "其余修饰符",
    "B": "同族/构造器缺口",
    "C": "多余成员",
    "D": "层级",
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


def analyze(surface, rom):
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
            if method["origin"] not in ("handwritten", "skeleton", "bytecode"):
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
        if cls["superclass"] != parent:
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
                    # Skeleton fields can only come from ROM own declarations;
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
    return {
        "deviations": dict(sorted(deviations.items())),
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
        "| 包 | A1 | A2 | B | C | D | 合计 |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
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
        "## E 纯 Java intrinsic",
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
            method("m", "(I)V", 4, "skeleton", "unimplemented-ledger"),
            method("<init>", overridable=False),
            method("<init>", "(I)V", 1, "skeleton", "unimplemented-ledger", False),
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
    # Exercise the shared binary reader rather than a second format parser.
    assert skeleton.unpack(skeleton.pack(rom)) == rom
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
    for source in ("bootdex", "skeleton"):
        ignored["source"] = source
        ignored["descriptor"] = "Ltest/Ignored;"
        assert analyze(dict(schema=1, classes=[cls, ignored]), rom) == result
    no_family = copy.deepcopy(surface)
    for m in no_family["classes"][0]["methods"]:
        m["origin"] = "bytecode"
    assert not any(k.startswith("B|") for k in analyze(no_family, rom)["deviations"])
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
        "<init>", origin="skeleton", status="unimplemented-ledger", overridable=False
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
        better = copy.deepcopy(result)
        better["deviations"].pop("C|Ltest/C;|extra|I")
        changes = compare_baseline(json.loads(original), better)
        assert not changes["new"] and len(changes["removed"]) == 1
        assert save_baseline(path, better, "rom")
        assert not save_baseline(path, result, "rom")
    for bad in (b"", skeleton.pack(rom)[:-1], skeleton.pack(rom) + b"x"):
        try:
            skeleton.unpack(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("malformed skeleton accepted")
    assert all(cat + " " + name in render(result) for cat, name in CATEGORIES.items())
    print(
        "framework_contract self-test passed (A-D, own members, origins, monotone baseline, malformed input)"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--surface", type=Path, help="JSON from ogplay-cli dexvm dump-surface"
    )
    parser.add_argument(
        "--skeleton", type=Path, default=ROOT / "data/dexvm/api19-skeleton.bin"
    )
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
    if not args.surface:
        parser.error("--surface is required")
    raw = args.skeleton.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    result = analyze(
        json.loads(args.surface.read_text(encoding="utf-8")), skeleton.unpack(raw)
    )
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
