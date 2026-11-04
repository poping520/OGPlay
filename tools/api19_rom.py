#!/usr/bin/env python3
"""Hash-pinned API19 ROM jars: own-member shapes for BootDex and contract tooling."""
from __future__ import annotations

import hashlib
import io
import struct
import zipfile
from pathlib import Path

import dex_survey_lib as dex
from dexdis import load_catalog

ROOT = Path(__file__).resolve().parent.parent
PINNED = {
    'core.jar': '60b6cdf3eded2fdf157c6166402477e4f72ade215b334817439960522c0e64ee',
    'framework.jar': '422e232e121425268f04044f5349072ce42d7b4403267d21569e8ce70d07bc4b',
    'framework2.jar': '5866d4e62e5fe44ffaf6ff441b6c361baeb1f8b91e30bb555481ef13ef837973',
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


def load(directory=ROOT / '.local/aosp'):
    """Return {descriptor: [super, interfaces, flags, methods, fields]} from the pinned jars.

    methods: [name, descriptor, flags]; fields: [name, descriptor, flags, kind, value] where
    kind 1 is a numeric and 2 a String static final constant no clinit writes.
    """
    records = {}
    catalog = load_catalog()
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


def extend_surface(surface, records):
    """Overlay ROM shapes on a dump-surface: absent classes become source='rom' candidates,
    absent members of existing classes are listed as unimplemented. Existing rows always win."""
    by = {c['descriptor']: c for c in surface}
    out = []
    for cls in surface:
        record = records.get(cls['descriptor'])
        if record is None:
            out.append(cls)
            continue
        methods = {(m['name'], m['descriptor']) for m in cls['methods']}
        fields = {(f['name'], f['descriptor']) for f in cls['fields']}
        merged = dict(cls)
        merged['methods'] = [*cls['methods'], *(_method(*m) for m in record[3] if (m[0], m[1]) not in methods)]
        merged['fields'] = [*cls['fields'], *(_field(*f) for f in record[4] if (f[0], f[1]) not in fields)]
        out.append(merged)
    for owner, (parent, interfaces, flags, methods, fields) in records.items():
        if owner not in by:
            out.append(dict(descriptor=owner, source='rom', superclass=parent, interfaces=interfaces,
                            access_flags=flags, link_error='',
                            methods=[_method(*m) for m in methods], fields=[_field(*f) for f in fields]))
    return out


def _method(name, descriptor, flags):
    abstract = bool(flags & 0x400)
    return dict(name=name, descriptor=descriptor, access_flags=flags,
                status='implemented' if abstract else 'unimplemented-ledger',
                binding='abstract-contract' if abstract else 'intrinsic')


def _field(name, descriptor, flags, kind, value):
    del value
    return dict(name=name, descriptor=descriptor, access_flags=flags, has_constant=bool(kind),
                status='implemented' if kind else 'unimplemented-ledger')
