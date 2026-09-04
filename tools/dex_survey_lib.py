#!/usr/bin/env python3
"""Minimal strict DEX 035 reader shared by stage-0 dexvm tooling.

Structure layout follows AOSP libdex/DexFile.h at the pinned baseline
(the pinned Android 4.4.4_r2.0.1 AOSP baseline). Only the sections needed for
static measurement are parsed; every out-of-range offset or index raises
DexFormatError instead of guessing.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

DEX_MAGIC_035 = b"dex\n035\x00"
NO_INDEX = 0xFFFFFFFF
HEADER_SIZE = 0x70
ENDIAN_CONSTANT = 0x12345678

ACC_NATIVE = 0x0100


class DexFormatError(ValueError):
    pass


def read_uleb128(data: bytes, offset: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while True:
        if offset >= len(data):
            raise DexFormatError(f"uleb128 truncated at {offset:#x}")
        byte = data[offset]
        offset += 1
        result |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            return result, offset
        shift += 7
        if shift > 32:
            raise DexFormatError(f"uleb128 too long at {offset:#x}")


@dataclass
class DexCode:
    registers_size: int
    ins_size: int
    outs_size: int
    tries_size: int
    insns_size: int
    insns_offset: int


@dataclass
class DexMethod:
    method_index: int
    access_flags: int
    code: DexCode | None


@dataclass
class DexField:
    field_index: int
    access_flags: int


@dataclass
class DexClass:
    type_index: int
    access_flags: int
    superclass_index: int | None
    interface_type_indices: list[int]
    static_fields: list[DexField] = field(default_factory=list)
    instance_fields: list[DexField] = field(default_factory=list)
    direct_methods: list[DexMethod] = field(default_factory=list)
    virtual_methods: list[DexMethod] = field(default_factory=list)


@dataclass
class DexFile:
    strings: list[str]
    type_descriptors: list[str]
    protos: list[tuple[int, int, list[int]]]
    field_ids: list[tuple[int, int, int]]
    method_ids: list[tuple[int, int, int]]
    classes: list[DexClass]

    def type_name(self, index: int) -> str:
        if index >= len(self.type_descriptors):
            raise DexFormatError(f"type index {index} out of range")
        return self.type_descriptors[index]

    def method_signature(self, method_index: int) -> tuple[str, str, str]:
        class_index, proto_index, name_index = self.method_ids[method_index]
        shorty, return_type, parameters = self.protos[proto_index]
        descriptor = "(" + "".join(
            self.type_name(p) for p in parameters
        ) + ")" + self.type_name(return_type)
        del shorty
        return (self.type_name(class_index), self.strings[name_index],
                descriptor)


def decode_mutf8(data: bytes) -> str:
    # Modified UTF-8: no NUL bytes, CESU-8 style surrogate pairs.
    units: list[int] = []
    index = 0
    while index < len(data):
        byte = data[index]
        if byte == 0:
            raise DexFormatError("embedded NUL in string data")
        if byte < 0x80:
            units.append(byte)
            index += 1
        elif (byte & 0xE0) == 0xC0:
            if index + 1 >= len(data):
                raise DexFormatError("truncated 2-byte mutf8 sequence")
            units.append(((byte & 0x1F) << 6) | (data[index + 1] & 0x3F))
            index += 2
        elif (byte & 0xF0) == 0xE0:
            if index + 2 >= len(data):
                raise DexFormatError("truncated 3-byte mutf8 sequence")
            units.append(((byte & 0x0F) << 12) |
                         ((data[index + 1] & 0x3F) << 6) |
                         (data[index + 2] & 0x3F))
            index += 3
        else:
            raise DexFormatError(f"invalid mutf8 lead byte {byte:#x}")
    return "".join(chr(unit) for unit in units)


def _u2(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u4(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _check_range(data: bytes, offset: int, size: int, what: str) -> None:
    if offset + size > len(data):
        raise DexFormatError(f"{what} out of range at {offset:#x}")


def parse_dex(data: bytes) -> DexFile:
    if len(data) < HEADER_SIZE:
        raise DexFormatError("file smaller than header")
    if data[:8] != DEX_MAGIC_035:
        raise DexFormatError(f"unsupported magic {data[:8]!r}")
    if _u4(data, 0x28) != ENDIAN_CONSTANT:
        raise DexFormatError("unsupported endianness")
    file_size = _u4(data, 0x20)
    if file_size != len(data):
        raise DexFormatError("header file_size mismatch")

    string_count, string_off = _u4(data, 0x38), _u4(data, 0x3C)
    type_count, type_off = _u4(data, 0x40), _u4(data, 0x44)
    proto_count, proto_off = _u4(data, 0x48), _u4(data, 0x4C)
    field_count, field_off = _u4(data, 0x50), _u4(data, 0x54)
    method_count, method_off = _u4(data, 0x58), _u4(data, 0x5C)
    class_count, class_off = _u4(data, 0x60), _u4(data, 0x64)

    strings: list[str] = []
    _check_range(data, string_off, string_count * 4, "string_ids")
    for i in range(string_count):
        data_off = _u4(data, string_off + i * 4)
        _, payload_start = read_uleb128(data, data_off)
        end = data.index(b"\x00", payload_start)
        strings.append(decode_mutf8(data[payload_start:end]))

    type_descriptors: list[str] = []
    _check_range(data, type_off, type_count * 4, "type_ids")
    for i in range(type_count):
        descriptor_index = _u4(data, type_off + i * 4)
        if descriptor_index >= string_count:
            raise DexFormatError("type descriptor index out of range")
        type_descriptors.append(strings[descriptor_index])

    protos: list[tuple[int, int, list[int]]] = []
    _check_range(data, proto_off, proto_count * 12, "proto_ids")
    for i in range(proto_count):
        base = proto_off + i * 12
        shorty_index = _u4(data, base)
        return_index = _u4(data, base + 4)
        parameters_off = _u4(data, base + 8)
        parameters: list[int] = []
        if parameters_off:
            count = _u4(data, parameters_off)
            _check_range(data, parameters_off + 4, count * 2, "type_list")
            parameters = [
                _u2(data, parameters_off + 4 + j * 2) for j in range(count)
            ]
        protos.append((shorty_index, return_index, parameters))

    field_ids = []
    _check_range(data, field_off, field_count * 8, "field_ids")
    for i in range(field_count):
        base = field_off + i * 8
        field_ids.append((_u2(data, base), _u2(data, base + 2),
                          _u4(data, base + 4)))

    method_ids = []
    _check_range(data, method_off, method_count * 8, "method_ids")
    for i in range(method_count):
        base = method_off + i * 8
        method_ids.append((_u2(data, base), _u2(data, base + 2),
                           _u4(data, base + 4)))

    classes: list[DexClass] = []
    _check_range(data, class_off, class_count * 32, "class_defs")
    for i in range(class_count):
        base = class_off + i * 32
        type_index = _u4(data, base)
        access_flags = _u4(data, base + 4)
        superclass = _u4(data, base + 8)
        interfaces_off = _u4(data, base + 12)
        class_data_off = _u4(data, base + 24)
        interfaces: list[int] = []
        if interfaces_off:
            count = _u4(data, interfaces_off)
            _check_range(data, interfaces_off + 4, count * 2, "interfaces")
            interfaces = [
                _u2(data, interfaces_off + 4 + j * 2) for j in range(count)
            ]
        parsed = DexClass(
            type_index=type_index,
            access_flags=access_flags,
            superclass_index=None if superclass == NO_INDEX else superclass,
            interface_type_indices=interfaces,
        )
        if class_data_off:
            _parse_class_data(data, class_data_off, parsed)
        classes.append(parsed)

    return DexFile(strings=strings, type_descriptors=type_descriptors,
                   protos=protos, field_ids=field_ids, method_ids=method_ids,
                   classes=classes)


def _parse_class_data(data: bytes, offset: int, parsed: DexClass) -> None:
    static_count, offset = read_uleb128(data, offset)
    instance_count, offset = read_uleb128(data, offset)
    direct_count, offset = read_uleb128(data, offset)
    virtual_count, offset = read_uleb128(data, offset)

    def read_fields(count: int, offset: int) -> tuple[list[DexField], int]:
        fields: list[DexField] = []
        index = 0
        for _ in range(count):
            diff, offset = read_uleb128(data, offset)
            flags, offset = read_uleb128(data, offset)
            index += diff
            fields.append(DexField(field_index=index, access_flags=flags))
        return fields, offset

    def read_methods(count: int, offset: int) -> tuple[list[DexMethod], int]:
        methods: list[DexMethod] = []
        index = 0
        for _ in range(count):
            diff, offset = read_uleb128(data, offset)
            flags, offset = read_uleb128(data, offset)
            code_off, offset = read_uleb128(data, offset)
            index += diff
            code = None
            if code_off:
                _check_range(data, code_off, 16, "code_item")
                code = DexCode(
                    registers_size=_u2(data, code_off),
                    ins_size=_u2(data, code_off + 2),
                    outs_size=_u2(data, code_off + 4),
                    tries_size=_u2(data, code_off + 6),
                    insns_size=_u4(data, code_off + 12),
                    insns_offset=code_off + 16,
                )
            methods.append(DexMethod(method_index=index, access_flags=flags,
                                     code=code))
        return methods, offset

    parsed.static_fields, offset = read_fields(static_count, offset)
    parsed.instance_fields, offset = read_fields(instance_count, offset)
    parsed.direct_methods, offset = read_methods(direct_count, offset)
    parsed.virtual_methods, offset = read_methods(virtual_count, offset)


PLATFORM_PREFIXES = (
    "Landroid/", "Ljava/", "Ljavax/", "Ldalvik/", "Lorg/apache/http/",
    # AOSP-bundled library packages (SAX, JSON) count as platform.
    "Lorg/xml/", "Lorg/json/",
)


def is_platform_descriptor(descriptor: str) -> bool:
    # android.support.* is an APK-bundled application library, not an Android
    # framework namespace, even though its binary name begins with android.
    if descriptor.startswith("Landroid/support/"):
        return False
    return descriptor.startswith(PLATFORM_PREFIXES)
