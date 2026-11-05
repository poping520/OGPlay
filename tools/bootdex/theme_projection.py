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
# Must match the ThemeResources::Load JSON bound.
MAXIMUM_BYTES = 8 * 1024 * 1024


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


def reference_index(rows):
    """Bind names to identities only; never inspect a target's configurations."""
    result, ids, types = {}, set(), {}
    for row in rows:
        kind, name, ident, type_id = (row[k] for k in ('resource_type', 'name', 'resource_id', 'type_id'))
        if (ident >> 24, (ident >> 16) & 255) != (1, type_id) or not 0 < type_id < 256:
            raise ValueError('ROM resource reference type mismatch')
        if types.setdefault(kind, type_id) != type_id or (kind, name) in result or ident in ids:
            raise ValueError('ROM resource reference identity conflict')
        ids.add(ident)
        result[(kind, name)] = dict(name=name, resource_type=kind, resource_id=ident, type_id=type_id)
    if len(types) != len(set(types.values())):
        raise ValueError('ROM resource type alias')
    return result


def self_test():
    report = theme_inputs.inspect_inputs()
    formats = report['attribute_formats']
    refs = reference_index(report['resource_bindings'])
    for kind in ('drawable', 'color', 'anim', 'layout', 'xml', 'string', 'dimen', 'style'):
        assert any(k[0] == kind for k in refs)
    assert ('drawable', 'text_select_handle_left') in refs
    assert ('layout', 'text_edit_suggestion_item') in refs
    assert ('drawable', 'missing_OGPlay_reference') not in refs
    row = refs[('drawable', 'text_select_handle_left')]
    for rows in ([row, row], [{**row, 'type_id': 6}]):
        try:
            reference_index(rows)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid reference identity accepted')
    assert named_value('normal', 'textStyle', formats['textStyle']) == dict(state='known', type=17, data=0)
    assert named_value('bold|italic', 'textStyle', formats['textStyle'])['data'] == 3
    assert named_value('normal', 'typeface', formats['typeface'])['data'] == 0
    assert named_value('sans', 'typeface', formats['typeface'])['data'] == 1
    assert named_value('bold|typo', 'textStyle', formats['textStyle'])['state'] == 'unsupported'
    assert named_value('bold', 'typeface', formats['typeface'])['state'] == 'unsupported'
    assert named_value('normal', 'fontFamily', formats['fontFamily']) is None
    with zipfile.ZipFile(ROOT / rom.APK) as archive:
        _, rows = rom.ResourceTable(archive.read('resources.arsc')).parse()
        by_id = {r['id']: r for r in rows}
        selectors = color_resources()
        assert sum('xml' in r for r in selectors) == sum(
            v.get('path', '').startswith('res/color/') for r in rows for v in r['configurations'])
        row = next(r for r in selectors if r['name'] == 'primary_text_light')
        raw = bytes.fromhex(row['xml'])
        validate_color_xml(raw, by_id)
        changes = []
        for at, kind, header, size in rom.ResourceTable(raw).chunks(8, len(raw)):
            if kind == 0x102:
                _, name, start, width, count, _, _, _ = struct.unpack_from('<II6H', raw, at + 16)
                if not count:
                    changed = bytearray(raw)
                    struct.pack_into('<I', changed, at + 20, 0)
                    changes.append(changed)  # non-selector root
                else:
                    for i in range(count):
                        a = at + 16 + start + i * width
                        if raw[a + 15] == 1:
                            changed = bytearray(raw)
                            struct.pack_into('<I', changed, a + 16, 0x0106ffff)
                            changes.append(changed)  # unknown color reference
                    changed = bytearray(raw)
                    struct.pack_into('<I', changed, at + 16 + start + 4, 0xffffffff)
                    changes.append(changed)  # unknown attribute
                    break
        assert len(changes) >= 3
        for changed in changes:
            try:
                validate_color_xml(changed, by_id)
            except (ValueError, IndexError):
                pass
            else:
                raise AssertionError('invalid ColorStateList XML accepted')
    with zipfile.ZipFile(ROOT / rom.APK) as archive:
        _, rows = rom.ResourceTable(archive.read('resources.arsc')).parse()
        by_id = {r['id']: r for r in rows}
        for name in ('progress_horizontal_holo_dark', 'progress_indeterminate_horizontal_holo'):
            row = next(r for r in rows if r['name'] == name and r['type'] == 'drawable')
            raw = archive.read(row['configurations'][0]['path'])
            facts = horizontal_xml_facts(raw, by_id, name)
            assert len(facts['references']) == (3 if name == 'progress_horizontal_holo_dark' else 8)
            mutations = []
            for at, kind, header, size in rom.ResourceTable(raw).chunks(8, len(raw)):
                if kind != 0x102:
                    continue
                _, tag, start, width, count, _, _, _ = struct.unpack_from('<II6H', raw, at+16)
                for i in range(count):
                    pos = at+16+start+i*width
                    if raw[pos+15] == 1:
                        changed = bytearray(raw)
                        struct.pack_into('<I', changed, pos+16, 0x0108ffff)
                        mutations.append(changed)
            assert mutations
            for changed in mutations:
                try:
                    horizontal_xml_facts(changed, by_id, name)
                except (ValueError, KeyError, IndexError):
                    pass
                else:
                    raise AssertionError('unsealed horizontal XML reference accepted')
    print('same-ROM enum/flags, color and horizontal drawable grammar/reference positive and negative checks passed')


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


