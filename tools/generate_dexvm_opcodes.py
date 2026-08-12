#!/usr/bin/env python3
"""Declarative Dalvik dex-035 opcode catalog generator and AOSP verifier.

The JSON catalog (data/dexvm/dalvik_opcodes.json) is machine-derived from the
pinned AOSP baseline's opcode-gen/bytecode.txt, never hand-copied. This tool:

  --bootstrap    derive the catalog from the baseline (used once, and to
                 regenerate after a deliberate baseline change)
  --verify-aosp  prove the catalog equals bytecode.txt facts, the
                 libdex/DexOpcodes.h enum and the libdex/InstrUtils.cpp
                 width table, entry by entry
  --output       emit the deterministic C++ decode table
  --check        with --output, compare instead of write
  --self-test    internal consistency (count, holes, formats, widths)

See docs/design/dexvm/02-architecture.md §4 and 07-aosp-reference.md §2.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

CATALOG_SCHEMA = "dexvm-opcodes-v1"
EXPECTED_STANDARD_COUNT = 218

# dex 035 standard instruction formats (docs/design/dexvm/02-architecture.md §4).
STANDARD_FORMATS = (
    "10x", "12x", "22x", "32x", "11n", "21s", "21h", "31i", "51l", "11x",
    "10t", "20t", "30t", "21t", "22t", "21c", "22c", "31c", "23x", "22b",
    "22s", "31t", "35c", "3rc",
)

# Explicit dex-035 rejection set: unused holes plus the odex-only range.
REJECTED_OPCODES = tuple(
    list(range(0x3E, 0x44)) + [0x73] + [0x79, 0x7A] + list(range(0xE3, 0x100))
)

INDEX_TYPES = ("none", "varies", "type-ref", "string-ref", "method-ref",
               "field-ref")
FLAG_NAMES = ("branch", "continue", "switch", "throw", "return", "invoke")

OP_LINE = re.compile(
    r"^op\s+([0-9a-f]{2})\s+(\S+)\s+(\S+)\s+([yn])\s+(\S+)\s+(\S+)\s*$")


def parse_bytecode_txt(path: Path) -> dict[int, dict]:
    """Extract non-optimized opcode facts from AOSP bytecode.txt."""
    ops: dict[int, dict] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = OP_LINE.match(line.strip())
        if not match:
            continue
        value = int(match.group(1), 16)
        flags = match.group(6).split("|")
        if "optimized" in flags:
            continue
        ops[value] = {
            "opcode": value,
            "name": match.group(2),
            "format": match.group(3),
            "has_result": match.group(4) == "y",
            "index_type": match.group(5),
            "flags": sorted(flag for flag in flags if flag != "optimized"),
        }
    return ops


def enum_name(name: str) -> str:
    return "OP_" + name.upper().replace("-", "_").replace("/", "_")


def parse_dex_opcodes_h(path: Path) -> dict[int, str]:
    """Extract OP_* enum entries (name by value) from libdex/DexOpcodes.h."""
    entries: dict[int, str] = {}
    pattern = re.compile(r"^\s*(OP_\w+)\s*=\s*0x([0-9a-f]+),\s*$")
    for line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            entries[int(match.group(2), 16)] = match.group(1)
    return entries


def parse_width_table(path: Path) -> list[int]:
    text = path.read_text(encoding="utf-8")
    begin = text.index("BEGIN(libdex-widths)")
    end = text.index("END(libdex-widths)")
    numbers = re.findall(r"\d+", text[begin:end].split("\n", 1)[1])
    return [int(number) for number in numbers]


def build_catalog(baseline: Path) -> dict:
    ops = parse_bytecode_txt(baseline / "opcode-gen" / "bytecode.txt")
    return {
        "schema": CATALOG_SCHEMA,
        "source": ("AOSP platform/dalvik android-4.4.4_r2 "
                   "opcode-gen/bytecode.txt (machine-derived)"),
        "rejected_opcodes": list(REJECTED_OPCODES),
        "opcodes": [ops[value] for value in sorted(ops)],
    }


def load_catalog(path: Path) -> dict:
    catalog = json.loads(path.read_text(encoding="utf-8"))
    if catalog.get("schema") != CATALOG_SCHEMA:
        raise ValueError(f"unexpected catalog schema: {catalog.get('schema')}")
    return catalog


def validate_catalog(catalog: dict) -> list[str]:
    errors: list[str] = []
    opcodes = catalog["opcodes"]
    values = [entry["opcode"] for entry in opcodes]
    if len(opcodes) != EXPECTED_STANDARD_COUNT:
        errors.append(
            f"catalog has {len(opcodes)} opcodes, expected "
            f"{EXPECTED_STANDARD_COUNT}")
    if values != sorted(values) or len(set(values)) != len(values):
        errors.append("catalog opcodes are not strictly ascending")
    rejected = set(catalog["rejected_opcodes"])
    if rejected != set(REJECTED_OPCODES):
        errors.append("catalog rejected set does not match design holes")
    if rejected & set(values):
        errors.append("catalog defines opcodes inside the rejected set")
    if set(values) | rejected != set(range(0x100)):
        errors.append("catalog + rejected does not cover 0x00..0xff")
    for entry in opcodes:
        if entry["format"] not in STANDARD_FORMATS:
            errors.append(
                f"opcode {entry['opcode']:#04x} uses non-standard format "
                f"{entry['format']}")
        if entry["index_type"] not in INDEX_TYPES:
            errors.append(
                f"opcode {entry['opcode']:#04x} has unknown index type "
                f"{entry['index_type']}")
        for flag in entry["flags"]:
            if flag not in FLAG_NAMES:
                errors.append(
                    f"opcode {entry['opcode']:#04x} has unknown flag {flag}")
    return errors


def verify_against_aosp(catalog: dict, baseline: Path) -> list[str]:
    errors = validate_catalog(catalog)
    ops = parse_bytecode_txt(baseline / "opcode-gen" / "bytecode.txt")
    by_value = {entry["opcode"]: entry for entry in catalog["opcodes"]}

    if set(ops) != set(by_value):
        missing = sorted(set(ops) - set(by_value))
        extra = sorted(set(by_value) - set(ops))
        errors.append(
            f"opcode set mismatch: missing={missing} extra={extra}")
    for value in sorted(set(ops) & set(by_value)):
        for key in ("name", "format", "has_result", "index_type", "flags"):
            if ops[value][key] != by_value[value][key]:
                errors.append(
                    f"opcode {value:#04x} {key}: catalog "
                    f"{by_value[value][key]!r} != bytecode.txt "
                    f"{ops[value][key]!r}")

    enum_entries = parse_dex_opcodes_h(baseline / "libdex" / "DexOpcodes.h")
    for value, entry in sorted(by_value.items()):
        expected = enum_name(entry["name"])
        actual = enum_entries.get(value)
        if actual != expected:
            errors.append(
                f"opcode {value:#04x} enum: DexOpcodes.h {actual!r} != "
                f"derived {expected!r}")

    widths = parse_width_table(baseline / "libdex" / "InstrUtils.cpp")
    if len(widths) != 0x100:
        errors.append(f"width table has {len(widths)} entries, expected 256")
    else:
        for value, entry in sorted(by_value.items()):
            derived = int(entry["format"][0])
            if widths[value] != derived:
                errors.append(
                    f"opcode {value:#04x} width: InstrUtils {widths[value]} "
                    f"!= format-derived {derived}")
    return errors


def cpp_identifier(name: str) -> str:
    parts = re.split(r"[-/]", name)
    return "".join(part.capitalize() if not part.isdigit() else part
                   for part in parts)


def generate_cpp(catalog: dict) -> str:
    lines: list[str] = []
    lines.append("// GENERATED by tools/generate_dexvm_opcodes.py; "
                 "do not edit.")
    lines.append("// Source of truth: data/dexvm/dalvik_opcodes.json "
                 "(machine-compared with the")
    lines.append("// pinned AOSP baseline; see docs/design/dexvm/"
                 "07-aosp-reference.md).")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <array>")
    lines.append("#include <cstdint>")
    lines.append("#include <string_view>")
    lines.append("")
    lines.append("namespace ogplay::runtime::dexvm::generated {")
    lines.append("")
    lines.append("enum class DexInstructionFormat : std::uint8_t {")
    for format_name in STANDARD_FORMATS:
        lines.append(f"    k{format_name},")
    lines.append("};")
    lines.append("")
    lines.append("enum class DexIndexType : std::uint8_t {")
    lines.append("    none,")
    lines.append("    varies,")
    lines.append("    type_ref,")
    lines.append("    string_ref,")
    lines.append("    method_ref,")
    lines.append("    field_ref,")
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr std::uint8_t kFlagBranch = 1u << 0;")
    lines.append("inline constexpr std::uint8_t kFlagContinue = 1u << 1;")
    lines.append("inline constexpr std::uint8_t kFlagSwitch = 1u << 2;")
    lines.append("inline constexpr std::uint8_t kFlagThrow = 1u << 3;")
    lines.append("inline constexpr std::uint8_t kFlagReturn = 1u << 4;")
    lines.append("inline constexpr std::uint8_t kFlagInvoke = 1u << 5;")
    lines.append("")
    lines.append("enum class DexOpcode : std::uint8_t {")
    for entry in catalog["opcodes"]:
        lines.append(
            f"    k{cpp_identifier(entry['name'])} = "
            f"0x{entry['opcode']:02x},")
    lines.append("};")
    lines.append("")
    lines.append("struct DexOpcodeInfo final {")
    lines.append("    std::string_view name;")
    lines.append("    DexInstructionFormat format{DexInstructionFormat::k10x};")
    lines.append("    DexIndexType index_type{DexIndexType::none};")
    lines.append("    std::uint8_t width{};  // code units; 0 = rejected")
    lines.append("    std::uint8_t flags{};")
    lines.append("    bool has_result{};")
    lines.append("    bool defined{};")
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr std::size_t kDexOpcodeTableSize = 256;")
    lines.append(
        f"inline constexpr std::size_t kDexDefinedOpcodeCount = "
        f"{len(catalog['opcodes'])};")
    lines.append("")
    lines.append("inline constexpr std::array<DexOpcodeInfo, "
                 "kDexOpcodeTableSize> kDexOpcodeTable = {{")
    by_value = {entry["opcode"]: entry for entry in catalog["opcodes"]}
    flag_bits = {"branch": 0, "continue": 1, "switch": 2, "throw": 3,
                 "return": 4, "invoke": 5}
    index_map = {"none": "none", "varies": "varies", "type-ref": "type_ref",
                 "string-ref": "string_ref", "method-ref": "method_ref",
                 "field-ref": "field_ref"}
    for value in range(0x100):
        entry = by_value.get(value)
        if entry is None:
            lines.append(
                f"    DexOpcodeInfo{{}},  // 0x{value:02x} rejected")
            continue
        flags = 0
        for flag in entry["flags"]:
            flags |= 1 << flag_bits[flag]
        lines.append(
            "    DexOpcodeInfo{"
            f"\"{entry['name']}\", "
            f"DexInstructionFormat::k{entry['format']}, "
            f"DexIndexType::{index_map[entry['index_type']]}, "
            f"{int(entry['format'][0])}, "
            f"0x{flags:02x}, "
            f"{'true' if entry['has_result'] else 'false'}, "
            f"true}},  // 0x{value:02x}")
    lines.append("}};")
    lines.append("")
    lines.append("}  // namespace ogplay::runtime::dexvm::generated")
    lines.append("")
    return "\n".join(lines)


def self_test() -> int:
    if len(REJECTED_OPCODES) + EXPECTED_STANDARD_COUNT != 256:
        print("self-test: rejected + defined != 256", file=sys.stderr)
        return 1
    if len(set(REJECTED_OPCODES)) != len(REJECTED_OPCODES):
        print("self-test: rejected set has duplicates", file=sys.stderr)
        return 1
    if len(set(STANDARD_FORMATS)) != 24:
        print("self-test: expected 24 standard formats", file=sys.stderr)
        return 1
    sample = OP_LINE.match(
        "op   1a const-string                21c  y string-ref    "
        "continue|throw")
    if not sample or sample.group(2) != "const-string":
        print("self-test: op line regex broken", file=sys.stderr)
        return 1
    if enum_name("move/from16") != "OP_MOVE_FROM16":
        print("self-test: enum naming broken", file=sys.stderr)
        return 1
    if cpp_identifier("add-int/lit8") != "AddIntLit8":
        print("self-test: cpp identifier naming broken", file=sys.stderr)
        return 1
    print("self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path)
    parser.add_argument("--bootstrap", action="store_true")
    parser.add_argument("--verify-aosp", type=Path, metavar="BASELINE")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--baseline", type=Path,
                        help="baseline path for --bootstrap")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    if arguments.self_test:
        return self_test()

    if arguments.bootstrap:
        if arguments.baseline is None or arguments.catalog is None:
            parser.error("--bootstrap requires --baseline and --catalog")
        catalog = build_catalog(arguments.baseline)
        errors = validate_catalog(catalog)
        if errors:
            for error in errors:
                print(f"error: {error}", file=sys.stderr)
            return 1
        arguments.catalog.write_text(
            json.dumps(catalog, indent=1) + "\n", encoding="utf-8")
        print(f"wrote {arguments.catalog}")
        return 0

    if arguments.catalog is None:
        parser.error("--catalog is required")
    catalog = load_catalog(arguments.catalog)

    if arguments.verify_aosp is not None:
        errors = verify_against_aosp(catalog, arguments.verify_aosp)
        if errors:
            for error in errors:
                print(f"error: {error}", file=sys.stderr)
            return 1
        print(f"catalog verified against AOSP baseline: "
              f"{len(catalog['opcodes'])} opcodes")

    if arguments.output is not None:
        errors = validate_catalog(catalog)
        if errors:
            for error in errors:
                print(f"error: {error}", file=sys.stderr)
            return 1
        generated = generate_cpp(catalog)
        if arguments.check:
            existing = (arguments.output.read_text(encoding="utf-8")
                        if arguments.output.exists() else "")
            if existing != generated:
                print(f"error: {arguments.output} is stale", file=sys.stderr)
                return 1
            print("generated table is current")
        else:
            arguments.output.parent.mkdir(parents=True, exist_ok=True)
            arguments.output.write_text(generated, encoding="utf-8")
            print(f"wrote {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
