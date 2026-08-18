#!/usr/bin/env python3
"""dexasm: deterministic DEX 035 assembler for dexvm test fixtures.

Assembles a restricted smali-flavoured IR (NOT smali-compatible) into a valid
dex 035 file. Byte-deterministic: identical input yields identical output.
Pool layout, alignment, uleb128 and checksum rules follow docs/dex-format at
the pinned AOSP baseline; opcode values and formats
come from data/dexvm/dalvik_opcodes.json, never a second hand-written table.

It is a test tool, not a general assembler: anything outside the supported
subset fails loudly. See docs/design/dexvm/05-verification.md §1.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

import dex_survey_lib as dexlib

DEFAULT_CATALOG = TOOLS_DIR.parent / "data" / "dexvm" / "dalvik_opcodes.json"

ACCESS_FLAGS = {
    "public": 0x1, "private": 0x2, "protected": 0x4, "static": 0x8,
    "final": 0x10, "synchronized": 0x20, "volatile": 0x40, "bridge": 0x40,
    "transient": 0x80, "varargs": 0x80, "native": 0x100, "interface": 0x200,
    "abstract": 0x400, "strictfp": 0x800, "synthetic": 0x1000,
    "annotation": 0x2000, "enum": 0x4000, "constructor": 0x10000,
    "declared-synchronized": 0x20000,
}

NO_INDEX = 0xFFFFFFFF


class DexAsmError(ValueError):
    def __init__(self, line_number: int, message: str):
        super().__init__(f"line {line_number}: {message}")
        self.line_number = line_number


def uleb128(value: int) -> bytes:
    if value < 0:
        raise ValueError("uleb128 requires non-negative value")
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def sleb128(value: int) -> bytes:
    out = bytearray()
    more = True
    while more:
        byte = value & 0x7F
        value >>= 7
        if (value == 0 and not (byte & 0x40)) or \
           (value == -1 and (byte & 0x40)):
            more = False
        else:
            byte |= 0x80
        out.append(byte)
    return bytes(out)


def encode_mutf8(text: str) -> bytes:
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


def parse_string_literal(token: str, line: int) -> str:
    if len(token) < 2 or not token.startswith('"') or not token.endswith('"'):
        raise DexAsmError(line, f"malformed string literal {token}")
    body = token[1:-1]
    out = []
    index = 0
    while index < len(body):
        char = body[index]
        if char != "\\":
            out.append(char)
            index += 1
            continue
        if index + 1 >= len(body):
            raise DexAsmError(line, "dangling escape in string literal")
        escape = body[index + 1]
        if escape == "n":
            out.append("\n")
            index += 2
        elif escape == "t":
            out.append("\t")
            index += 2
        elif escape == '"':
            out.append('"')
            index += 2
        elif escape == "\\":
            out.append("\\")
            index += 2
        elif escape == "u":
            if index + 6 > len(body):
                raise DexAsmError(line, "truncated \\u escape")
            out.append(chr(int(body[index + 2:index + 6], 16)))
            index += 6
        else:
            raise DexAsmError(line, f"unsupported escape \\{escape}")
    return "".join(out)


def parse_int_literal(token: str, line: int) -> int:
    try:
        return int(token, 0)
    except ValueError as error:
        raise DexAsmError(line, f"malformed integer literal {token}") \
            from error


def is_type_descriptor(token: str) -> bool:
    base = token.lstrip("[")
    return (base in ("V", "Z", "B", "S", "C", "I", "J", "F", "D") or
            (base.startswith("L") and base.endswith(";")))


def shorty_char(descriptor: str) -> str:
    if descriptor.startswith("[") or descriptor.startswith("L"):
        return "L"
    return descriptor


def split_parameters(descriptor: str, line: int) -> tuple[list[str], str]:
    if not descriptor.startswith("("):
        raise DexAsmError(line, f"malformed method descriptor {descriptor}")
    close = descriptor.index(")")
    body = descriptor[1:close]
    return_type = descriptor[close + 1:]
    parameters: list[str] = []
    index = 0
    while index < len(body):
        start = index
        while body[index] == "[":
            index += 1
        if body[index] == "L":
            index = body.index(";", index) + 1
        else:
            index += 1
        parameters.append(body[start:index])
    if not is_type_descriptor(return_type):
        raise DexAsmError(line, f"malformed return type {return_type}")
    return parameters, return_type


def type_width(descriptor: str) -> int:
    return 2 if descriptor in ("J", "D") else 1


@dataclass
class FieldRef:
    owner: str
    name: str
    descriptor: str


@dataclass
class MethodRef:
    owner: str
    name: str
    descriptor: str


FIELD_REF = re.compile(r"^(\[*L[^;]+;|\[+[ZBSCIJFD])->([^:]+):(.+)$")
METHOD_REF = re.compile(r"^(\[*L[^;]+;|\[+[ZBSCIJFD])->([^(]+)(\(.*\).+)$")


def parse_field_ref(token: str, line: int) -> FieldRef:
    match = FIELD_REF.match(token)
    if not match or not is_type_descriptor(match.group(3)):
        raise DexAsmError(line, f"malformed field reference {token}")
    return FieldRef(match.group(1), match.group(2), match.group(3))


def parse_method_ref(token: str, line: int) -> MethodRef:
    match = METHOD_REF.match(token)
    if not match:
        raise DexAsmError(line, f"malformed method reference {token}")
    return MethodRef(match.group(1), match.group(2), match.group(3))


@dataclass
class Instruction:
    line: int
    mnemonic: str
    opcode: int
    format: str
    address: int = 0
    registers: list[int] = field(default_factory=list)
    literal: int | None = None
    label: str | None = None
    string_value: str | None = None
    type_value: str | None = None
    field_value: FieldRef | None = None
    method_value: MethodRef | None = None

    def units(self) -> int:
        return int(self.format[0])


@dataclass
class Payload:
    line: int
    kind: str  # packed_switch | sparse_switch | array_data
    address: int = 0
    label: str = ""
    first_key: int = 0
    keys: list[int] = field(default_factory=list)
    targets: list[str] = field(default_factory=list)
    element_width: int = 0
    elements: list[int] = field(default_factory=list)

    def units(self) -> int:
        if self.kind == "packed_switch":
            return len(self.targets) * 2 + 4
        if self.kind == "sparse_switch":
            return len(self.keys) * 4 + 2
        byte_count = self.element_width * len(self.elements)
        return (byte_count + 1) // 2 + 4


@dataclass
class TryBlock:
    line: int
    start_label: str
    end_label: str
    handler_label: str
    exception_type: str | None  # None = catch-all


@dataclass
class Method:
    line: int
    name: str
    descriptor: str
    access_flags: int
    registers: int = 0
    items: list = field(default_factory=list)  # Instruction | Payload | label
    labels: dict[str, int] = field(default_factory=dict)
    tries: list[TryBlock] = field(default_factory=list)
    has_code: bool = True


@dataclass
class FieldDecl:
    line: int
    name: str
    descriptor: str
    access_flags: int
    static_value: tuple[str, object] | None = None


@dataclass
class ClassDecl:
    line: int
    descriptor: str
    access_flags: int
    superclass: str | None = None
    interfaces: list[str] = field(default_factory=list)
    fields: list[FieldDecl] = field(default_factory=list)
    methods: list[Method] = field(default_factory=list)


class Parser:
    def __init__(self, catalog_path: Path):
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        self.opcodes = {entry["name"]: entry for entry in catalog["opcodes"]}
        self.classes: list[ClassDecl] = []

    def parse(self, text: str) -> list[ClassDecl]:
        current_class: ClassDecl | None = None
        current_method: Method | None = None
        lines = text.splitlines()
        index = 0
        while index < len(lines):
            raw = lines[index]
            line_number = index + 1
            index += 1
            code = raw.split("#", 1)[0].strip()
            if not code:
                continue
            tokens = code.replace(",", " ").split()

            if tokens[0] == ".class":
                flags = 0
                for token in tokens[1:-1]:
                    if token not in ACCESS_FLAGS:
                        raise DexAsmError(line_number,
                                          f"unknown access flag {token}")
                    flags |= ACCESS_FLAGS[token]
                descriptor = tokens[-1]
                if not is_type_descriptor(descriptor):
                    raise DexAsmError(line_number,
                                      f"malformed class {descriptor}")
                current_class = ClassDecl(line_number, descriptor, flags)
                self.classes.append(current_class)
            elif tokens[0] == ".super":
                self._require_class(current_class, line_number)
                current_class.superclass = tokens[1]
            elif tokens[0] == ".implements":
                self._require_class(current_class, line_number)
                current_class.interfaces.append(tokens[1])
            elif tokens[0] == ".field":
                self._require_class(current_class, line_number)
                current_class.fields.append(
                    self._parse_field(tokens, line_number))
            elif tokens[0] == ".method":
                self._require_class(current_class, line_number)
                current_method = self._parse_method_header(
                    tokens, line_number)
                current_class.methods.append(current_method)
            elif tokens[0] == ".registers":
                self._require_method(current_method, line_number)
                current_method.registers = parse_int_literal(
                    tokens[1], line_number)
            elif tokens[0] == ".end":
                if len(tokens) > 1 and tokens[1] == "method":
                    current_method = None
                elif len(tokens) > 1 and tokens[1] == "class":
                    current_class = None
                else:
                    raise DexAsmError(line_number, "unsupported .end")
            elif tokens[0] == ".catch":
                self._require_method(current_method, line_number)
                current_method.tries.append(TryBlock(
                    line_number, tokens[2].lstrip(":"),
                    tokens[3].lstrip(":"), tokens[4].lstrip(":"), tokens[1]))
            elif tokens[0] == ".catchall":
                self._require_method(current_method, line_number)
                current_method.tries.append(TryBlock(
                    line_number, tokens[1].lstrip(":"),
                    tokens[2].lstrip(":"), tokens[3].lstrip(":"), None))
            elif tokens[0] == ".packed-switch":
                self._require_method(current_method, line_number)
                index = self._parse_packed_switch(
                    current_method, tokens, lines, index, line_number)
            elif tokens[0] == ".sparse-switch":
                self._require_method(current_method, line_number)
                index = self._parse_sparse_switch(
                    current_method, lines, index, line_number)
            elif tokens[0] == ".array-data":
                self._require_method(current_method, line_number)
                index = self._parse_array_data(
                    current_method, tokens, lines, index, line_number)
            elif tokens[0].startswith(":") and len(tokens) == 1:
                self._require_method(current_method, line_number)
                current_method.items.append(("label", tokens[0][1:]))
            else:
                self._require_method(current_method, line_number)
                current_method.items.append(
                    self._parse_instruction(code, line_number))
        return self.classes

    @staticmethod
    def _require_class(current: ClassDecl | None, line: int) -> None:
        if current is None:
            raise DexAsmError(line, "directive outside .class")

    @staticmethod
    def _require_method(current: Method | None, line: int) -> None:
        if current is None:
            raise DexAsmError(line, "directive outside .method")

    def _parse_field(self, tokens: list[str], line: int) -> FieldDecl:
        # .field [flags] name:TYPE [= literal]
        assignment = None
        if "=" in tokens:
            eq = tokens.index("=")
            assignment = " ".join(tokens[eq + 1:])
            tokens = tokens[:eq]
        flags = 0
        for token in tokens[1:-1]:
            if token not in ACCESS_FLAGS:
                raise DexAsmError(line, f"unknown access flag {token}")
            flags |= ACCESS_FLAGS[token]
        name, _, descriptor = tokens[-1].partition(":")
        if not name or not is_type_descriptor(descriptor):
            raise DexAsmError(line, f"malformed field {tokens[-1]}")
        static_value = None
        if assignment is not None:
            if not flags & ACCESS_FLAGS["static"]:
                raise DexAsmError(line, "initial value on non-static field")
            static_value = self._parse_static_value(
                assignment.strip(), descriptor, line)
        return FieldDecl(line, name, descriptor, flags, static_value)

    @staticmethod
    def _parse_static_value(token: str, descriptor: str,
                            line: int) -> tuple[str, object]:
        if token == "null":
            return ("null", None)
        if token in ("true", "false"):
            return ("boolean", token == "true")
        if token.startswith('"'):
            return ("string", parse_string_literal(token, line))
        if descriptor == "F":
            return ("float", float(token.rstrip("fF")))
        if descriptor == "D":
            return ("double", float(token.rstrip("dD")))
        value = parse_int_literal(token.rstrip("lL"), line)
        kinds = {"Z": "boolean", "B": "byte", "S": "short", "C": "char",
                 "I": "int", "J": "long"}
        kind = kinds.get(descriptor)
        if kind is None:
            raise DexAsmError(line,
                              f"unsupported static value type {descriptor}")
        if kind == "boolean":
            return ("boolean", bool(value))
        return (kind, value)

    def _parse_method_header(self, tokens: list[str], line: int) -> Method:
        flags = 0
        for token in tokens[1:-1]:
            if token not in ACCESS_FLAGS:
                raise DexAsmError(line, f"unknown access flag {token}")
            flags |= ACCESS_FLAGS[token]
        signature = tokens[-1]
        paren = signature.index("(")
        name = signature[:paren]
        descriptor = signature[paren:]
        split_parameters(descriptor, line)  # validate
        method = Method(line, name, descriptor, flags)
        if flags & (ACCESS_FLAGS["native"] | ACCESS_FLAGS["abstract"]):
            method.has_code = False
        return method

    def _parse_packed_switch(self, method: Method, tokens: list[str],
                             lines: list[str], index: int,
                             line: int) -> int:
        payload = Payload(line, "packed_switch")
        payload.first_key = parse_int_literal(tokens[1], line)
        while index < len(lines):
            code = lines[index].split("#", 1)[0].strip()
            index += 1
            if not code:
                continue
            if code == ".end packed-switch":
                method.items.append(payload)
                return index
            if not code.startswith(":"):
                raise DexAsmError(index, f"expected label, got {code}")
            payload.targets.append(code[1:])
        raise DexAsmError(line, "unterminated .packed-switch")

    def _parse_sparse_switch(self, method: Method, lines: list[str],
                             index: int, line: int) -> int:
        payload = Payload(line, "sparse_switch")
        while index < len(lines):
            code = lines[index].split("#", 1)[0].strip()
            index += 1
            if not code:
                continue
            if code == ".end sparse-switch":
                if payload.keys != sorted(payload.keys):
                    raise DexAsmError(line, "sparse-switch keys not sorted")
                method.items.append(payload)
                return index
            key_token, arrow, target = code.partition("->")
            if not arrow or not target.strip().startswith(":"):
                raise DexAsmError(index, f"expected 'key -> :label': {code}")
            payload.keys.append(parse_int_literal(key_token.strip(), index))
            payload.targets.append(target.strip()[1:])
        raise DexAsmError(line, "unterminated .sparse-switch")

    def _parse_array_data(self, method: Method, tokens: list[str],
                          lines: list[str], index: int, line: int) -> int:
        payload = Payload(line, "array_data")
        payload.element_width = parse_int_literal(tokens[1], line)
        if payload.element_width not in (1, 2, 4, 8):
            raise DexAsmError(line, "array-data width must be 1/2/4/8")
        while index < len(lines):
            code = lines[index].split("#", 1)[0].strip()
            index += 1
            if not code:
                continue
            if code == ".end array-data":
                method.items.append(payload)
                return index
            for token in code.replace(",", " ").split():
                payload.elements.append(parse_int_literal(token, index))
        raise DexAsmError(line, "unterminated .array-data")

    def _parse_instruction(self, code: str, line: int) -> Instruction:
        # Split mnemonic from operands; string literals may contain commas.
        parts = code.split(None, 1)
        mnemonic = parts[0]
        entry = self.opcodes.get(mnemonic)
        if entry is None:
            raise DexAsmError(line, f"unknown mnemonic {mnemonic}")
        instruction = Instruction(line, mnemonic, entry["opcode"],
                                  entry["format"])
        rest = parts[1].strip() if len(parts) > 1 else ""
        operands = self._split_operands(rest, line)
        self._bind_operands(instruction, operands, line)
        return instruction

    @staticmethod
    def _split_operands(rest: str, line: int) -> list[str]:
        operands: list[str] = []
        buffer = ""
        in_string = False
        in_braces = False
        index = 0
        while index < len(rest):
            char = rest[index]
            if in_string:
                buffer += char
                if char == "\\":
                    buffer += rest[index + 1]
                    index += 1
                elif char == '"':
                    in_string = False
            elif char == '"':
                in_string = True
                buffer += char
            elif char == "{":
                in_braces = True
                buffer += char
            elif char == "}":
                in_braces = False
                buffer += char
            elif char == "," and not in_braces:
                operands.append(buffer.strip())
                buffer = ""
            else:
                buffer += char
            index += 1
        if buffer.strip():
            operands.append(buffer.strip())
        if in_string or in_braces:
            raise DexAsmError(line, "unterminated string or brace list")
        return operands

    def _bind_operands(self, instruction: Instruction, operands: list[str],
                       line: int) -> None:
        def register(token: str) -> int:
            if not token.startswith("v"):
                raise DexAsmError(line, f"expected register, got {token}")
            return int(token[1:])

        for token in operands:
            if token.startswith("{"):
                inner = token[1:-1].strip()
                if not inner:
                    continue
                if ".." in inner:
                    first, _, last = inner.partition("..")
                    start = register(first.strip())
                    end = register(last.strip())
                    if end < start:
                        raise DexAsmError(line, "descending range")
                    instruction.registers.extend(range(start, end + 1))
                else:
                    for reg in inner.replace(",", " ").split():
                        instruction.registers.append(register(reg))
            elif token.startswith("v") and token[1:].isdigit():
                instruction.registers.append(int(token[1:]))
            elif token.startswith(":"):
                instruction.label = token[1:]
            elif token.startswith('"'):
                instruction.string_value = parse_string_literal(token, line)
            elif "->" in token and ":" in token.split("->", 1)[1] and \
                    "(" not in token:
                instruction.field_value = parse_field_ref(token, line)
            elif "->" in token:
                instruction.method_value = parse_method_ref(token, line)
            elif is_type_descriptor(token):
                instruction.type_value = token
            else:
                instruction.literal = parse_int_literal(token, line)


class Pools:
    def __init__(self) -> None:
        self.strings: set[str] = set()
        self.types: set[str] = set()
        self.protos: set[tuple[str, ...]] = set()  # (return, *params)
        self.fields: set[tuple[str, str, str]] = set()
        self.methods: set[tuple[str, str, str]] = set()

    def add_type(self, descriptor: str) -> None:
        self.types.add(descriptor)
        self.strings.add(descriptor)

    def add_proto(self, descriptor: str, line: int) -> tuple[str, ...]:
        parameters, return_type = split_parameters(descriptor, line)
        for parameter in parameters:
            self.add_type(parameter)
        self.add_type(return_type)
        shorty = shorty_char(return_type) + "".join(
            shorty_char(parameter) for parameter in parameters)
        self.strings.add(shorty)
        key = (return_type, *parameters)
        self.protos.add(key)
        return key

    def add_field(self, ref: FieldRef) -> None:
        self.add_type(ref.owner)
        self.add_type(ref.descriptor)
        self.strings.add(ref.name)
        self.fields.add((ref.owner, ref.name, ref.descriptor))

    def add_method(self, ref: MethodRef, line: int) -> None:
        self.add_type(ref.owner)
        self.strings.add(ref.name)
        self.add_proto(ref.descriptor, line)
        self.methods.add((ref.owner, ref.name, ref.descriptor))


class Assembler:
    def __init__(self, classes: list[ClassDecl]):
        self.classes = classes
        self.pools = Pools()
        self._collect()
        self._index_pools()

    def _collect(self) -> None:
        for declaration in self.classes:
            self.pools.add_type(declaration.descriptor)
            if declaration.superclass:
                self.pools.add_type(declaration.superclass)
            for interface in declaration.interfaces:
                self.pools.add_type(interface)
            for field_decl in declaration.fields:
                self.pools.add_field(FieldRef(
                    declaration.descriptor, field_decl.name,
                    field_decl.descriptor))
                if field_decl.static_value and \
                        field_decl.static_value[0] == "string":
                    self.pools.strings.add(field_decl.static_value[1])
            for method in declaration.methods:
                self.pools.add_method(MethodRef(
                    declaration.descriptor, method.name, method.descriptor),
                    method.line)
                for item in method.items:
                    if not isinstance(item, Instruction):
                        continue
                    if item.string_value is not None:
                        self.pools.strings.add(item.string_value)
                    if item.type_value is not None:
                        self.pools.add_type(item.type_value)
                    if item.field_value is not None:
                        self.pools.add_field(item.field_value)
                    if item.method_value is not None:
                        self.pools.add_method(item.method_value, item.line)
                for try_block in method.tries:
                    if try_block.exception_type is not None:
                        self.pools.add_type(try_block.exception_type)

    def _index_pools(self) -> None:
        self.string_list = sorted(self.pools.strings)
        self.string_index = {value: index for index, value
                             in enumerate(self.string_list)}
        self.type_list = sorted(self.pools.types,
                                key=lambda t: self.string_index[t])
        self.type_index = {value: index for index, value
                           in enumerate(self.type_list)}
        self.proto_list = sorted(
            self.pools.protos,
            key=lambda p: (self.type_index[p[0]],
                           tuple(self.type_index[x] for x in p[1:])))
        self.proto_index = {value: index for index, value
                            in enumerate(self.proto_list)}
        self.field_list = sorted(
            self.pools.fields,
            key=lambda f: (self.type_index[f[0]], self.string_index[f[1]],
                           self.type_index[f[2]]))
        self.field_index = {value: index for index, value
                            in enumerate(self.field_list)}

        def proto_key(descriptor: str) -> tuple[str, ...]:
            parameters, return_type = split_parameters(descriptor, 0)
            return (return_type, *parameters)

        self.method_list = sorted(
            self.pools.methods,
            key=lambda m: (self.type_index[m[0]], self.string_index[m[1]],
                           self.proto_index[proto_key(m[2])]))
        self.method_index = {value: index for index, value
                             in enumerate(self.method_list)}
        self.proto_of_method = {
            key: self.proto_index[proto_key(key[2])]
            for key in self.method_list
        }

        # class_defs: dependencies (super/interfaces defined here) first.
        defined = {declaration.descriptor: declaration
                   for declaration in self.classes}
        ordered: list[ClassDecl] = []
        visiting: set[str] = set()

        def visit(declaration: ClassDecl) -> None:
            if declaration in ordered:
                return
            if declaration.descriptor in visiting:
                raise DexAsmError(declaration.line,
                                  "class dependency cycle")
            visiting.add(declaration.descriptor)
            dependencies = ([declaration.superclass] if declaration.superclass
                            else []) + declaration.interfaces
            for dependency in dependencies:
                if dependency in defined:
                    visit(defined[dependency])
            visiting.discard(declaration.descriptor)
            ordered.append(declaration)

        for declaration in sorted(self.classes,
                                  key=lambda d: self.type_index[d.descriptor]):
            visit(declaration)
        self.ordered_classes = ordered

    # ----- instruction encoding -------------------------------------------

    def _lay_out_method(self, method: Method) -> None:
        address = 0
        pending_labels: list[str] = []
        for item in method.items:
            if isinstance(item, tuple):  # label
                method.labels[item[1]] = address
                pending_labels.append(item[1])
                continue
            if isinstance(item, Payload):
                # Payloads must start on a 4-byte (even code-unit) boundary;
                # a nop pad is inserted during encoding when needed. Labels
                # preceding the payload move with it past the pad.
                if address % 2 != 0:
                    address += 1
                    for label in pending_labels:
                        method.labels[label] = address
                item.address = address
                address += item.units()
                pending_labels.clear()
                continue
            item.address = address
            address += item.units()
            pending_labels.clear()
        method.code_units = address  # type: ignore[attr-defined]

    def _encode_method(self, method: Method) -> bytes:
        # Payload labels: label preceding a Payload marks the payload site.
        # Map labels to payload objects for 31t resolution.
        payload_by_label: dict[str, Payload] = {}
        previous_label: str | None = None
        for item in method.items:
            if isinstance(item, tuple):
                previous_label = item[1]
            elif isinstance(item, Payload):
                if previous_label is None:
                    raise DexAsmError(item.line, "payload requires a label")
                payload_by_label[previous_label] = item
                previous_label = None
            else:
                previous_label = None

        units: list[int] = []

        def emit(*values: int) -> None:
            for value in values:
                units.append(value & 0xFFFF)

        for item in method.items:
            if isinstance(item, tuple):
                continue
            if isinstance(item, Payload):
                while len(units) < item.address:
                    emit(0x0000)  # nop pad
                units.extend(self._encode_payload(item, method))
                continue
            if len(units) != item.address:
                raise DexAsmError(item.line, "address bookkeeping mismatch")
            units.extend(self._encode_instruction(item, method,
                                                  payload_by_label))
        data = struct.pack(f"<{len(units)}H", *units)
        return data

    def _resolve_label(self, method: Method, label: str, line: int) -> int:
        if label not in method.labels:
            raise DexAsmError(line, f"undefined label :{label}")
        return method.labels[label]

    def _encode_instruction(self, ins: Instruction, method: Method,
                            payloads: dict[str, Payload]) -> list[int]:
        op = ins.opcode
        regs = ins.registers
        fmt = ins.format

        def check(condition: bool, message: str) -> None:
            if not condition:
                raise DexAsmError(ins.line, message)

        def signed_fit(value: int, bits: int) -> int:
            low = -(1 << (bits - 1))
            high = (1 << (bits - 1)) - 1
            check(low <= value <= high,
                  f"literal {value} out of {bits}-bit range")
            return value & ((1 << bits) - 1)

        def branch_offset(bits: int) -> int:
            target = self._resolve_label(method, ins.label, ins.line)
            return signed_fit(target - ins.address, bits)

        def pool_index() -> int:
            if ins.string_value is not None:
                return self.string_index[ins.string_value]
            if ins.type_value is not None:
                return self.type_index[ins.type_value]
            if ins.field_value is not None:
                ref = ins.field_value
                return self.field_index[(ref.owner, ref.name, ref.descriptor)]
            if ins.method_value is not None:
                ref = ins.method_value
                return self.method_index[(ref.owner, ref.name,
                                          ref.descriptor)]
            raise DexAsmError(ins.line, "missing pool reference operand")

        if fmt == "10x":
            return [op]
        if fmt == "12x":
            check(len(regs) == 2 and max(regs) <= 15, "12x needs vA,vB <=15")
            return [op | (regs[0] << 8) | (regs[1] << 12)]
        if fmt == "22x":
            check(len(regs) == 2 and regs[0] <= 255 and regs[1] <= 0xFFFF,
                  "22x operand range")
            return [op | (regs[0] << 8), regs[1]]
        if fmt == "32x":
            check(len(regs) == 2, "32x needs two registers")
            return [op, regs[0], regs[1]]
        if fmt == "11n":
            check(len(regs) == 1 and regs[0] <= 15, "11n register range")
            check(ins.literal is not None, "11n needs literal")
            return [op | (regs[0] << 8) |
                    (signed_fit(ins.literal, 4) << 12)]
        if fmt == "11x":
            check(len(regs) == 1 and regs[0] <= 255, "11x register range")
            return [op | (regs[0] << 8)]
        if fmt == "10t":
            return [op | (branch_offset(8) << 8)]
        if fmt == "20t":
            return [op, branch_offset(16)]
        if fmt == "30t":
            offset = self._resolve_label(method, ins.label,
                                         ins.line) - ins.address
            value = offset & 0xFFFFFFFF
            return [op, value & 0xFFFF, value >> 16]
        if fmt == "21s":
            check(len(regs) == 1 and ins.literal is not None, "21s operands")
            return [op | (regs[0] << 8), signed_fit(ins.literal, 16)]
        if fmt == "21h":
            check(len(regs) == 1 and ins.literal is not None, "21h operands")
            literal = ins.literal
            wide = ins.mnemonic.startswith("const-wide")
            shift = 48 if wide else 16
            check(literal % (1 << shift) == 0,
                  "21h literal must be high-bits only")
            return [op | (regs[0] << 8),
                    (literal >> shift) & 0xFFFF]
        if fmt == "31i":
            check(len(regs) == 1 and ins.literal is not None, "31i operands")
            value = ins.literal & 0xFFFFFFFF
            return [op | (regs[0] << 8), value & 0xFFFF, value >> 16]
        if fmt == "51l":
            check(len(regs) == 1 and ins.literal is not None, "51l operands")
            value = ins.literal & 0xFFFFFFFFFFFFFFFF
            return [op | (regs[0] << 8), value & 0xFFFF,
                    (value >> 16) & 0xFFFF, (value >> 32) & 0xFFFF,
                    (value >> 48) & 0xFFFF]
        if fmt == "21t":
            check(len(regs) == 1, "21t needs one register")
            return [op | (regs[0] << 8), branch_offset(16)]
        if fmt == "22t":
            check(len(regs) == 2 and max(regs) <= 15, "22t register range")
            return [op | (regs[0] << 8) | (regs[1] << 12),
                    branch_offset(16)]
        if fmt == "21c":
            check(len(regs) == 1, "21c needs one register")
            return [op | (regs[0] << 8), pool_index()]
        if fmt == "22c":
            check(len(regs) == 2 and max(regs) <= 15, "22c register range")
            return [op | (regs[0] << 8) | (regs[1] << 12), pool_index()]
        if fmt == "31c":
            check(len(regs) == 1, "31c needs one register")
            index = pool_index()
            return [op | (regs[0] << 8), index & 0xFFFF, index >> 16]
        if fmt == "23x":
            check(len(regs) == 3 and max(regs) <= 255, "23x register range")
            return [op | (regs[0] << 8), regs[1] | (regs[2] << 8)]
        if fmt == "22b":
            check(len(regs) == 2 and ins.literal is not None, "22b operands")
            return [op | (regs[0] << 8),
                    regs[1] | (signed_fit(ins.literal, 8) << 8)]
        if fmt == "22s":
            check(len(regs) == 2 and max(regs) <= 15 and
                  ins.literal is not None, "22s operands")
            return [op | (regs[0] << 8) | (regs[1] << 12),
                    signed_fit(ins.literal, 16)]
        if fmt == "31t":
            check(len(regs) == 1 and ins.label is not None, "31t operands")
            target = self._resolve_label(method, ins.label, ins.line)
            payload = payloads.get(ins.label)
            if payload is not None:
                payload.anchor = ins.address  # type: ignore[attr-defined]
            offset = (target - ins.address) & 0xFFFFFFFF
            return [op | (regs[0] << 8), offset & 0xFFFF, offset >> 16]
        if fmt == "35c":
            check(len(regs) <= 5, "35c allows at most 5 registers")
            check(all(reg <= 15 for reg in regs), "35c register range")
            index = pool_index()
            padded = regs + [0] * (5 - len(regs))
            unit0 = op | (padded[4] << 8) | (len(regs) << 12)
            unit2 = (padded[0] | (padded[1] << 4) | (padded[2] << 8) |
                     (padded[3] << 12))
            return [unit0, index, unit2]
        if fmt == "3rc":
            check(len(regs) >= 1, "3rc needs a register range")
            first = regs[0]
            check(regs == list(range(first, first + len(regs))),
                  "3rc registers must be consecutive")
            return [op | (len(regs) << 8), pool_index(), first]
        raise DexAsmError(ins.line, f"unsupported format {fmt}")

    def _encode_payload(self, payload: Payload, method: Method) -> list[int]:
        anchor = getattr(payload, "anchor", None)
        if anchor is None:
            raise DexAsmError(payload.line, "payload is never referenced")
        units: list[int] = []
        if payload.kind == "packed_switch":
            units.append(0x0100)
            units.append(len(payload.targets))
            key = payload.first_key & 0xFFFFFFFF
            units.extend([key & 0xFFFF, key >> 16])
            for label in payload.targets:
                offset = (self._resolve_label(method, label, payload.line) -
                          anchor) & 0xFFFFFFFF
                units.extend([offset & 0xFFFF, offset >> 16])
        elif payload.kind == "sparse_switch":
            units.append(0x0200)
            units.append(len(payload.keys))
            for key in payload.keys:
                value = key & 0xFFFFFFFF
                units.extend([value & 0xFFFF, value >> 16])
            for label in payload.targets:
                offset = (self._resolve_label(method, label, payload.line) -
                          anchor) & 0xFFFFFFFF
                units.extend([offset & 0xFFFF, offset >> 16])
        else:  # array_data
            units.append(0x0300)
            units.append(payload.element_width)
            count = len(payload.elements)
            units.extend([count & 0xFFFF, count >> 16])
            raw = bytearray()
            for element in payload.elements:
                raw += element.to_bytes(payload.element_width, "little",
                                        signed=element < 0)
            if len(raw) % 2:
                raw += b"\x00"
            for index in range(0, len(raw), 2):
                units.append(raw[index] | (raw[index + 1] << 8))
        return units

    # ----- static values ---------------------------------------------------

    def _encode_encoded_value(self, kind: str, value: object,
                              line: int) -> bytes:
        def trimmed_signed(number: int, max_bytes: int) -> bytes:
            for size in range(1, max_bytes + 1):
                low = -(1 << (size * 8 - 1))
                high = (1 << (size * 8 - 1)) - 1
                if low <= number <= high:
                    return number.to_bytes(size, "little", signed=True)
            raise DexAsmError(line, f"value {number} too wide")

        def trimmed_unsigned(number: int, max_bytes: int) -> bytes:
            for size in range(1, max_bytes + 1):
                if number < (1 << (size * 8)):
                    return number.to_bytes(size, "little")
            raise DexAsmError(line, f"value {number} too wide")

        if kind == "null":
            return bytes([0x1E])
        if kind == "boolean":
            return bytes([0x1F | ((1 if value else 0) << 5)])
        if kind == "byte":
            return bytes([0x00]) + trimmed_signed(value, 1)
        if kind == "short":
            payload = trimmed_signed(value, 2)
            return bytes([0x02 | ((len(payload) - 1) << 5)]) + payload
        if kind == "char":
            payload = trimmed_unsigned(value, 2)
            return bytes([0x03 | ((len(payload) - 1) << 5)]) + payload
        if kind == "int":
            payload = trimmed_signed(value, 4)
            return bytes([0x04 | ((len(payload) - 1) << 5)]) + payload
        if kind == "long":
            payload = trimmed_signed(value, 8)
            return bytes([0x06 | ((len(payload) - 1) << 5)]) + payload
        if kind == "float":
            raw = struct.pack("<f", value)
            trimmed = raw.rstrip(b"\x00") or b"\x00"
            return bytes([0x10 | ((len(trimmed) - 1) << 5)]) + trimmed
        if kind == "double":
            raw = struct.pack("<d", value)
            trimmed = raw.rstrip(b"\x00") or b"\x00"
            return bytes([0x11 | ((len(trimmed) - 1) << 5)]) + trimmed
        if kind == "string":
            payload = trimmed_unsigned(self.string_index[value], 4)
            return bytes([0x17 | ((len(payload) - 1) << 5)]) + payload
        raise DexAsmError(line, f"unsupported encoded value kind {kind}")

    # ----- final layout ------------------------------------------------------

    def assemble(self) -> bytes:
        header_size = 0x70
        string_ids_off = header_size
        type_ids_off = string_ids_off + 4 * len(self.string_list)
        proto_ids_off = type_ids_off + 4 * len(self.type_list)
        field_ids_off = proto_ids_off + 12 * len(self.proto_list)
        method_ids_off = field_ids_off + 8 * len(self.field_list)
        class_defs_off = method_ids_off + 8 * len(self.method_list)
        data_off = class_defs_off + 32 * len(self.classes)

        data = bytearray()
        map_items: list[tuple[int, int, int]] = []  # (type, size, offset)

        def align4() -> None:
            while (data_off + len(data)) % 4:
                data.append(0)

        # type_lists: for protos with parameters and class interface lists.
        type_list_offsets: dict[tuple[str, ...], int] = {}
        needed_lists: list[tuple[str, ...]] = []
        for proto in self.proto_list:
            if len(proto) > 1 and tuple(proto[1:]) not in needed_lists:
                needed_lists.append(tuple(proto[1:]))
        for declaration in self.ordered_classes:
            key = tuple(declaration.interfaces)
            if key and key not in needed_lists:
                needed_lists.append(key)
        if needed_lists:
            for key in needed_lists:
                align4()
                type_list_offsets[key] = data_off + len(data)
                data += struct.pack("<I", len(key))
                for descriptor in key:
                    data += struct.pack("<H", self.type_index[descriptor])
            map_items.append((0x1001, len(needed_lists),
                              type_list_offsets[needed_lists[0]]))

        # code items
        code_offsets: dict[tuple[str, str, str], int] = {}
        code_count = 0
        first_code_off = 0
        for declaration in self.ordered_classes:
            for method in declaration.methods:
                if not method.has_code:
                    continue
                self._lay_out_method(method)
                insns = self._encode_method(method)
                align4()
                offset = data_off + len(data)
                if code_count == 0:
                    first_code_off = offset
                code_count += 1
                code_offsets[(declaration.descriptor, method.name,
                              method.descriptor)] = offset
                parameters, _ = split_parameters(method.descriptor,
                                                 method.line)
                ins_words = sum(type_width(p) for p in parameters)
                if not method.access_flags & ACCESS_FLAGS["static"]:
                    ins_words += 1
                outs_words = self._outs_words(method)
                tries_size = len(method.tries)
                insns_units = len(insns) // 2
                data += struct.pack("<HHHHII", method.registers, ins_words,
                                    outs_words, tries_size, 0, insns_units)
                data += insns
                if tries_size:
                    if insns_units % 2:
                        data += b"\x00\x00"
                    data += self._encode_tries(method)
        if code_count:
            map_items.append((0x2001, code_count, first_code_off))

        # encoded arrays (static values), one per class that has them
        static_value_offsets: dict[str, int] = {}
        array_count = 0
        first_array_off = 0
        for declaration in self.ordered_classes:
            static_fields = self._sorted_fields(declaration, static_only=True)
            values = [f for f in static_fields if f.static_value is not None]
            if not values:
                continue
            # encoded_array covers a prefix of the static field list.
            last_with_value = max(static_fields.index(f) for f in values)
            prefix = static_fields[:last_with_value + 1]
            offset = data_off + len(data)
            if array_count == 0:
                first_array_off = offset
            array_count += 1
            static_value_offsets[declaration.descriptor] = offset
            data += uleb128(len(prefix))
            for field_decl in prefix:
                if field_decl.static_value is None:
                    default_kind = self._default_kind(field_decl.descriptor)
                    data += self._encode_encoded_value(
                        default_kind[0], default_kind[1], field_decl.line)
                else:
                    data += self._encode_encoded_value(
                        field_decl.static_value[0],
                        field_decl.static_value[1], field_decl.line)
        if array_count:
            map_items.append((0x2005, array_count, first_array_off))

        # class_data
        class_data_offsets: dict[str, int] = {}
        class_data_count = 0
        first_class_data_off = 0
        for declaration in self.ordered_classes:
            offset = data_off + len(data)
            if class_data_count == 0:
                first_class_data_off = offset
            class_data_count += 1
            class_data_offsets[declaration.descriptor] = offset
            data += self._encode_class_data(declaration, code_offsets)
        if class_data_count:
            map_items.append((0x2000, class_data_count, first_class_data_off))

        # string data
        string_data_offsets: list[int] = []
        for value in self.string_list:
            string_data_offsets.append(data_off + len(data))
            data += uleb128(len(value)) + encode_mutf8(value) + b"\x00"
        if self.string_list:
            map_items.append((0x2002, len(self.string_list),
                              string_data_offsets[0]))

        # map_list
        align4()
        map_off = data_off + len(data)
        map_items_all = [(0x0000, 1, 0),
                         (0x0001, len(self.string_list), string_ids_off),
                         (0x0002, len(self.type_list), type_ids_off)]
        if self.proto_list:
            map_items_all.append((0x0003, len(self.proto_list),
                                  proto_ids_off))
        if self.field_list:
            map_items_all.append((0x0004, len(self.field_list),
                                  field_ids_off))
        if self.method_list:
            map_items_all.append((0x0005, len(self.method_list),
                                  method_ids_off))
        if self.classes:
            map_items_all.append((0x0006, len(self.classes), class_defs_off))
        map_items_all.extend(map_items)
        map_items_all.append((0x1000, 1, map_off))
        map_items_all.sort(key=lambda item: item[2])
        data += struct.pack("<I", len(map_items_all))
        for item_type, size, offset in map_items_all:
            data += struct.pack("<HHII", item_type, 0, size, offset)

        file_size = data_off + len(data)

        # id tables
        ids = bytearray()
        for offset in string_data_offsets:
            ids += struct.pack("<I", offset)
        for descriptor in self.type_list:
            ids += struct.pack("<I", self.string_index[descriptor])
        for proto in self.proto_list:
            return_type = proto[0]
            parameters = tuple(proto[1:])
            shorty = shorty_char(return_type) + "".join(
                shorty_char(parameter) for parameter in parameters)
            parameters_off = type_list_offsets.get(parameters, 0) \
                if parameters else 0
            ids += struct.pack("<III", self.string_index[shorty],
                               self.type_index[return_type], parameters_off)
        for owner, name, descriptor in self.field_list:
            ids += struct.pack("<HHI", self.type_index[owner],
                               self.type_index[descriptor],
                               self.string_index[name])
        for owner, name, descriptor in self.method_list:
            ids += struct.pack("<HHI", self.type_index[owner],
                               self.proto_of_method[(owner, name,
                                                     descriptor)],
                               self.string_index[name])
        for declaration in self.ordered_classes:
            superclass = (self.type_index[declaration.superclass]
                          if declaration.superclass else NO_INDEX)
            interfaces_off = type_list_offsets.get(
                tuple(declaration.interfaces), 0) \
                if declaration.interfaces else 0
            ids += struct.pack(
                "<IIIIIIII", self.type_index[declaration.descriptor],
                declaration.access_flags, superclass, interfaces_off,
                NO_INDEX, 0,
                class_data_offsets[declaration.descriptor],
                static_value_offsets.get(declaration.descriptor, 0))

        header = bytearray(0x70)
        struct.pack_into("<8s", header, 0, dexlib.DEX_MAGIC_035)
        struct.pack_into("<I", header, 0x20, file_size)
        struct.pack_into("<I", header, 0x24, 0x70)
        struct.pack_into("<I", header, 0x28, dexlib.ENDIAN_CONSTANT)
        struct.pack_into("<II", header, 0x2C, 0, 0)  # link
        struct.pack_into("<I", header, 0x34, map_off)
        struct.pack_into("<II", header, 0x38, len(self.string_list),
                         string_ids_off)
        struct.pack_into("<II", header, 0x40, len(self.type_list),
                         type_ids_off)
        struct.pack_into("<II", header, 0x48, len(self.proto_list),
                         proto_ids_off if self.proto_list else 0)
        struct.pack_into("<II", header, 0x50, len(self.field_list),
                         field_ids_off if self.field_list else 0)
        struct.pack_into("<II", header, 0x58, len(self.method_list),
                         method_ids_off if self.method_list else 0)
        struct.pack_into("<II", header, 0x60, len(self.classes),
                         class_defs_off if self.classes else 0)
        struct.pack_into("<II", header, 0x68, file_size - data_off, data_off)

        blob = bytes(header) + bytes(ids) + bytes(data)
        signature = hashlib.sha1(blob[0x20:]).digest()
        blob = blob[:0x0C] + signature + blob[0x20:]
        checksum = zlib.adler32(blob[0x0C:]) & 0xFFFFFFFF
        blob = blob[:0x08] + struct.pack("<I", checksum) + blob[0x0C:]
        return blob

    @staticmethod
    def _default_kind(descriptor: str) -> tuple[str, object]:
        defaults = {"Z": ("boolean", False), "B": ("byte", 0),
                    "S": ("short", 0), "C": ("char", 0), "I": ("int", 0),
                    "J": ("long", 0), "F": ("float", 0.0),
                    "D": ("double", 0.0)}
        return defaults.get(descriptor, ("null", None))

    def _outs_words(self, method: Method) -> int:
        outs = 0
        for item in method.items:
            if not isinstance(item, Instruction):
                continue
            if item.method_value is None or \
                    not item.mnemonic.startswith("invoke"):
                continue
            outs = max(outs, len(item.registers))
        return outs

    def _encode_tries(self, method: Method) -> bytes:
        # Group try blocks by (start, end); each group is one try_item with
        # one encoded handler entry.
        groups: dict[tuple[int, int], list[TryBlock]] = {}
        order: list[tuple[int, int]] = []
        for try_block in method.tries:
            start = self._resolve_label(method, try_block.start_label,
                                        try_block.line)
            end = self._resolve_label(method, try_block.end_label,
                                      try_block.line)
            if end <= start:
                raise DexAsmError(try_block.line, "empty try range")
            key = (start, end)
            if key not in groups:
                groups[key] = []
                order.append(key)
            groups[key].append(try_block)
        order.sort()

        handlers_blob = bytearray()
        handlers_blob += uleb128(len(order))
        handler_offsets: dict[tuple[int, int], int] = {}
        for key in order:
            handler_offsets[key] = len(handlers_blob)
            blocks = groups[key]
            typed = [block for block in blocks
                     if block.exception_type is not None]
            catch_all = [block for block in blocks
                         if block.exception_type is None]
            if len(catch_all) > 1:
                raise DexAsmError(blocks[0].line,
                                  "multiple catch-all handlers")
            size = len(typed)
            handlers_blob += sleb128(-size if catch_all else size)
            for block in typed:
                handlers_blob += uleb128(
                    self.type_index[block.exception_type])
                handlers_blob += uleb128(self._resolve_label(
                    method, block.handler_label, block.line))
            if catch_all:
                handlers_blob += uleb128(self._resolve_label(
                    method, catch_all[0].handler_label, catch_all[0].line))

        tries_blob = bytearray()
        for key in order:
            start, end = key
            tries_blob += struct.pack("<IHH", start, end - start,
                                      handler_offsets[key])
        return bytes(tries_blob) + bytes(handlers_blob)

    def _sorted_fields(self, declaration: ClassDecl,
                       static_only: bool) -> list[FieldDecl]:
        selected = [field_decl for field_decl in declaration.fields
                    if bool(field_decl.access_flags &
                            ACCESS_FLAGS["static"]) == static_only]
        return sorted(
            selected,
            key=lambda f: self.field_index[(declaration.descriptor, f.name,
                                            f.descriptor)])

    def _encode_class_data(self, declaration: ClassDecl,
                           code_offsets: dict) -> bytes:
        static_fields = self._sorted_fields(declaration, static_only=True)
        instance_fields = self._sorted_fields(declaration, static_only=False)
        direct: list[Method] = []
        virtual: list[Method] = []
        for method in declaration.methods:
            is_direct = bool(method.access_flags &
                             (ACCESS_FLAGS["static"] |
                              ACCESS_FLAGS["private"] |
                              ACCESS_FLAGS["constructor"])) or \
                method.name in ("<init>", "<clinit>")
            (direct if is_direct else virtual).append(method)

        def method_key(method: Method) -> int:
            return self.method_index[(declaration.descriptor, method.name,
                                      method.descriptor)]

        direct.sort(key=method_key)
        virtual.sort(key=method_key)

        blob = bytearray()
        blob += uleb128(len(static_fields))
        blob += uleb128(len(instance_fields))
        blob += uleb128(len(direct))
        blob += uleb128(len(virtual))
        for fields in (static_fields, instance_fields):
            previous = 0
            for field_decl in fields:
                index = self.field_index[(declaration.descriptor,
                                          field_decl.name,
                                          field_decl.descriptor)]
                blob += uleb128(index - previous)
                blob += uleb128(field_decl.access_flags)
                previous = index
        for methods in (direct, virtual):
            previous = 0
            for method in methods:
                index = method_key(method)
                blob += uleb128(index - previous)
                flags = method.access_flags
                if method.name in ("<init>", "<clinit>"):
                    flags |= ACCESS_FLAGS["constructor"]
                blob += uleb128(flags)
                code_off = code_offsets.get(
                    (declaration.descriptor, method.name,
                     method.descriptor), 0)
                blob += uleb128(code_off)
                previous = index
        return bytes(blob)


def assemble_text(text: str, catalog: Path = DEFAULT_CATALOG) -> bytes:
    parser = Parser(catalog)
    classes = parser.parse(text)
    if not classes:
        raise DexAsmError(0, "no classes declared")
    return Assembler(classes).assemble()


SELF_TEST_SOURCE = """
.class public LFixture;
.super Ljava/lang/Object;
.field static public counter:I = 41
.field static public tag:Ljava/lang/String; = "fixture"