def view_configuration_values():
    """Read exact default scalar facts from the pinned APK, never source defaults."""
    demands = {
        'config_viewConfigurationTouchSlop': (0x01050008, 5),
        'config_overrideHasPermanentMenuKey': (0x010e0049, 16),
        'config_ui_enableFadingMarquee': (0x01110013, 18),
        'config_showNavigationBar': (0x01110044, 18),
    }
    with zipfile.ZipFile(ROOT / rom.APK) as archive:
        _, rows = rom.ResourceTable(archive.read('resources.arsc')).parse()
    result = []
    for name, (ident, kind) in demands.items():
        matches = [r for r in rows if r['name'] == name and r['id'] == ident]
        if len(matches) != 1 or len(matches[0]['configurations']) != 1:
            raise ValueError('unreviewed ViewConfiguration resource identity/configuration: ' + name)
        variant = matches[0]['configurations'][0]
        if (any(bytes.fromhex(variant['config_hex'])[4:]) or variant['complex']
                or variant['value_type'] != kind):
            raise ValueError('unsupported ViewConfiguration resource: ' + name)
        result.append(dict(name=name, id=ident, type=kind, data=variant['data']))
    return sorted(result, key=lambda r: r['id'])


def animation_closure():
    """ADR-0174: only the exact default linear interpolator XML."""
    with zipfile.ZipFile(ROOT / rom.APK) as archive:
        _, rows = rom.ResourceTable(archive.read('resources.arsc')).parse()
        row, = [r for r in rows if r['type'] == 'anim' and r['name'] == 'linear_interpolator']
        if row['id'] != 0x010a000b or len(row['configurations']) != 1:
            raise ValueError('unreviewed ROM animation identity/configurations')
        v, = row['configurations']
        if (any(bytes.fromhex(v['config_hex'])[4:]) or v['complex'] or v['value_type'] != 3
                or v['path'] != 'res/anim/linear_interpolator.xml'):
            raise ValueError('unsupported ROM animation configuration')
        raw = archive.read(v['path'])
        table = rom.ResourceTable(raw)
        strings, roots, ends = [], 0, 0
        for at, kind, header, size in table.chunks(8, len(raw)):
            if kind == 1: strings = table.pool(at)
            elif kind == 0x102:
                _, name, start, width, count, _, _, _ = table.words('<II6H', at + 16)
                if strings[name] != 'linearInterpolator' or count or roots:
                    raise ValueError('unreviewed ROM interpolator element/attributes')
                roots += 1
            elif kind == 0x103:
                if strings[table.words('<II', at + 16)[1]] != 'linearInterpolator':
                    raise ValueError('invalid ROM interpolator end')
                ends += 1
            elif kind not in (0x100, 0x101):
                raise ValueError('unreviewed ROM interpolator chunk')
        if (roots, ends) != (1, 1): raise ValueError('invalid ROM interpolator XML')
        return [dict(id=row['id'], name=row['name'], path=v['path'], config_hex=v['config_hex'],
                     sha256=rom.sha(raw), references=[], xml=raw.hex())]


