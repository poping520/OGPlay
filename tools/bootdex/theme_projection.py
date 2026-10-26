#!/usr/bin/env python3
"""Generate the sealed, default-values-only bounded theme demand projection."""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

import theme_inputs
import struct
import zipfile
import framework_res_audit as rom

ROOT = Path(__file__).resolve().parents[2]
ENTRY = 'META-INF/ogplay/theme-projection.json'
POLICY = Path(__file__).with_name('theme-projection-inputs.json')


def canonical(document):
    return (json.dumps(document, sort_keys=True, separators=(',', ':'), ensure_ascii=True) + '\n').encode()


def named_value(text, attr_name, fmt):
    if not fmt['format'] & (0x10000 | 0x20000):
        return None
    parts = text.split('|') if fmt['format'] & 0x20000 else [text]
    if all(part.strip() in fmt['names'] for part in parts):
        result = 0
        for part in parts:
            result |= fmt['names'][part.strip()]
        return dict(state='known', type=17, data=result)
    return dict(state='unsupported', reason='unknown ROM enum/flags name for ' + attr_name + ': ' + text)


def self_test():
    formats = theme_inputs.inspect_inputs()['attribute_formats']
    assert named_value('normal', 'textStyle', formats['textStyle']) == dict(state='known', type=17, data=0)
    assert named_value('bold|italic', 'textStyle', formats['textStyle'])['data'] == 3
    assert named_value('normal', 'typeface', formats['typeface'])['data'] == 0
    assert named_value('sans', 'typeface', formats['typeface'])['data'] == 1
    assert named_value('bold|typo', 'textStyle', formats['textStyle'])['state'] == 'unsupported'
    assert named_value('bold', 'typeface', formats['typeface'])['state'] == 'unsupported'
    assert named_value('normal', 'fontFamily', formats['fontFamily']) is None
    print('same-ROM enum/flags format positive and unknown/cross-attribute negatives passed')


def string_pool(strings):
    offsets, data = [], bytearray()
    for text in strings:
        encoded = text.encode('utf-16-le')
        count = len(encoded) // 2
        if count >= 0x8000:
            raise ValueError('projected string too long')
        offsets.append(len(data))
        data += struct.pack('<H', count) + encoded + bytes(2)
    data += bytes((-len(data)) % 4)
    start = 28 + 4 * len(strings)
    return struct.pack('<HH6I', 1, 28, start + len(data), len(strings), 0, 0, start, 0) + struct.pack('<%dI' % len(offsets), *offsets) + data