.method public constructor <init>()V
.registers 1
    invoke-direct {v0}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method static public divide(II)I
.registers 4
    :try_start
    div-int v0, v2, v3
    :try_end
    return v0
    :handler
    const/4 v0, -1
    return v0
    .catch Ljava/lang/ArithmeticException; :try_start :try_end :handler
.end method

.method static public pick(I)I
.registers 3
    packed-switch v2, :table
    const/4 v0, 0
    return v0
    :case_one
    const/4 v0, 1
    return v0
    :case_two
    const/16 v0, 22
    return v0
    :table
    .packed-switch 1
        :case_one
        :case_two
    .end packed-switch
.end method

.method static public fill()[I
.registers 2
    const/4 v0, 3
    new-array v0, v0, [I
    fill-array-data v0, :data
    return-object v0
    :data
    .array-data 4
        7 8 9
    .end array-data
.end method
"""

# Golden SHA-256 of the self-test assembly (locks byte determinism).
SELF_TEST_GOLDEN = None  # patched below after first computation


def self_test(catalog: Path) -> int:
    blob = assemble_text(SELF_TEST_SOURCE, catalog)
    digest = hashlib.sha256(blob).hexdigest()
    golden = ("e1b47e3ee9a7d557be38f00d2cf7a5cb556c7e9f64d7dd43149ff05818a4c4fc")
    if digest != golden:
        print(f"self-test: golden mismatch: {digest}", file=sys.stderr)
        return 1

    # Structural readback through the independent survey-lib parser.
    dex = dexlib.parse_dex(blob)
    if len(dex.classes) != 1:
        print("self-test: expected one class", file=sys.stderr)
        return 1
    parsed = dex.classes[0]
    if dex.type_name(parsed.type_index) != "LFixture;":
        print("self-test: class descriptor mismatch", file=sys.stderr)
        return 1
    method_names = set()
    for method in parsed.direct_methods + parsed.virtual_methods:
        _, name, _ = dex.method_signature(method.method_index)
        method_names.add(name)
    if method_names != {"<init>", "divide", "pick", "fill"}:
        print(f"self-test: methods {method_names}", file=sys.stderr)
        return 1
    divide = next(
        method for method in parsed.direct_methods
        if dex.method_signature(method.method_index)[1] == "divide")
    if divide.code is None or divide.code.tries_size != 1:
        print("self-test: divide try block missing", file=sys.stderr)
        return 1
    if divide.code.registers_size != 4 or divide.code.ins_size != 2:
        print("self-test: divide frame layout wrong", file=sys.stderr)
        return 1
    determinism = hashlib.sha256(
        assemble_text(SELF_TEST_SOURCE, catalog)).hexdigest()
    if determinism != digest:
        print("self-test: output is not deterministic", file=sys.stderr)
        return 1
    print("self-test: ok")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--batch-input", type=Path,
                        help="assemble every *.dexasm in this directory")
    parser.add_argument("--batch-output", type=Path)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--print-golden", action="store_true",
                        help=argparse.SUPPRESS)
    arguments = parser.parse_args()

    if arguments.print_golden:
        blob = assemble_text(SELF_TEST_SOURCE, arguments.catalog)
        print(hashlib.sha256(blob).hexdigest())
        return 0
    if arguments.self_test:
        return self_test(arguments.catalog)
    if arguments.batch_input is not None:
        if arguments.batch_output is None:
            parser.error("--batch-input requires --batch-output")
        arguments.batch_output.mkdir(parents=True, exist_ok=True)
        sources = sorted(arguments.batch_input.glob("*.dexasm"))
        if not sources:
            print(f"error: no .dexasm sources in {arguments.batch_input}",
                  file=sys.stderr)
            return 1
        for source in sources:
            blob = assemble_text(source.read_text(encoding="utf-8"),
                                 arguments.catalog)
            target = arguments.batch_output / (source.stem + ".dex")
            target.write_bytes(blob)
        print(f"assembled {len(sources)} fixture(s) into "
              f"{arguments.batch_output}")
        return 0
    if arguments.input is None or arguments.output is None:
        parser.error("--input and --output are required")
    blob = assemble_text(arguments.input.read_text(encoding="utf-8"),
                         arguments.catalog)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(blob)
    print(f"wrote {arguments.output} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
