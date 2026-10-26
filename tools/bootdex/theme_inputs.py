#!/usr/bin/env python3
"""Pinned ROM field/array and source resource identity audit for ADR-0160."""
import argparse
import hashlib
import json
import struct
import sys
import zipfile
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.dont_write_bytecode = True
import dex_survey_lib as dx
import dexdis
import framework_res_audit

GROUPS = ('View', 'ViewGroup', 'FrameLayout', 'LinearLayout', 'RelativeLayout',
          'ViewGroup_Layout', 'ViewGroup_MarginLayout', 'FrameLayout_Layout',
          'LinearLayout_Layout', 'RelativeLayout_Layout',
          'TextViewAppearance', 'TextAppearance', 'TextView', 'ImageView',
          'ScrollView', 'HorizontalScrollView')
PINNED = {
    '.local/aosp/framework.jar': '90fc9fd36b45db1810ecf51e40119222da081b552f9a64a6d0c09cac09f98a48',
    '.local/aosp/framework2.jar': 'b6aba8bec45e2a2faa7a4e1e8ae3d8e450259f7e28c8dae6ca2c31dc34e06bea',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def decode_arrays(dex, data, cls):
    """Only decode the pinned R.styleable constant-array initializer forms."""
    arrays = {}
    catalog = dexdis.load_catalog()
    for method in cls.direct_methods:
        if dex.method_signature(method.method_index)[1] != '<clinit>':
            continue
        code = method.code
        units = struct.unpack_from(f'<{code.insns_size}H', data, code.insns_offset)
        regs = {}
        pc = 0
        while pc < len(units):
            word = units[pc]
            op = word & 255
            a = word >> 8
            if op == 0 and a:
                break  # Array payloads follow return-void in these pinned inputs.
            width = int(catalog[op]['format'][0])
            if op == 0x12:
                n = word >> 12
                regs[a & 15] = n - 16 if n >= 8 else n
            elif op == 0x13:
                n = units[pc + 1]
                regs[a] = n - 65536 if n >= 32768 else n
            elif op == 0x14:
                regs[a] = units[pc + 1] | units[pc + 2] << 16
            elif op == 0x15:
                regs[a] = units[pc + 1] << 16
            elif op == 0x23:
                regs[a & 15] = [0] * regs[word >> 12]
            elif op == 0x26:
                offset = units[pc + 1] | units[pc + 2] << 16
                if offset >= 2**31:
                    offset -= 2**32
                at = pc + offset
                require(units[at:at + 2] == (0x300, 4), 'invalid R.styleable array payload')
                size = units[at + 2] | units[at + 3] << 16
                regs[a][:] = [units[at + 4 + i * 2] | units[at + 5 + i * 2] << 16
                              for i in range(size)]
            elif op == 0x4b:
                regs[units[pc + 1] & 255][regs[units[pc + 1] >> 8]] = regs[a]
            elif op == 0x69:
                name = dex.strings[dex.field_ids[units[pc + 1]][2]]
                if name in GROUPS:
                    arrays[name] = list(regs[a])
            elif op not in (0x00, 0x0e):
                raise ValueError(f'unrecognized R.styleable opcode {op:#x}')
            pc += width
    return arrays


def inspect_inputs(negative_rom_id=False):
    hashes = {}
    rom = {}
    shapes = {}
    arrays = {}
    for rel, expected in PINNED.items():
        raw = (ROOT / rel).read_bytes()
        hashes[rel] = sha(raw)
        require(hashes[rel] == expected, f'pinned input mismatch: {rel}')
        with zipfile.ZipFile(ROOT / rel) as archive:
            data = archive.read('classes.dex')
        dex = dx.parse_dex(data)
        for cls in dex.classes:
            owner = dex.type_name(cls.type_index)
            if owner not in ('Landroid/R$attr;', 'Landroid/R$style;',
                             'Lcom/android/internal/R$attr;', 'Lcom/android/internal/R$style;',
                             'Lcom/android/internal/R$styleable;', 'Landroid/R$drawable;', 'Lcom/android/internal/R$drawable;'):
                continue
            values = dx.static_values(data, dex, cls)
            fields = [dex.strings[dex.field_ids[f.field_index][2]] for f in cls.static_fields]
            shapes[owner] = dict(rom=rel, static_field_count=len(fields),
                                 encoded_value_count=len(values), all_field_names=fields)
            rom[owner] = {name: value for name, value in zip(fields, values) if isinstance(value, int)}
            if owner.endswith('R$styleable;'):
                arrays.update(decode_arrays(dex, data, cls))
    if negative_rom_id:
        rom['Landroid/R$style;']['Theme'] ^= 1
    # Independently pin the resource input and cross-check *all* shared R
    # constants before admitting private scrollbar IDs absent from R.drawable.
    resource_audit = framework_res_audit.inspect()
    require(not resource_audit['errors'], 'framework-res/R identity conflict')
    hashes.update({rel: row['sha256'] for rel, row in resource_audit['inputs'].items()})
    drawable_bindings = dict(rom['Landroid/R$drawable;'])
    drawable_bindings.update(rom['Lcom/android/internal/R$drawable;'])
    for row in resource_audit['demanded_drawables']:
        if row['name'] not in framework_res_audit.SCROLLBARS:
            continue
        require(row['package_id'] == 1 and row['type_id'] == 8 and row['type'] == 'drawable',
                'framework-res scrollbar type mismatch')
        require(row['name'] not in drawable_bindings or drawable_bindings[row['name']] == row['id'],
                'framework-res scrollbar ID conflict')
        drawable_bindings[row['name']] = row['id']
    values_dir = ROOT / '.local/aosp/framework/base/core/res/res/values'
    styles = {}
    attrs = {}
    for file in sorted(values_dir.glob('*.xml')):
        rel = file.relative_to(ROOT).as_posix()
        hashes[rel] = sha(file.read_bytes())
        root = ET.parse(file).getroot()
        for style in root.findall('style'):
            name = style.attrib['name']
            if name == 'Theme' or name.startswith('Theme.'):
                require(name not in styles, f'duplicate style {name}')
                styles[name] = dict(source=rel, name=name)
        for node in root.findall('declare-styleable'):
            if node.get('name') in GROUPS:
                attrs[node.attrib['name']] = [item.attrib['name'] for item in node.findall('attr')]
    public = {(p.attrib['type'], p.attrib['name']): int(p.attrib['id'], 16)
              for p in ET.parse(values_dir / 'public.xml').getroot().findall('public')}
    bindings = []
    errors = []
    public_checks = []
    for (kind, name), value in sorted(public.items()):
        if kind not in ('attr', 'style'):
            continue
        owner = f'Landroid/R${kind};'
        actual = rom[owner].get(name.replace('.', '_'))
        if actual is not None:
            public_checks.append(dict(kind=kind, name=name, source_id=value, rom_id=actual))
            if actual != value:
                errors.append(dict(reason='public-id-mismatch', kind=kind, name=name,
                                   source_id=value, rom_id=actual))
    for name, origin in sorted(styles.items()):
        field = name.replace('.', '_')  # Android resource Java field spelling, no numeric alias.
        found = {o: rom[o][field] for o in ('Landroid/R$style;', 'Lcom/android/internal/R$style;')
                 if field in rom[o]}
        ids = set(found.values())
        row = dict(**origin, field=field, rom_fields=found,
                   resource_id=next(iter(ids)) if len(ids) == 1 else None)
        bindings.append(row)
        if not ids:
            pass  # An unbound theme is explicitly unregistered (ADR-0160 supplement).
        elif len(ids) != 1:
            errors.append(dict(reason='conflicting-rom-style-field', name=name, rom_fields=found))
    styleable_checks = []
    for group in GROUPS:
        ids = arrays[group]
        indexes = {n[len(group) + 1:]: value for n, value in rom['Lcom/android/internal/R$styleable;'].items()
                   if n.startswith(group + '_') and not any(n.startswith(other + '_') for other in GROUPS if len(other) > len(group))}
        require(set(indexes) == set(attrs[group]), 'styleable name mismatch: ' + group)
        require(len(ids) == len(attrs[group]), 'styleable array size mismatch: ' + group)
        for name in attrs[group]:
            index = indexes[name]
            require(0 <= index < len(ids), 'styleable index out of range: ' + group + '.' + name)
            resource_id = ids[index]
            expected = public.get(('attr', name))
            fields = {o: rom[o][name] for o in ('Landroid/R$attr;', 'Lcom/android/internal/R$attr;')
                      if name in rom[o]}
            # The private foregroundInsidePadding slot is bound to the original
            # ROM array, as established by REPORT; no synthetic R.attr is added.
            require(all(v == resource_id for v in fields.values()), 'ROM attr/styleable ID mismatch: ' + group + '.' + name)
            require(expected is None or expected == resource_id, 'public attr/styleable ID mismatch: ' + group + '.' + name)
            styleable_checks.append(dict(group=group, name=name, index=index, resource_id=resource_id,
                                         source_public_id=expected, rom_attr_fields=fields))
    all_styles = {}
    for file in sorted(values_dir.glob('*.xml')):
        for node in ET.parse(file).getroot().findall('style'):
            name = node.attrib['name']
            field = name.replace('.', '_')
            ids = {rom[o][field] for o in ('Landroid/R$style;', 'Lcom/android/internal/R$style;') if field in rom[o]}
            if len(ids) > 1:
                errors.append(dict(reason='conflicting-rom-style-binding', name=name))
                continue
            if ids:
                all_styles[name] = next(iter(ids))
    attr_ids = {}
    for owner in ('Landroid/R$attr;', 'Lcom/android/internal/R$attr;'):
        for name, ident in rom[owner].items():
            require(name not in attr_ids or attr_ids[name] == ident, 'conflicting ROM attr: ' + name)
            attr_ids[name] = ident
    by_id = {row['id']: row for row in resource_audit['entries']}
    formats = {}
    for row in resource_audit['entries']:
        if row['type'] != 'attr':
            continue
        variants = row['configurations']
        require(len(variants) == 1 and variants[0]['complex'], 'ambiguous ROM attr format')
        bag = variants[0]['bag']
        masks = [v['data'] for v in bag if v['name'] == 0x01000000]
        require(len(masks) == 1, 'missing ROM attr format')
        names = {}
        for v in bag:
            if v['name'] < 0x01000004:
                continue  # format/min/max/localization metadata
            require(v['name'] in by_id and by_id[v['name']]['type'] == 'id', 'invalid enum name')
            names[by_id[v['name']]['name']] = v['data']
        formats[row['name']] = dict(format=masks[0], names=names)
    report = dict(schema_version=1, task='DVM-288', status='rejected' if errors else 'preflight-passed',
                  accepted_projection=False, input_sha256=hashes, rom_shapes=shapes,
                  themes=bindings, theme_count=len(bindings),
                  bound_theme_count=sum(t['resource_id'] is not None for t in bindings),
                  missing_theme_count=sum(not t['rom_fields'] for t in bindings),
                  public_id_checks=public_checks, styleable_slot_checks=styleable_checks,
                  styleable_group_count=len(GROUPS), styleable_slot_count=len(styleable_checks), errors=errors,
                  negative_control=negative_rom_id, style_bindings=all_styles, attr_bindings=attr_ids,
                  drawable_bindings=drawable_bindings, attribute_formats=formats)
    return report
