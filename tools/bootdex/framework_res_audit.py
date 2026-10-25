#!/usr/bin/env python3
"""DVM-324: hash-locked framework-res identity and scrollbar dependency audit.

This is an audit only. It grants no resource or member implementation credit and
never changes the theme policy, recipe, production JAR, or framework baseline.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.dont_write_bytecode = True
import api19_skeleton
import dex_survey_lib as dx
import dexdis

APK = '.local/aosp/framework-res.apk'
PINNED = {'.local/aosp/' + name: digest for name, digest in api19_skeleton.PINNED.items()}
PINNED.update({
    '.local/aosp/conscrypt.jar': '43ab6b953bd5a9e25f0f0d4411a5aa3f0660d16954a0a84e95ad834558b21a1e',
    '.local/aosp/android.policy.jar': '23af4dcc308b124134c16da18edba3720e8d2910b75e63d4e03b20275e255bae',
    APK: 'b76835c5bd2c2d70c624b33d82e26ba00445196ce73a70381982540796a90f0e',
})
SCROLLBARS = ('scrollbar_handle_horizontal', 'scrollbar_handle_vertical',
              'scrollbar_handle_holo_dark', 'scrollbar_handle_holo_light')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=True) + '\n').encode()


class ResourceTable:
    """Read only the pinned API19 dense table format; reject other formats."""
    def __init__(self, data):
        self.data = data

    def words(self, fmt, at):
        require(0 <= at <= len(self.data) - struct.calcsize(fmt), 'truncated ARSC field')
        return struct.unpack_from(fmt, self.data, at)

    def chunks(self, start, end):
        require(0 <= start <= end <= len(self.data), 'invalid ARSC range')
        while start < end:
            kind, header, size = self.words('<HHI', start)
            require(8 <= header <= size <= end - start, 'invalid ARSC chunk')
            yield start, kind, header, size
            start += size
        require(start == end, 'ARSC chunk alignment')

    def pool(self, at):
        kind, header, size = self.words('<HHI', at)
        require(kind == 1 and 28 <= header <= size <= len(self.data) - at, 'invalid string pool')
        count, styles, flags, start, style_start = self.words('<5I', at + 8)
        require(header + 4 * (count + styles) <= start <= size, 'invalid pool offsets')
        limit = at + (style_start or size)

        def length(pos, width):
            first, = self.words('<B' if width == 1 else '<H', pos)
            pos += width
            bit = 1 << (8 * width - 1)
            if first & bit:
                second, = self.words('<B' if width == 1 else '<H', pos)
                pos += width
                first = ((first & (bit - 1)) << (8 * width)) | second
            return first, pos

        result = []
        for i in range(count):
            offset, = self.words('<I', at + header + 4 * i)
            pos = at + start + offset
            require(at + start <= pos < limit, 'string offset outside pool')
            if flags & 0x100:
                _, pos = length(pos, 1)
                size, pos = length(pos, 1)
                width, encoding = 1, 'utf-8'
            else:
                size, pos = length(pos, 2)
                width, encoding = 2, 'utf-16-le'
            end = pos + size * width
            require(end + width <= limit and self.data[end:end + width] == bytes(width),
                    'invalid string extent/terminator')
            result.append(self.data[pos:end].decode(encoding))
        return result

    def parse(self):
        kind, header, size, package_count = self.words('<HHII', 0)
        require(kind == 2 and header == 12 and size == len(self.data), 'invalid table header')
        packages, entries, strings = [], {}, None
        for at, kind, header, size in self.chunks(header, size):
            if kind == 1:
                require(strings is None, 'duplicate global pool')
                strings = self.pool(at)
                continue
            require(kind == 0x200 and header == 284 and strings is not None, 'unsupported ARSC top-level chunk')
            pid, = self.words('<I', at + 8)
            name = self.data[at + 12:at + 268].decode('utf-16-le').split('\0')[0]
            types_at, _, keys_at, _ = self.words('<4I', at + 268)
            types, keys = self.pool(at + types_at), self.pool(at + keys_at)
            require(pid == 1 and name == 'android', 'unexpected framework resource package')
            packages.append(dict(id=pid, name=name, types=[dict(id=i + 1, name=t) for i, t in enumerate(types)]))
            specs = {}
            for pos, typ, head, extent in self.chunks(at + header, at + size):
                if typ == 1:
                    continue
                require(typ in (0x201, 0x202), 'unsupported resource chunk')
                tid, reserved, reserved2, count = self.words('<BBHI', pos + 8)
                require(0 < tid <= len(types) and not reserved and not reserved2, 'unsupported type layout')
                if typ == 0x202:
                    require(head == 16 and 16 + count * 4 == extent and tid not in specs, 'invalid type spec')
                    specs[tid] = self.words(f'<{count}I', pos + head)
                    continue
                start, config_size = self.words('<II', pos + 16)
                require(head >= 56 and 20 + config_size == head and head + count * 4 <= start <= extent,
                        'invalid type config/index')
                require(tid in specs and count == len(specs[tid]), 'type/spec entry count mismatch')
                config = self.data[pos + 20:pos + head]
                density, = struct.unpack_from('<H', config, 14)
                for i in range(count):
                    offset, = self.words('<I', pos + head + 4 * i)
                    if offset == 0xffffffff:
                        continue
                    ep = pos + start + offset
                    require(pos + start <= ep <= pos + extent - 8, 'entry outside type')
                    esize, flags, key = self.words('<HHI', ep)
                    require(8 <= esize <= pos + extent - ep and key < len(keys), 'invalid entry header')
                    rid = pid << 24 | tid << 16 | i
                    row = entries.setdefault(rid, dict(id=rid, package_id=pid, type_id=tid, entry_id=i,
                        type=types[tid - 1], name=keys[key], spec_flags=specs[tid][i], configurations=[]))
                    require(row['name'] == keys[key] and row['type'] == types[tid - 1], 'conflicting resource identity')
                    variant = dict(config_hex=config.hex(), density=density, entry_flags=flags, complex=bool(flags & 1))
                    if not flags & 1:
                        require(ep + esize + 8 <= pos + extent, 'truncated scalar')
                        vsize, zero, vtype, value = self.words('<HBBI', ep + esize)
                        require(vsize == 8 and zero == 0, 'invalid scalar value')
                        variant.update(value_type=vtype, data=value)
                        if vtype == 3:
                            require(value < len(strings), 'invalid global string index')
                            variant['path'] = strings[value]
                    else:
                        require(esize == 16, 'unsupported complex entry')
                        parent, bag_count = self.words('<II', ep + 8)
                        require(ep + esize + 12 * bag_count <= pos + extent, 'truncated bag')
                        variant.update(parent=parent, bag_count=bag_count,
                                       bag_sha256=sha(self.data[ep + esize:ep + esize + 12 * bag_count]))
                    require(all(v['config_hex'] != variant['config_hex'] for v in row['configurations']), 'duplicate config')
                    row['configurations'].append(variant)
        require(len(packages) == package_count == 1, 'invalid package count')
        return packages, [entries[k] for k in sorted(entries)]


def cross_check(entries, constants):
    by_id = {row['id']: row for row in entries}
    checks, errors = [], []
    for constant in constants:
        row = by_id.get(constant['id'])
        good = row is not None and row['type'] == constant['type'] and row['name'].replace('.', '_') == constant['field']
        check = dict(**constant, matched=good,
                     arsc_name=row['name'] if row else None, arsc_type=row['type'] if row else None)
        checks.append(check)
        if not good:
            errors.append(check)
    return checks, errors


def closure_audit(images):
    catalog = dexdis.load_catalog()
    definitions = {}
    classes = {}
    for rel, (data, image) in images.items():
        for cls in image.classes:
            owner = image.type_name(cls.type_index)
            require(owner not in classes, 'conflicting source class: ' + owner)
            classes[owner] = dict(source=rel, flags=cls.access_flags,
                superclass=image.type_name(cls.superclass_index) if cls.superclass_index is not None else None,
                interfaces=[image.type_name(i) for i in cls.interface_type_indices])
            for m in cls.direct_methods + cls.virtual_methods:
                _, name, desc = image.method_signature(m.method_index)
                lines = dexdis.disassemble(image, data, m.code, catalog) if m.code else []
                definitions[owner + '->' + name + desc] = dict(source=rel, method_id=m.method_index,
                    flags=m.access_flags, disassembly=lines,
                    insns_sha256=sha(data[m.code.insns_offset:m.code.insns_offset + 2 * m.code.insns_size]) if m.code else None)
    chain = [
        'Landroid/view/View;-><init>(Landroid/content/Context;Landroid/util/AttributeSet;I)V',
        'Landroid/view/View;->initializeScrollbars(Landroid/content/res/TypedArray;)V',
        'Landroid/view/View;->initScrollCache()V',
        'Landroid/view/ViewConfiguration;->get(Landroid/content/Context;)Landroid/view/ViewConfiguration;',
        'Landroid/view/ViewConfiguration;-><init>(Landroid/content/Context;)V',
        'Landroid/view/Display;->getRealSize(Landroid/graphics/Point;)V',
        'Landroid/view/Display;->updateDisplayInfoLocked()V',
        'Landroid/hardware/display/DisplayManagerGlobal;->getDisplayInfo(I)Landroid/view/DisplayInfo;',
        'Landroid/hardware/display/IDisplayManager;->getDisplayInfo(I)Landroid/view/DisplayInfo;',
    ]
    edges = []
    for caller, target in zip(chain, chain[1:]):
        sites = [line.strip() for line in definitions[caller]['disassembly'] if 'invoke-' in line and line.endswith(' ' + target)]
        require(sites, 'missing fixed ROM closure edge: ' + caller + ' -> ' + target)
        edges.append(dict(caller=caller, target=target, sites=sites,
                          resolution='declared DEX reference; virtual/interface dispatch is not a unique-target proof'))
    binder_interface = 'Landroid/hardware/display/IDisplayManager;'
    require('Landroid/os/IInterface;' in classes[binder_interface]['interfaces'], 'missing Binder ancestry')
    extras = [
        'Landroid/view/View$ScrollabilityCache;-><init>(Landroid/view/ViewConfiguration;Landroid/view/View;)V',
        'Landroid/widget/ScrollBarDrawable;-><init>()V',
        'Landroid/graphics/drawable/NinePatchDrawable;-><init>(Landroid/content/res/Resources;Landroid/graphics/Bitmap;[BLandroid/graphics/Rect;Ljava/lang/String;)V',
        'Landroid/graphics/NinePatch;-><init>(Landroid/graphics/Bitmap;[BLjava/lang/String;)V',
        'Landroid/hardware/display/IDisplayManager$Stub$Proxy;->getDisplayInfo(I)Landroid/view/DisplayInfo;',
    ]
    proxy = definitions[extras[-1]]['disassembly']
    require(any('Landroid/os/IBinder;->transact(' in line for line in proxy), 'missing concrete Binder transaction')
    return dict(status='stop-out-of-scope-rom-closure', runtime_credit=False,
        condition='scrollbars flag nonzero; first ViewConfiguration for this density has no cached instance',
        boundary='ROM Display.getRealSize is not implemented by the existing Display adapter; its ROM body reaches IDisplayManager/IInterface',
        caveat='This is a static ROM dependency witness, not a game run or proof that every virtual target uses Binder. A new bounded adapter could cut it, but no such adapter is admitted by this audit.',
        chain=edges, binder_interface=classes[binder_interface],
        bodies={key: definitions[key] for key in sorted(set(chain + extras))})


def inspect():
    inputs, images, constants = {}, {}, []
    for rel, expected in sorted(PINNED.items()):
        raw = (ROOT / rel).read_bytes()
        require(sha(raw) == expected, 'pinned input mismatch: ' + rel)
        with zipfile.ZipFile(ROOT / rel) as archive:
            item = archive.getinfo('resources.arsc' if rel == APK else 'classes.dex')
            payload = archive.read(item)
            inputs[rel] = dict(sha256=sha(raw), payload_sha256=sha(payload), zip_time=list(item.date_time),
                               manifest=archive.read('META-INF/MANIFEST.MF').decode('utf-8') if 'META-INF/MANIFEST.MF' in archive.namelist() else None)
            if rel == APK:
                packages, entries = ResourceTable(payload).parse()
                for row in entries:
                    if row['type'] == 'drawable' and row['name'] in SCROLLBARS:
                        for variant in row['configurations']:
                            path = variant.get('path', '')
                            require(path.endswith('.9.png'), 'scrollbar variant is not a ninepatch')
                            png = archive.read(path)
                            require(png.startswith(b'\x89PNG\r\n\x1a\n'), 'invalid PNG signature')
                            variant.update(file_sha256=sha(png), file_size=len(png))
                continue
        image = dx.parse_dex(payload)
        images[rel] = payload, image
        for cls in image.classes:
            owner = image.type_name(cls.type_index)
            if not owner.startswith(('Landroid/R$', 'Lcom/android/internal/R$')) or owner.endswith('R$styleable;'):
                continue
            kind = owner.split('$')[1][:-1]
            for field, value in zip(cls.static_fields, dx.static_values(payload, image, cls)):
                if not isinstance(value, int):
                    continue
                name = image.strings[image.field_ids[field.field_index][2]]
                constants.append(dict(source=rel, owner=owner, field=name, type=kind, id=value))
    checks, errors = cross_check(entries, constants)
    required = set(SCROLLBARS) | {'btn_default', 'btn_default_holo_dark', 'btn_default_holo_light'}
    drawables = [r for r in entries if r['type'] == 'drawable' and r['name'] in required]
    require({r['name'] for r in drawables} == required, 'missing demanded drawable')
    return dict(schema_version=1, task='DVM-324', accepted_projection=False,
        status='id-audit-rejected' if errors else 'id-audit-passed-runtime-closure-stopped',
        provenance_claim='Hash-locked inputs supplied from the same ROM directory; shared R constants agree. This is not a cryptographic proof of a common build.',
        inputs=inputs, packages=packages, entries=entries, r_constant_checks=checks,
        errors=errors, counts=dict(entries=len(entries), r_constants=len(checks), id_conflicts=len(errors)),
        demanded_drawables=drawables, closure=closure_audit(images))


def self_test():
    row = dict(id=0x01080001, type='drawable', name='fixture')
    constant = dict(id=row['id'], type='drawable', field='fixture')
    require(not cross_check([row], [constant])[1], 'positive identity control')
    for change in ({'id': row['id'] + 1}, {'type': 'color'}, {'field': 'wrong'}):
        require(bool(cross_check([row], [{**constant, **change}])[1]), 'negative identity control')
    for data in (b'', bytes(12), struct.pack('<HHII', 2, 12, 100, 1)):
        try:
            ResourceTable(data).parse()
        except ValueError:
            pass
        else:
            raise ValueError('malformed table accepted')
    print('framework-res audit self-test: identity positive; unknown/conflict/type and malformed table negatives passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
        if args.output:
            first = inspect()
            payload = canonical(first)
            require(payload == canonical(inspect()), 'two audits differ')
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(payload)
            print(json.dumps(dict(status=first['status'], counts=first['counts'], sha256=sha(payload))))
            return 2 if first['errors'] else 0
        require(args.self_test, '--output or --self-test required')
        return 0
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
