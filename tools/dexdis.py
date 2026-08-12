#!/usr/bin/env python3
"""Quick DEX method disassembler for local analysis (stage-0 tooling).

Formats mnemonics from the declarative opcode catalog. Analysis aid only;
never part of the runtime or CI.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

import dex_survey_lib as dexlib


def load_catalog() -> dict[int, dict]:
    catalog = json.loads(
        (TOOLS.parent / "data/dexvm/dalvik_opcodes.json").read_text())
    return {entry["opcode"]: entry for entry in catalog["opcodes"]}


def disassemble(dex: dexlib.DexFile, data: bytes, code: dexlib.DexCode,
                catalog: dict[int, dict]) -> list[str]:
    units = struct.unpack_from(f"<{code.insns_size}H", data,
                               code.insns_offset)
    lines = []
    pc = 0
    while pc < len(units):
        unit = units[pc]
        op = unit & 0xFF
        if op == 0 and (unit >> 8):
            ident = unit
            if ident == 0x0100:
                width = 4 + 2 * units[pc + 1]
                lines.append(f"{pc:5d}: .packed-switch-payload")
            elif ident == 0x0200:
                width = 2 + 4 * units[pc + 1]
                lines.append(f"{pc:5d}: .sparse-switch-payload")
            else:
                count = units[pc + 2] | (units[pc + 3] << 16)
                width = 4 + (units[pc + 1] * count + 1) // 2
                lines.append(f"{pc:5d}: .array-data-payload")
            pc += width
            continue
        entry = catalog.get(op)
        if entry is None:
            lines.append(f"{pc:5d}: <bad {op:#x}>")
            break
        width = int(entry["format"][0])
        detail = ""
        index_type = entry["index_type"]
        if index_type != "none" and width >= 2:
            index = units[pc + 1]
            if entry["format"] == "31c":
                index |= units[pc + 2] << 16
            try:
                if index_type == "string-ref":
                    detail = repr(dex.strings[index])
                elif index_type == "type-ref":
                    detail = dex.type_name(index)
                elif index_type == "method-ref":
                    owner, name, desc = dex.method_signature(index)
                    detail = f"{owner}->{name}{desc}"
                elif index_type == "field-ref":
                    class_index, type_index, name_index = dex.field_ids[index]
                    detail = (f"{dex.type_name(class_index)}->"
                              f"{dex.strings[name_index]}:"
                              f"{dex.type_name(type_index)}")
            except Exception:  # noqa: BLE001 - analysis aid only
                detail = f"#?{index}"
        raw = " ".join(f"{units[pc + i]:04x}" for i in range(width))
        lines.append(f"{pc:5d}: {entry['name']:<24} {raw:<16} {detail}")
        pc += width
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--class-prefix", default="")
    parser.add_argument("--method", default="")
    arguments = parser.parse_args()

    with zipfile.ZipFile(arguments.apk) as archive:
        data = archive.read("classes.dex")
    dex = dexlib.parse_dex(data)
    catalog = load_catalog()

    for cls in dex.classes:
        descriptor = dex.type_name(cls.type_index)
        if arguments.class_prefix and \
                not descriptor.startswith(arguments.class_prefix):
            continue
        for method in cls.direct_methods + cls.virtual_methods:
            owner, name, signature = dex.method_signature(
                method.method_index)
            if arguments.method and name != arguments.method:
                continue
            flags = method.access_flags
            print(f"\n== {owner}->{name}{signature} "
                  f"(flags {flags:#x}, code={'yes' if method.code else 'no'})")
            if method.code:
                print(f"   registers={method.code.registers_size} "
                      f"ins={method.code.ins_size} "
                      f"tries={method.code.tries_size}")
                for line in disassemble(dex, data, method.code, catalog):
                    print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