def layout_closure():
    """Two exact same-ROM default layouts; content credit separate from identity."""
    roots = {'simple_spinner_item': 0x01090008, 'simple_spinner_dropdown_item': 0x01090009}
    with zipfile.ZipFile(ROOT / rom.APK) as archive:
        _, rows = rom.ResourceTable(archive.read('resources.arsc')).parse()
        by_id = {row['id']: row for row in rows}
        result = []
        for name, ident in roots.items():
            row = by_id[ident]
            if row['name'] != name or row['type'] != 'layout' or len(row['configurations']) != 1:
                raise ValueError('unreviewed ROM layout identity/configurations: ' + name)
            variant, = row['configurations']
            if (any(bytes.fromhex(variant['config_hex'])[4:]) or variant['complex']
                    or variant['value_type'] != 3 or variant['path'] != 'res/layout/' + name + '.xml'):
                raise ValueError('unsupported ROM layout configuration: ' + name)
            raw = archive.read(variant['path'])
            table = rom.ResourceTable(raw)
            strings, resource_ids, stack, references = [], [], [], set()
            roots_seen = 0
            for at, kind, header, size in table.chunks(8, len(raw)):
                if kind == 1:
                    strings = table.pool(at)
                elif kind == 0x180:
                    resource_ids = list(table.words('<%dI' % ((size - header) // 4), at + header))
                elif kind == 0x102:
                    _, tag, start, width, count, _, _, _ = table.words('<II6H', at + 16)
                    if stack or strings[tag] != ('TextView' if ident == 0x01090008 else 'CheckedTextView'):
                        raise ValueError('unreviewed ROM layout element')
                    roots_seen += 1
                    stack.append(tag)
                    for i in range(count):
                        ns, key, text, vs, zero, typ, data = table.words('<IIIHBBI', at + 16 + start + i * width)
                        aid = resource_ids[key] if key < len(resource_ids) else 0
                        if aid and (aid not in by_id or by_id[aid]['type'] != 'attr'):
                            raise ValueError('unknown ROM layout attribute')
                        if typ in (1, 2):
                            if data not in by_id or by_id[data]['type'] not in ('style', 'attr', 'color', 'id'):
                                raise ValueError('unreviewed ROM layout reference')
                            references.add(data)
                        elif typ not in (16, 17, 18):
                            raise ValueError('unreviewed ROM layout value')
                elif kind == 0x103:
                    if not stack or table.words('<II', at + 16)[1] != stack.pop():
                        raise ValueError('invalid ROM layout nesting')
                elif kind not in (0x100, 0x101):
                    raise ValueError('unreviewed ROM layout XML chunk')
            if stack or roots_seen != 1:
                raise ValueError('invalid ROM layout root')
            result.append(dict(id=ident, name=name, path=variant['path'],
                               config_hex=variant['config_hex'], sha256=rom.sha(raw),
                               references=sorted(references), xml=raw.hex()))
    return result


# Holo indeterminate roots: a layer-list of two counter-rotating spinner bitmaps, by dip size.
HOLO_SPINNERS = {'progress_small_holo': 16, 'progress_medium_holo': 48, 'progress_large_holo': 76}


def horizontal_xml_facts(raw, by_id, root_name):
    """Exact API19 horizontal closures; no general drawable XML admission."""
    table = rom.ResourceTable(raw)
    strings, ids, stack, events, references = [], [], [], [], set()
    for at, kind, header, size in table.chunks(8, len(raw)):
        if kind == 1:
            strings = table.pool(at)
        elif kind == 0x180:
            ids = list(table.words('<%dI' % ((size-header)//4), at+header))
        elif kind == 0x102:
            _, tag, start, width, count, _, _, _ = table.words('<II6H', at+16)
            tag = strings[tag]
            attrs = []
            for i in range(count):
                ns, key, text, vs, zero, typ, data = table.words('<IIIHBBI', at+16+start+i*width)
                name = by_id[ids[key]]['name']
                if typ == 1:
                    value = by_id[data]['name']
                    if name == 'drawable':
                        references.add(data)
                elif typ == 3:
                    value = strings[data]
                else:
                    value = data
                attrs.append((name, typ, value))
            events.append((tag, sorted(attrs)))
            stack.append(tag)
        elif kind == 0x103:
            _, tag = table.words('<II', at+16)
            if not stack or stack.pop() != strings[tag]:
                raise ValueError('unbalanced horizontal drawable XML')
            events.append(('/' + strings[tag], []))
        elif kind not in (0x100, 0x101):
            raise ValueError('unsupported horizontal drawable XML chunk')
    if root_name == 'progress_horizontal_holo_dark':
        expected = [('layer-list', []),
            ('item', [('drawable', 1, 'progress_bg_holo_dark'), ('id', 1, 'background')]), ('/item', []),
            ('item', [('id', 1, 'secondaryProgress')]),
            ('scale', [('drawable', 1, 'progress_secondary_holo_dark'), ('scaleWidth', 3, '100%')]),
            ('/scale', []), ('/item', []), ('item', [('id', 1, 'progress')]),
            ('scale', [('drawable', 1, 'progress_primary_holo_dark'), ('scaleWidth', 3, '100%')]),
            ('/scale', []), ('/item', []), ('/layer-list', [])]
        kind = 'layer-list'
    elif root_name == 'progress_indeterminate_horizontal_holo':
        expected = [('animation-list', [('oneshot', 18, 0)])]
        for i in range(1, 9):
            expected += [('item', [('drawable', 1, 'progressbar_indeterminate_holo' + str(i)), ('duration', 16, 50)]), ('/item', [])]
        expected += [('/animation-list', [])]
        kind = 'animation-list'
    elif root_name in HOLO_SPINNERS:
        pivots = [('pivotX', 6, 0x40000030), ('pivotY', 6, 0x40000030)]
        outer, inner = [('spinner_%d_%s_holo' % (HOLO_SPINNERS[root_name], side)) for side in ('outer', 'inner')]
        expected = [('layer-list', []), ('item', []),
            ('rotate', [('drawable', 1, outer), ('fromDegrees', 4, 0), ('toDegrees', 4, 0x44870000)] + pivots),
            ('/rotate', []), ('/item', []), ('item', []),
            ('rotate', [('drawable', 1, inner), ('fromDegrees', 4, 0x44340000), ('toDegrees', 4, 0)] + pivots),
            ('/rotate', []), ('/item', []), ('/layer-list', [])]
        expected = [(tag, sorted(attrs)) for tag, attrs in expected]
        kind = 'rotate-layers'
    else:
        raise ValueError('unreviewed horizontal drawable root')
    if stack or events != expected:
        raise ValueError('unreviewed horizontal drawable grammar/reference')
    return dict(kind=kind, xml=raw.hex(), references=sorted(references))


def drawable_closure():
    """Reviewed same-ROM widget skins and their exact drawable reference closures."""
    with zipfile.ZipFile(ROOT / rom.APK) as archive:
        _, rows = rom.ResourceTable(archive.read('resources.arsc')).parse()
        by_id = {row['id']: row for row in rows}
        root, = [row for row in rows if row['type'] == 'drawable' and row['name'] == 'edit_text']
        thumbs = {
            'scrollbar_handle_horizontal': 0x0108052a,
            'scrollbar_handle_vertical': 0x0108052b,
            'scrollbar_handle_holo_dark': 0x01080528,
            'scrollbar_handle_holo_light': 0x01080529,
        }
        for name, ident in thumbs.items():
            matches = [row for row in rows if row['type'] == 'drawable' and row['name'] == name]
            if len(matches) != 1 or matches[0]['id'] != ident:
                raise ValueError('scrollbar thumb ROM name/ID mismatch: ' + name)
            variants = matches[0]['configurations']
            if len(variants) != 1 or not variants[0]['path'].endswith('.9.png'):
                raise ValueError('scrollbar thumb requires one ninepatch leaf: ' + name)
        button, = [row for row in rows if row['type'] == 'drawable' and row['name'] == 'btn_default']
        if button['id'] != 0x01080004:
            raise ValueError('btn_default ROM name/ID mismatch')
        holo_button, = [row for row in rows if row['type'] == 'drawable' and row['name'] == 'btn_default_holo_dark']
        if holo_button['id'] != 0x0108011d:
            raise ValueError('btn_default_holo_dark ROM name/ID mismatch')
        dropdown, = [row for row in rows if row['type'] == 'drawable' and row['name'] == 'btn_dropdown']
        if dropdown['id'] != 0x01080006:
            raise ValueError('btn_dropdown ROM name/ID mismatch')
        # Reviewed indeterminate rotate roots and the one spinner bitmap each references.
        rotates = {}
        for rotate_name, rotate_id, spinner_name, spinner_id in (
                ('progress_medium_white', 0x010804cf, 'spinner_white_48', 0x01080580),
                ('progress_large_white', 0x010804cc, 'spinner_white_76', 0x01080581),
                ('progress_small_white', 0x010804d7, 'spinner_white_16', 0x0108057f),
                ('progress_medium', 0x010804cd, 'spinner_black_48', 0x01080566),
                ('progress_large', 0x010804ca, 'spinner_black_76', 0x01080567),
                ('progress_small', 0x010804d4, 'spinner_black_16', 0x01080564)):
            progress, = [r for r in rows if r['type'] == 'drawable' and r['name'] == rotate_name]
            spinner, = [r for r in rows if r['type'] == 'drawable' and r['name'] == spinner_name]
            if progress['id'] != rotate_id or spinner['id'] != spinner_id:
                raise ValueError('progress drawable ROM name/ID mismatch')
            rotates[rotate_id] = spinner_id
        horizontal = {'progress_horizontal_holo_dark': 0x010804c7,
                      'progress_indeterminate_horizontal_holo': 0x010804c9,
                      'progress_small_holo': 0x010804d5,
                      'progress_medium_holo': 0x010804ce,
                      'progress_large_holo': 0x010804cb}
        for name, ident in horizontal.items():
            if by_id[ident]['name'] != name or by_id[ident]['type'] != 'drawable':
                raise ValueError('horizontal ROM identity mismatch')
        pending, result = [root['id'], button['id'], holo_button['id'], dropdown['id'], *rotates, *thumbs.values(), *horizontal.values()], []
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
                    if ident in horizontal.values():
                        facts.update(horizontal_xml_facts(raw, by_id, row['name']))
                        pending.extend(facts['references'])
                        variants.append(facts)
                        continue
                    if ident in rotates:
                        table = rom.ResourceTable(raw)
                        strings, ids, roots, targets = [], [], 0, []
                        for at, kind, header, size in table.chunks(8, len(raw)):
                            if kind == 1: strings = table.pool(at)
                            elif kind == 0x180: ids = list(table.words('<%dI' % ((size-header)//4), at+header))
                            elif kind == 0x102:
                                _, tag, start, width, count, _, _, _ = table.words('<II6H', at+16)
                                if strings[tag] != 'animated-rotate' or roots: raise ValueError('invalid progress root')
                                roots += 1
                                for i in range(count):
                                    ns, key, text, vs, zero, typ, data = table.words('<IIIHBBI', at+16+start+i*width)
                                    attr = by_id[ids[key]]['name']
                                    if attr == 'drawable' and typ == 1 and data == rotates[ident]: targets.append(data)
                                    elif attr in ('pivotX', 'pivotY') and typ == 6 and data == 0x40000030: pass
                                    elif attr == 'framesCount' and typ == 16 and data == 12: pass
                                    elif attr == 'frameDuration' and typ == 16 and data == 100: pass
                                    else: raise ValueError('unreviewed animated-rotate attribute: '+attr+' '+hex(data))
                            elif kind not in (0x100,0x101,0x103): raise ValueError('unreviewed progress XML chunk')
                        if roots != 1 or targets != [rotates[ident]]: raise ValueError('unclosed progress drawable')
                        facts.update(kind='animated-rotate', xml=raw.hex(), references=targets)
                        pending.extend(targets)
                        variants.append(facts)
                        continue
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
                                 stretch_x=list(struct.unpack_from('>%di' % nx, chunk, 32)),
                                 stretch_y=list(struct.unpack_from('>%di' % ny, chunk, 32 + 4 * nx)),
                                 padding=[left, top, right, bottom], optical=optical)
                elif (ident in rotates.values() or row['name'] in {'progressbar_indeterminate_holo' + str(i) for i in range(1, 9)} or
                      row['name'] in {'spinner_%d_%s_holo' % (size, side) for size in HOLO_SPINNERS.values() for side in ('outer', 'inner')}) and path.endswith('.png'):
                    if not raw.startswith(b'\x89PNG\r\n\x1a\n'): raise ValueError('invalid spinner PNG')
                    width, height = struct.unpack_from('>II', raw, 16)
                    facts.update(kind='bitmap', png=raw.hex(), width=width, height=height)
                else:
                    raise ValueError('unreviewed skin drawable type')
                variants.append(facts)
            result.append(dict(id=ident, name=row['name'], variants=variants))
        return sorted(result, key=lambda row: row['id'])


def validate_color_xml(raw, by_id):
    """Validate the capability grammar before the unmodified ROM XML parser."""
    table = rom.ResourceTable(raw)
    if table.words('<HHI', 0) != (3, 8, len(raw)):
        raise ValueError('invalid ColorStateList XML header')
    strings, ids, stack, count = [], [], [], 0
    root_seen = False
    for at, kind, header, size in table.chunks(8, len(raw)):
        if kind == 1:
            strings = table.pool(at)
        elif kind == 0x180:
            ids = list(table.words('<%dI' % ((size - header) // 4), at + header))
        elif kind == 0x102:
            _, name, start, width, number, _, _, _ = table.words('<II6H', at + 16)
            tag = strings[name]
            if (not stack and (tag != 'selector' or root_seen)) or (stack and (stack != ['selector'] or tag != 'item')):
                raise ValueError('unsupported ColorStateList XML structure')
            if tag == 'selector':
                root_seen = True
            if tag == 'selector' and number:
                raise ValueError('unknown ColorStateList selector attribute')
            color = False
            attributes = set()
            for i in range(number):
                ns, name, text, vs, zero, typ, data = table.words('<IIIHBBI', at + 16 + start + i * width)
                aid = ids[name] if name < len(ids) else 0
                attr = by_id.get(aid, {})
                if aid in attributes:
                    raise ValueError('duplicate ColorStateList attribute')
                attributes.add(aid)
                if ns >= len(strings) or strings[ns] != 'http://schemas.android.com/apk/res/android':
                    raise ValueError('unknown ColorStateList attribute namespace')
                if attr.get('type') != 'attr':
                    raise ValueError('unknown ColorStateList attribute')
                if attr['name'] == 'color':
                    color = True
                    if typ == 1:
                        resolve_rom_color(data, by_id)
                    elif not 28 <= typ <= 31:
                        raise ValueError('unsupported ColorStateList color value')
                elif not attr['name'].startswith('state_') or typ != 18:
                    raise ValueError('unknown ColorStateList item attribute')
            if tag == 'item':
                if not color:
                    raise ValueError('ColorStateList item without color')
                count += 1
            stack.append(tag)
        elif kind == 0x103:
            _, name = table.words('<II', at + 16)
            if not stack or stack.pop() != strings[name]:
                raise ValueError('invalid ColorStateList end tag')
        elif kind not in (0x100, 0x101):
            raise ValueError('unsupported ColorStateList XML chunk')
    if stack or not count:
        raise ValueError('empty/unclosed ColorStateList')


def resolve_rom_color(ident, by_id, seen=()):
    if ident in seen or len(seen) >= 64:
        raise ValueError('ROM color reference cycle/depth')
    row = by_id.get(ident)
    if not row or row['type'] != 'color':
        raise ValueError('unknown ROM color reference')
    defaults = [v for v in row['configurations'] if not any(bytes.fromhex(v['config_hex'])[4:])]
    if len(defaults) != 1:
        raise ValueError('unsupported ROM color configuration')
    v = defaults[0]
    if v['value_type'] == 1:
        return resolve_rom_color(v['data'], by_id, seen + (ident,))
    if not 28 <= v['value_type'] <= 31:
        raise ValueError('ROM color reference is not scalar')
    return dict(type=v['value_type'], data=v['data'])


def rom_style_inventory(domain, projected):
    """Classify every unprojected ROM style by its ARSC parent chain.

    An APK compiled against a later SDK may name an API 19 ID that is an
    unrelated ROM style. ResTable resolves it like any bag, so a chain that
    sets no projected attribute is inert within the domain. Any other
    configuration or chain shape stays non-inert and fails explicitly.
    """
    with zipfile.ZipFile(ROOT / rom.APK) as archive:
        _, rows = rom.ResourceTable(archive.read('resources.arsc')).parse()
    styles = {row['id']: row for row in rows if row['type'] == 'style'}

    def chain(ident, seen=()):
        if ident in seen or len(seen) >= 64:
            raise ValueError('ROM style parent cycle/depth')
        row = styles.get(ident)
        if row is None or len(row['configurations']) != 1:
            return None
        v = row['configurations'][0]
        if any(bytes.fromhex(v['config_hex'])[4:]) or not v['complex']:
            return None
        names = {item['name'] for item in v['bag']}
        if v['parent']:
            inherited = chain(v['parent'], seen + (ident,))
            if inherited is None:
                return None
            names |= inherited
        return names

    result = []
    for ident, row in sorted(styles.items()):
        if ident in projected:
            continue
        names = chain(ident)
        result.append(dict(id=ident, name=row['name'], inert=names is not None and not names & domain))
    return result


def color_resources():
    with zipfile.ZipFile(ROOT / rom.APK) as archive:
        _, rows = rom.ResourceTable(archive.read('resources.arsc')).parse()
        by_id = {r['id']: r for r in rows}
        result = []
        for row in rows:
            if row['type'] != 'color':
                continue
            entry = dict(id=row['id'], name=row['name'])
            try:
                defaults = [v for v in row['configurations'] if not any(bytes.fromhex(v['config_hex'])[4:])]
                if len(defaults) != 1:
                    raise ValueError('unsupported ROM color configuration')
                v = defaults[0]
                if v.get('path', '').startswith('res/color/') and v['path'].endswith('.xml'):
                    raw = archive.read(v['path'])
                    validate_color_xml(raw, by_id)
                    entry.update(type=1, data=row['id'], xml=raw.hex(), path=v['path'], sha256=rom.sha(raw))
                else:
                    entry.update(resolve_rom_color(row['id'], by_id))
            except ValueError as error:
                entry['unsupported'] = str(error)
            result.append(entry)
        return result


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
    # Constructor defStyleAttr inputs are demands even though they are not
    # members of the styleable arrays. Follow ?attr and style edges to a fixed
    # point, proving every ID against the same pinned ROM field inventory.
    implicit = ('textViewStyle', 'buttonStyle', 'editTextStyle', 'imageButtonStyle',
                'scrollViewStyle', 'horizontalScrollViewStyle', 'spinnerStyle', 'progressBarStyle', 'progressBarStyleHorizontal', 'progressBarStyleSmall', 'progressBarStyleLarge',
                'progressBarStyleInverse', 'progressBarStyleSmallInverse', 'progressBarStyleLargeInverse',
                'spinnerItemStyle', 'spinnerDropDownItemStyle', 'dropdownListPreferredItemHeight')
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

    colors = color_resources()
    references = reference_index(report['resource_bindings'])
    layouts = layout_closure()
    drawables = drawable_closure()
    by_reference_id = {row['resource_id']: row for row in references.values()}
    projected_references = {ident: by_reference_id[ident]
                            for layout in layouts for ident in layout['references']}
    # Sealed XML consumers may read framework IDs as well as drawable references.
    # This grants identity only; loading remains independently sealed.
    for drawable in drawables:
        for variant in drawable['variants']:
            if 'xml' not in variant:
                continue
            raw = bytes.fromhex(variant['xml'])
            table = rom.ResourceTable(raw)
            for at, kind, header, size in table.chunks(8, len(raw)):
                if kind != 0x102:
                    continue
                _, tag, start, width, count, _, _, _ = table.words('<II6H', at+16)
                for i in range(count):
                    _, _, _, _, _, typ, ident = table.words('<IIIHBBI', at+16+start+i*width)
                    if typ == 1:
                        projected_references[ident] = by_reference_id[ident]
    strings = [row['path'] for row in colors if 'xml' in row]
    for row in colors:
        if 'xml' in row:
            row['string_index'] = strings.index(row['path'])

    def value(text, attr_name, seen=()):
        if text in seen or len(seen) >= 64:
            raise ValueError('projected scalar reference cycle/depth: ' + text)
        ref = re.fullmatch(r'@(?:android:)?([a-zA-Z_]+)/(.+)', text)
        if ref:
            row = references.get((ref[1], ref[2]))
            if row is None:
                return dict(state='unsupported', reason='unsupported projected value: ' + text)
            if ref[1] == 'style' and ref[2] in styles:
                demanded_styles.add(ref[2])
            projected_references[row['resource_id']] = row
            return dict(state='known', type=1, data=row['resource_id'])
        attr = re.fullmatch(r'\?(?:android:)?(?:attr/)?(.+)', text)
        if attr and attr[1] in attrs and attr[1] in report['attr_bindings']:
            return dict(state='known', type=2, data=report['attr_bindings'][attr[1]])

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
        # Unknown literals keep the original diagnostic; missing ARSC names
        # are rejected above without granting any resource loader credit.
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
        style_closure.append(dict(id=references[('style', name)]['resource_id'], name=name, attributes=projected))
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
                    rom_styles=rom_style_inventory(set(attrs.values()), set(ids) | {row['id'] for row in style_closure}),
                    counts=dict(counts), attribute_count=len(attrs),
                    rom_colors=colors, rom_drawables=drawables, rom_layouts=layouts, rom_animations=animation_closure(), rom_configuration=view_configuration_values(), strings=strings, string_pool=string_pool(strings).hex(), attribute_formats=report['attribute_formats'],
                    resource_types={r['resource_type']: r['type_id'] for r in references.values()},
                    resource_names=sorted(references.values(), key=lambda r: r['resource_id']),
                    projected_references=sorted(projected_references.values(), key=lambda r: r['resource_id']))
    payload = canonical(document)
    if len(payload) > MAXIMUM_BYTES:
        raise ValueError('theme projection exceeds runtime size bound')
    return payload


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