def drawable_closure():
    """Only the reviewed same-ROM EditText selector and its nine-patch leaves."""
    with zipfile.ZipFile(ROOT / rom.APK) as archive:
        _, rows = rom.ResourceTable(archive.read('resources.arsc')).parse()
        by_id = {row['id']: row for row in rows}
        root, = [row for row in rows if row['type'] == 'drawable' and row['name'] == 'edit_text']
        pending, result = [root['id']], []
        while pending:
            ident = pending.pop(0)
            if any(row['id'] == ident for row in result):
                continue
            row = by_id[ident]
            if row['type'] != 'drawable':
                raise ValueError('non-drawable selector dependency')
            variants = []
            for variant in row['configurations']:
                # Bound this capability to default configuration plus density.
                cfg = bytearray.fromhex(variant['config_hex'])
                cfg[14:16] = bytes(2)
                sdk, = struct.unpack_from('<H', cfg, 24)
                if sdk > 19:
                    raise ValueError('unsupported skin SDK')
                cfg[24:26] = bytes(2)
                if any(cfg[4:]):
                    raise ValueError('unsupported ROM skin configuration')
                path = variant['path']
                raw = archive.read(path)
                facts = dict(density=variant['density'], path=path, sha256=rom.sha(raw))
                if path.endswith('.xml'):
                    table = rom.ResourceTable(raw)
                    strings, resource_ids, items = [], [], []
                    stack = []
                    for at, kind, header, size in table.chunks(8, len(raw)):
                        if kind == 1:
                            strings = table.pool(at)
                        elif kind == 0x180:
                            resource_ids = list(table.words('<%dI' % ((size - header) // 4), at + header))
                        elif kind == 0x102:
                            _, name, start, width, count, _, _, _ = table.words('<II6H', at + 16)
                            tag = strings[name]
                            if tag not in ('selector', 'item') or (tag == 'item' and stack != ['selector']):
                                raise ValueError('unreviewed drawable XML element')
                            stack.append(tag)
                            states, target = [], None
                            if tag == 'selector' and count:
                                raise ValueError('selector options require separate review')
                            for i in range(count):
                                ns, name, text, vs, zero, typ, data = table.words('<IIIHBBI', at + 16 + start + i * width)
                                aid = resource_ids[name]
                                if aid == 0x01010199 and typ == 1:
                                    target = data
                                elif by_id[aid]['type'] == 'attr' and by_id[aid]['name'].startswith('state_') and typ == 18:
                                    states.append(aid if data else -aid)
                                else:
                                    raise ValueError('unsupported selector attribute')
                            if tag == 'item':
                                if target is None:
                                    raise ValueError('selector item without drawable')
                                items.append(dict(states=states, drawable=target))
                                pending.append(target)
                        elif kind == 0x103:
                            stack.pop()
                        elif kind not in (0x100, 0x101):
                            raise ValueError('unsupported selector XML chunk')
                    facts.update(kind='selector', items=items)
                elif path.endswith('.9.png'):
                    if not raw.startswith(b'\x89PNG\r\n\x1a\n'):
                        raise ValueError('invalid ninepatch signature')
                    width, height = struct.unpack_from('>II', raw, 16)
                    chunks = {}
                    at = 8
                    while at < len(raw):
                        size, = struct.unpack_from('>I', raw, at)
                        if size > len(raw) - at - 12:
                            raise ValueError('truncated PNG chunk')
                        name = raw[at+4:at+8]
                        if name in chunks:
                            raise ValueError('duplicate PNG metadata chunk')
                        if name in (b'npTc', b'npLb'):
                            chunks[name] = raw[at+8:at+8+size]
                        at += size + 12
                    chunk = chunks[b'npTc']
                    nx, ny, nc = chunk[1:4]
                    if not nx or nx % 2 or not ny or ny % 2 or len(chunk) != 32 + 4 * (nx + ny + nc):
                        raise ValueError('invalid ninepatch division/color counts')
                    left, right, top, bottom = struct.unpack_from('>4i', chunk, 12)
                    if min(left, right, top, bottom) < 0 or left + right > width or top + bottom > height:
                        raise ValueError('invalid ninepatch padding')
                    optical = list(struct.unpack('<4i', chunks[b'npLb'])) if b'npLb' in chunks else [0]*4
                    facts.update(kind='ninepatch', png=raw.hex(), width=width, height=height,
                                 padding=[left, top, right, bottom], optical=optical)
                else:
                    raise ValueError('unreviewed skin drawable type')
                variants.append(facts)
            result.append(dict(id=ident, name=row['name'], variants=variants))
        return sorted(result, key=lambda row: row['id'])


def generate(negative_rom_id=False):
    report = theme_inputs.inspect_inputs(negative_rom_id)
    policy = json.loads(POLICY.read_text(encoding='utf-8'))
    if report['input_sha256'] != policy['input_sha256']:
        raise ValueError('theme projection pinned source/ROM input mismatch')
    if policy.get('projectable_reference_styleables') != {'View': ['drawable']}:
        raise ValueError('unreviewed projected reference categories')
    if report['errors']:
        raise ValueError('theme projection ROM ID mismatch: ' + json.dumps(report['errors']))
    values = ROOT / '.local/aosp/framework/base/core/res/res/values'
    styles = {}
    for path in sorted(values.glob('*.xml')):
        for item in ET.parse(path).getroot().findall('style'):
            name = item.attrib['name']
            if name in styles:
                raise ValueError('duplicate style: ' + name)
            styles[name] = item
    attrs = {}
    for item in report['styleable_slot_checks']:
        old = attrs.setdefault(item['name'], item['resource_id'])
        if old != item['resource_id']:
            raise ValueError('inconsistent styleable attribute ID')
    if len(set(attrs.values())) != len(attrs):
        raise ValueError('attribute ID alias')

    def merge(name, seen=()):
        if name in seen or len(seen) >= 64:
            raise ValueError('theme parent cycle/depth: ' + name)
        if name not in styles:
            raise ValueError('missing parent: ' + name)
        node = styles[name]
        parent = node.get('parent', name.rsplit('.', 1)[0] if '.' in name else '')
        parent = parent.removeprefix('@android:style/').removeprefix('@style/')
        result = merge(parent, seen + (name,)) if parent else {}
        result.update({i.attrib['name'].removeprefix('android:'): ''.join(i.itertext()).strip()
                       for i in node.findall('item')})
        return result

    demanded_styles = set()
    scalar_resources = {}
    for path in sorted(values.glob('*.xml')):
        for node in ET.parse(path).getroot():
            kind = node.get('type') if node.tag == 'item' else node.tag
            if kind in ('color', 'dimen', 'integer', 'bool', 'string') and node.get('name'):
                scalar_resources[(kind, node.get('name'))] = ''.join(node.itertext()).strip()

    # Constructor defStyleAttr inputs are demands even though they are not
    # members of the styleable arrays. Follow ?attr and style edges to a fixed
    # point, proving every ID against the same pinned ROM field inventory.
    implicit = ('textViewStyle', 'buttonStyle', 'editTextStyle', 'imageButtonStyle',
                'scrollViewStyle', 'horizontalScrollViewStyle')
    extra_demands = set(implicit)
    drawable_attrs = {row['name'] for row in report['styleable_slot_checks'] if row['group'] == 'View'}
    pending_styles = {row['name'] for row in report['themes'] if row['resource_id'] is not None}
    while True:
        before = (len(extra_demands), len(pending_styles), len(drawable_attrs))
        for name in sorted(extra_demands):
            if name not in report['attr_bindings']:
                raise ValueError('unbound demanded ROM attr: ' + name)
            attrs[name] = report['attr_bindings'][name]
        for name in sorted(pending_styles):
            inherited = merge(name)
            for attr_name in sorted(attrs):
                text = inherited.get(attr_name, '')
                ref = re.fullmatch(r'\?(?:android:)?(?:attr/)?(.+)', text)
                if ref and ref[1] in report['attr_bindings']:
                    extra_demands.add(ref[1])
                    if attr_name in drawable_attrs:
                        drawable_attrs.add(ref[1])
                ref = re.fullmatch(r'@(?:android:)?style/(.+)', text)
                if ref and ref[1] in report['style_bindings'] and ref[1] in styles:
                    pending_styles.add(ref[1])
        if before == (len(extra_demands), len(pending_styles), len(drawable_attrs)):
            break
    attribute_demands = [dict(name=name, resource_id=attrs[name]) for name in sorted(extra_demands)]

    projected_references = {}
    strings = []

    def value(text, attr_name, seen=()):
        if text in seen or len(seen) >= 64:
            raise ValueError('projected scalar reference cycle/depth: ' + text)
        ref = re.fullmatch(r'@(?:android:)?(color|dimen|integer|bool|string)/(.+)', text)
        if ref and (ref[1], ref[2]) in scalar_resources:
            return value(scalar_resources[(ref[1], ref[2])], attr_name, seen + (text,))
        style = re.fullmatch(r'@(?:android:)?style/(.+)', text)
        if style and style[1] in report['style_bindings'] and style[1] in styles:
            demanded_styles.add(style[1])
            return dict(state='known', type=1, data=report['style_bindings'][style[1]])
        attr = re.fullmatch(r'\?(?:android:)?(?:attr/)?(.+)', text)
        if attr and attr[1] in attrs and attr[1] in report['attr_bindings']:
            return dict(state='known', type=2, data=report['attr_bindings'][attr[1]])

        drawable = re.fullmatch(r'@(?:android:)?drawable/(.+)', text)
        if drawable and attr_name in drawable_attrs and drawable[1] in report['drawable_bindings']:
            ident = report['drawable_bindings'][drawable[1]]
            if (ident >> 24, (ident >> 16) & 255) != (1, 8):
                raise ValueError('ROM drawable reference type mismatch: ' + text)
            projected_references[drawable[1]] = dict(name=drawable[1], resource_id=ident, resource_type='drawable')
            return dict(state='known', type=1, data=ident)
        fmt = report['attribute_formats'].get(attr_name)
        if not fmt:
            raise ValueError('missing same-ROM attribute format: ' + attr_name)
        mask = fmt['format']
        if text == '@null':
            return dict(state='known', type=1, data=0)
        if mask & 16 and re.fullmatch(r'#[0-9a-fA-F]{3,8}', text) and len(text) in (4, 5, 7, 9):
            digits = text[1:]
            kind = {3:31, 4:30, 6:29, 8:28}[len(digits)]
            if len(digits) in (3, 4):
                digits = ''.join(c * 2 for c in digits)
            if len(digits) == 6:
                digits = 'ff' + digits
            return dict(state='known', type=kind, data=int(digits, 16))
        if mask & (4 | 0x10000 | 0x20000) and re.fullmatch(r'[+-]?(?:0[xX][0-9a-fA-F]+|[0-9]+)', text):
            hexadecimal = 'x' in text.lower()
            n = int(text, 16 if hexadecimal else 10)
            if not -2**31 <= n <= (0xffffffff if hexadecimal else 2**31 - 1):
                raise ValueError('integer out of range')
            return dict(state='known', type=17 if hexadecimal else 16, data=n & 0xffffffff)
        if mask & 8 and text in ('true', 'false'):
            return dict(state='known', type=18, data=0xffffffff if text == 'true' else 0)
        # Exactly integral AOSP complex dimensions use the 23p0 radix.
        m = re.fullmatch(r'(-?\d+)(px|dip|dp|sp|pt|in|mm)', text)
        if mask & 64 and m and -2**23 <= int(m[1]) < 2**23:
            unit = dict(px=0, dip=1, dp=1, sp=2, pt=3, **{'in':4, 'mm':5})[m[2]]
            return dict(state='known', type=5, data=((int(m[1]) << 8) | unit) & 0xffffffff)
        named = named_value(text, attr_name, fmt)
        if named is not None:
            return named
        if mask & 32 and re.fullmatch(r'[+-]?(?:\d+\.\d*|\d*\.\d+)', text):
            return dict(state='known', type=4, data=struct.unpack('<I', struct.pack('<f', float(text)))[0])
        if fmt['format'] & 2 and not text.startswith(('@', '?')):
            if text not in strings:
                strings.append(text)
            return dict(state='known', type=3, data=strings.index(text), cookie=2)
        # Unregistered drawable/selector/ninepatch/animation values keep the
        # original reference text, without inventing an ID or a successful value.
        return dict(state='unsupported', reason='unsupported projected value: ' + text)

    themes = []
    unregistered = []
    counts = Counter()
    for row in report['themes']:
        if row['resource_id'] is None:
            unregistered.append(dict(name=row['name'], reason='无 ROM ID 绑定'))
            continue
        inherited = merge(row['name'])
        projected = []
        for name, ident in sorted(attrs.items(), key=lambda x: x[1]):
            result = value(inherited[name], name) if name in inherited else dict(state='absent')
            counts[result['state']] += 1
            projected.append(dict(id=ident, name=name, **result))
        themes.append(dict(id=row['resource_id'], name=row['name'], attributes=projected))
    style_closure = []
    visited = {row['name'] for row in themes}
    while demanded_styles - visited:
        name = sorted(demanded_styles - visited)[0]
        visited.add(name)
        inherited = merge(name)
        projected = []
        for attr_name, ident in sorted(attrs.items(), key=lambda x: x[1]):
            result = value(inherited[attr_name], attr_name) if attr_name in inherited else dict(state='absent')
            projected.append(dict(id=ident, name=attr_name, **result))
        style_closure.append(dict(id=report['style_bindings'][name], name=name, attributes=projected))
    ids = [row['id'] for row in themes]
    if len(ids) != len(set(ids)):
        raise ValueError('theme resource ID alias')
    # The ADR-0077 mapping has a separate query domain. No ID may be owned by
    # both providers, even when its literal representation happens to match.
    import build_bootdex as builder
    recipe = builder.load_system_resources()
    system_names = {row['name'] for row in recipe['resources']}
    if system_names & set(attrs) or system_names & {t['name'] for t in themes}:
        raise ValueError('system/theme provider name conflict')
    document = dict(schema='ogplay.bounded-theme', schema_version=1, api_level=19,
                    configuration='values', input_sha256=report['input_sha256'],
                    styleables=report['styleable_slot_checks'], attribute_demands=attribute_demands,
                    registered=themes, style_closure=style_closure, unregistered=unregistered,
                    counts=dict(counts), attribute_count=len(attrs),
                    rom_drawables=drawable_closure(), strings=strings, string_pool=string_pool(strings).hex(), attribute_formats=report['attribute_formats'],
                    projected_references=sorted(projected_references.values(), key=lambda r: r['resource_id']))
    return canonical(document)


def check_conflicts(payload, system_payload):
    theme = json.loads(payload)
    system = json.loads(system_payload)
    owned = {t['id'] for t in theme['registered']}
    owned.update(a['id'] for t in theme['registered'] for a in t['attributes'])
    if owned & {r['id'] for r in system['resources']}:
        raise ValueError('ADR-0077/theme projection resource ID conflict')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', nargs='?', choices=('build', 'check'))
    p.add_argument('--self-test', action='store_true')
    p.add_argument('--output', type=Path)
    p.add_argument('--negative-rom-id', action='store_true')
    args = p.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        if not args.mode or not args.output:
            p.error('mode and --output required')
        first = generate(args.negative_rom_id)
        if first != generate(args.negative_rom_id):
            raise ValueError('two theme projections differ')
        if args.mode == 'build':
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(first)
        elif args.output.read_bytes() != first:
            raise ValueError('stale theme projection')
        d = json.loads(first)
        print(json.dumps(dict(registered=len(d['registered']), unregistered=len(d['unregistered']),
                              attributes=d['attribute_count'], counts=d['counts'],
                              sha256=hashlib.sha256(first).hexdigest())))
        return 0
    except (ValueError, OSError, AssertionError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
