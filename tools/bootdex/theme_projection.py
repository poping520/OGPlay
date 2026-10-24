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

ROOT = Path(__file__).resolve().parents[2]
ENTRY = 'META-INF/ogplay/theme-projection.json'
POLICY = Path(__file__).with_name('theme-projection-inputs.json')


def canonical(document):
    return (json.dumps(document, sort_keys=True, separators=(',', ':'), ensure_ascii=True) + '\n').encode()


def generate(negative_rom_id=False):
    report = theme_inputs.inspect_inputs(negative_rom_id)
    policy = json.loads(POLICY.read_text(encoding='utf-8'))
    if report['input_sha256'] != policy['input_sha256']:
        raise ValueError('theme projection pinned source/ROM input mismatch')
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
            if kind in ('color', 'dimen', 'integer', 'bool') and node.get('name'):
                scalar_resources[(kind, node.get('name'))] = ''.join(node.itertext()).strip()

    # Constructor defStyleAttr inputs are demands even though they are not
    # members of the styleable arrays. Follow ?attr and style edges to a fixed
    # point, proving every ID against the same pinned ROM field inventory.
    implicit = ('textViewStyle', 'buttonStyle', 'editTextStyle', 'imageButtonStyle',
                'scrollViewStyle', 'horizontalScrollViewStyle')
    extra_demands = set(implicit)
    pending_styles = {row['name'] for row in report['themes'] if row['resource_id'] is not None}
    while True:
        before = (len(extra_demands), len(pending_styles))
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
                ref = re.fullmatch(r'@(?:android:)?style/(.+)', text)
                if ref and ref[1] in report['style_bindings'] and ref[1] in styles:
                    pending_styles.add(ref[1])
        if before == (len(extra_demands), len(pending_styles)):
            break
    attribute_demands = [dict(name=name, resource_id=attrs[name]) for name in sorted(extra_demands)]

    def value(text, seen=()):
        if text in seen or len(seen) >= 64:
            raise ValueError('projected scalar reference cycle/depth: ' + text)
        ref = re.fullmatch(r'@(?:android:)?(color|dimen|integer|bool)/(.+)', text)
        if ref and (ref[1], ref[2]) in scalar_resources:
            return value(scalar_resources[(ref[1], ref[2])], seen + (text,))
        style = re.fullmatch(r'@(?:android:)?style/(.+)', text)
        if style and style[1] in report['style_bindings'] and style[1] in styles:
            demanded_styles.add(style[1])
            return dict(state='known', type=1, data=report['style_bindings'][style[1]])
        attr = re.fullmatch(r'\?(?:android:)?(?:attr/)?(.+)', text)
        if attr and attr[1] in attrs and attr[1] in report['attr_bindings']:
            return dict(state='known', type=2, data=report['attr_bindings'][attr[1]])

        if text == '@null':
            return dict(state='known', type=1, data=0)
        if re.fullmatch(r'#[0-9a-fA-F]{3,8}', text) and len(text) in (4, 5, 7, 9):
            digits = text[1:]
            kind = {3:31, 4:30, 6:29, 8:28}[len(digits)]
            if len(digits) in (3, 4):
                digits = ''.join(c * 2 for c in digits)
            if len(digits) == 6:
                digits = 'ff' + digits
            return dict(state='known', type=kind, data=int(digits, 16))
        if re.fullmatch(r'-?\d+', text):
            n = int(text)
            if not -2**31 <= n < 2**31:
                raise ValueError('integer out of range')
            return dict(state='known', type=16, data=n & 0xffffffff)
        if text in ('true', 'false'):
            return dict(state='known', type=18, data=0xffffffff if text == 'true' else 0)
        # Exactly integral AOSP complex dimensions use the 23p0 radix.
        m = re.fullmatch(r'(-?\d+)(px|dip|dp|sp|pt|in|mm)', text)
        if m and -2**23 <= int(m[1]) < 2**23:
            unit = dict(px=0, dip=1, dp=1, sp=2, pt=3, **{'in':4, 'mm':5})[m[2]]
            return dict(state='known', type=5, data=((int(m[1]) << 8) | unit) & 0xffffffff)
        # Drawable/selector/ninepatch/animation values deliberately keep the
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
            result = value(inherited[name]) if name in inherited else dict(state='absent')
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
            result = value(inherited[attr_name]) if attr_name in inherited else dict(state='absent')
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
                    counts=dict(counts), attribute_count=len(attrs))
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
    p.add_argument('mode', choices=('build', 'check'))
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--negative-rom-id', action='store_true')
    args = p.parse_args()
    try:
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
