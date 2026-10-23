#!/usr/bin/env python3
"""Exact, opt-in fixed-ROM member composition (ADR-0158).

The original smali is never rewritten. The sealed JAR entry selects execution
sources after normal DEX registration. An empty table emits no entry at all.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import sys

ENTRY = 'META-INF/ogplay/member-sources.json'
DEFAULT = Path(__file__).with_name('member-sources.json')
METHOD = re.compile(r'^\.method ([^\n]+)\n(.*?)^\.end method', re.M | re.S)
FIELD = re.compile(r'^\.field ([^\n]+)', re.M)
FLAGS = dict(public=1, private=2, protected=4, static=8, final=16,
             synchronized=32, volatile=64, bridge=64, transient=128, varargs=128,
             native=256, interface=512, abstract=1024, strictfp=2048,
             synthetic=4096, annotation=8192, enum=16384, constructor=65536,
             **{'declared-synchronized': 131072})


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def flags(words):
    result = 0
    for word in words:
        result |= FLAGS[word]
    return result


def inventory(text):
    methods = {}
    for declaration, body in METHOD.findall(text):
        words = declaration.split()
        key = words[-1]
        if key in methods:
            raise ValueError('duplicate ROM member: ' + key)
        methods[key] = dict(signature=key, flags=flags(words[:-1]),
                            body_sha256=digest(body.encode('utf-8')), body=body)
    fields = []
    for declaration in FIELD.findall(text):
        decl, separator, value = declaration.partition(' = ')
        words = decl.split()
        fields.append(dict(signature=words[-1], flags=flags(words[:-1]), source='rom',
                           initialization=('rom-encoded:' + value if separator else 'vm-zero-before-selected-constructor/rom-clinit')))
    return methods, sorted(fields, key=lambda x: x['signature'])


def canonical(document):
    return json.dumps(document, ensure_ascii=True, sort_keys=True, separators=(',', ':')).encode('utf-8') + b'\n'


def validate(document, texts, surface):
    """Validate exact ownership, byte identity, reviewed handlers and references.

    `surface` is a pinned dump-surface; no dependency receives future-implementation
    credit. All selected bodies, including constructors/clinit, are audited.
    """
    if set(document) != {'schema', 'classes'} or document['schema'] != 1:
        raise ValueError('invalid member source schema')
    classes = document['classes']
    if not isinstance(classes, list):
        raise ValueError('classes must be an array')
    runtime = {c['descriptor']: c for c in surface}
    tables = {}
    for cls in classes:
        owner = cls['owner']
        if owner in tables:
            raise ValueError('duplicate class source: ' + owner)
        if cls['shape_owner'] != 'rom' or cls['clinit_owner'] not in ('rom', 'rom-absent'):
            raise ValueError('ROM must own shape/fields/clinit: ' + owner)
        text = texts[owner]
        if digest(text.encode('utf-8')) != cls['smali_sha256']:
            raise ValueError('ROM input hash mismatch: ' + owner)
        methods, fields = inventory(text)
        if fields != cls['fields']:
            raise ValueError('ROM field type/flags/initialization mismatch: ' + owner)
        if ('<clinit>()V' in methods) != (cls['clinit_owner'] == 'rom'):
            raise ValueError('clinit owner mismatch: ' + owner)
        rows = {}
        for row in cls['methods']:
            key = row['signature']
            if key in rows:
                raise ValueError('conflicting member sources: ' + owner + '->' + key)
            rows[key] = row
            original = methods[key]
            if any(row[k] != original[k] for k in ('flags', 'body_sha256')):
                raise ValueError('ROM method identity mismatch: ' + key)
            source = row['source']
            if source not in ('rom', 'overlay', 'failure'):
                raise ValueError('invalid member source: ' + key)
            if key == '<clinit>()V' and source != 'rom':
                raise ValueError('clinit must execute ROM: ' + owner)
            if original['flags'] & 256 and source != 'failure':
                if source != 'overlay' or row.get('platform_seam') is not True:
                    raise ValueError('unauthorized native selection: ' + key)
            if source == 'rom' and original['flags'] & (256 | 1024):
                raise ValueError('ROM source has no bytecode: ' + key)
            if source == 'overlay':
                review = row.get('review', {})
                if not all(isinstance(review.get(k), str) and review[k].strip()
                           for k in ('reason', 'inputs', 'outputs', 'state_owner', 'failure_boundary')):
                    raise ValueError('overlay lacks explicit review: ' + key)
                match = next((m for m in runtime.get(owner, {}).get('methods', [])
                              if m['name'] + m['descriptor'] == key), None)
                if not match or match['status'] != 'implemented' or match['binding'] != 'intrinsic':
                    raise ValueError('overlay handler not implemented: ' + key)
        if set(rows) != set(methods):
            raise ValueError('member table must cover all ROM declarations: ' + owner)
        tables[owner] = rows
        # No legacy intrinsic state or declarations may survive the ROM owner.
        for f in runtime.get(owner, {}).get('fields', []):
            if f['name'] + ':' + f['descriptor'] not in {v['signature'] for v in fields}:
                raise ValueError('non-ROM intrinsic field: ' + owner)
        for m in runtime.get(owner, {}).get('methods', []):
            key = m['name'] + m['descriptor']
            if key not in methods:
                raise ValueError('non-ROM intrinsic method: ' + owner + '->' + key)

    def implemented(owner, key, field=False):
        visited = set()
        while owner and owner not in visited:
            visited.add(owner)
            if owner in tables:
                if field and key in {f['signature'] for f in next(c for c in classes if c['owner'] == owner)['fields']}:
                    return True
                if not field and key in tables[owner]:
                    return tables[owner][key]['source'] != 'failure'
            cls = runtime.get(owner, {})
            for m in cls.get('fields' if field else 'methods', []):
                signature = m['name'] + (':' if field else '') + m['descriptor']
                if signature == key:
                    return m.get('status') == 'implemented'
            owner = cls.get('superclass')
        return False

    # Reuse the existing ADR-0156 hierarchy/dispatch audit. The selector itself
    # receives no whitelist, status relaxation or policy change.
    import select_rom_java as selector
    normalized = copy.deepcopy(runtime)
    parsed = {owner: selector.parse_smali_class(text) for owner, text in texts.items()}
    for cls in classes:
        owner = cls['owner']
        normalized[owner] = dict(descriptor=owner, source='bootdex', access_flags=flags(parsed[owner]['flags']),
                                 superclass=parsed[owner]['super'], interfaces=parsed[owner]['interfaces'],
                                 methods=[dict(name=r['signature'].split('(')[0],
                                               descriptor='(' + r['signature'].split('(', 1)[1],
                                               access_flags=r['flags'],
                                               overridable=not r['flags'] & (2 | 8 | 16),
                                               status='unimplemented-ledger' if r['source'] == 'failure' else 'implemented',
                                               binding='bytecode' if r['source'] == 'rom' else 'intrinsic')
                                          for r in cls['methods']],
                                 fields=[dict(name=f['signature'].split(':')[0], descriptor=f['signature'].split(':')[1],
                                              status='implemented', static=bool(f['flags'] & 8)) for f in cls['fields']])
    for cls in normalized.values():
        cls.setdefault('source', 'intrinsic')
        cls.setdefault('access_flags', 1)
        cls.setdefault('interfaces', [])
        cls.setdefault('fields', [])
        for m in cls['methods']:
            m.setdefault('access_flags', 1)
    analyzer = selector.Analyzer(list(normalized.values()), parsed, {}, set(parsed), set(), set())
    for owner in parsed:
        reasons = analyzer.parent_member_reasons(owner)
        if reasons:
            raise ValueError('ADR-0156: ' + repr(reasons))
        for key, row in tables[owner].items():
            if row['source'] != 'rom':
                continue
            name, descriptor = key.split('(', 1)
            reasons, _, _ = analyzer.summary((owner, name, '(' + descriptor))
            if reasons:
                raise ValueError('selected-ROM-dependency: ' + repr(reasons))

    # Signature-exact references. Check every selected body, not just clinit;
    # default selector/ADR-0156 analysis is deliberately untouched.
    reference = re.compile(r'(L[^;\s]+;)->([^\s,}]+)')
    for cls in classes:
        methods, _ = inventory(texts[cls['owner']])
        for key, row in tables[cls['owner']].items():
            if row['source'] != 'rom':
                continue
            body = re.sub(r'"(?:\\.|[^"\\])*"|#[^\n]*', '', methods[key]['body'])
            for owner, member in reference.findall(body):
                if not implemented(owner, member, '(' not in member):
                    raise ValueError('parent-intrinsic-unimplemented-member/missing-dependency: '
                                     + cls['owner'] + '->' + key + ' -> ' + owner + '->' + member)
    return canonical(document)


def load(path=DEFAULT):
    document = json.loads(path.read_text(encoding='utf-8'))
    if document != {'schema': 1, 'classes': []} and not document.get('classes'):
        raise ValueError('invalid empty member manifest')
    return document


def payload(document, roots, root):
    if not document['classes']:
        return None
    texts = {}
    surfaces = []
    for cls in document['classes']:
        owner = cls['owner']
        matches = [p / (owner[1:-1] + '.smali') for p in roots
                   if (p / (owner[1:-1] + '.smali')).is_file()]
        if len(matches) != 1:
            raise ValueError('ROM class must have exactly one owner: ' + owner)
        texts[owner] = matches[0].read_text(encoding='utf-8')
        source = root / cls['rom_input']
        if digest(source.read_bytes()) != cls['rom_input_sha256']:
            raise ValueError('pinned ROM input hash mismatch: ' + owner)
        surface = root / cls['surface']
        if digest(surface.read_bytes()) != cls['surface_sha256']:
            raise ValueError('pinned runtime surface hash mismatch')
        current = json.loads(surface.read_text(encoding='utf-8'))
        if current.get('startup_link_error'):
            raise ValueError('surface startup link failure')
        surfaces += current['classes']
    return validate(document, texts, surfaces)


def fixture(text):
    """Controlled test ROM; never part of the production recipe."""
    methods, fields = inventory(text)
    owner = 'Ltest/MemberComposition;'
    rows = []
    for key, m in methods.items():
        row = {k: v for k, v in m.items() if k != 'body'}
        row['source'] = 'overlay' if key == 'host()I' else 'failure' if key in ('missing()I', 'unavailable()I') else 'rom'
        if row['source'] == 'overlay':
            row['review'] = dict(reason='controlled handler probe', inputs='none', outputs='int 29',
                                 state_owner='ROM fields only', failure_boundary='no platform calls')
        rows.append(row)
    cls = dict(owner=owner, shape_owner='rom', clinit_owner='rom',
               smali_sha256=digest(text.encode()), fields=fields, methods=rows,
               rom_input='tests/dexvm/fixtures/member_composition.dexasm', rom_input_sha256=digest(text.encode()),
               surface='tests/dexvm/fixtures/member_composition.surface.json',
               surface_sha256=digest((Path(__file__).resolve().parents[2] / 'tests/dexvm/fixtures/member_composition.surface.json').read_bytes()))
    return dict(schema=1, classes=[cls]), [dict(descriptor=owner, superclass=None, fields=[],
                                              methods=[dict(name='host', descriptor='()I',
                                                            status='implemented', binding='intrinsic')])]


def self_test():
    text = (Path(__file__).resolve().parents[2] / 'tests/dexvm/fixtures/member_composition.dexasm').read_text(encoding='utf-8')
    document, surface = fixture(text)
    texts = {'Ltest/MemberComposition;': text}
    result = validate(document, texts, surface)
    assert result == validate(copy.deepcopy(document), texts, surface)
    for mutation in ('duplicate', 'hash', 'field', 'clinit', 'native', 'review', 'dependency'):
        bad = copy.deepcopy(document)
        rows = bad['classes'][0]['methods']
        if mutation == 'duplicate': rows.append(rows[0])
        elif mutation == 'hash': rows[0]['body_sha256'] = '0' * 64
        elif mutation == 'field': bad['classes'][0]['fields'][0]['flags'] ^= 8
        elif mutation == 'clinit': bad['classes'][0]['clinit_owner'] = 'overlay'
        elif mutation == 'review': next(r for r in rows if r['source'] == 'overlay').pop('review')
        elif mutation == 'native':
            native_text = text.replace('.method public static host()', '.method public static native host()')
            native, _ = fixture(native_text)
            bad = native
            try:
                validate(bad, {'Ltest/MemberComposition;': native_text}, surface)
            except ValueError:
                continue
            raise AssertionError('native accepted')
        else:
            next(r for r in rows if r['signature'] == 'rom()I')['source'] = 'failure'
        try:
            validate(bad, texts, surface)
        except ValueError:
            pass
        else:
            raise AssertionError('accepted ' + mutation)
    import tempfile
    with tempfile.TemporaryDirectory(prefix='ogplay-member-source-test-') as work:
        smali = Path(work) / 'test/MemberComposition.smali'
        smali.parent.mkdir(parents=True)
        smali.write_text(text, encoding='utf-8')
        root = Path(__file__).resolve().parents[2]
        assert payload(document, [Path(work)], root) == result
        # Exercise real nonzero command exits as well as exception-level tests.
        import subprocess
        for mutation, expected in (('conflict', 'conflicting member sources'),
                                   ('dependency', 'selected-ROM-dependency'),
                                   ('overlay', 'overlay lacks explicit review'),
                                   ('native', 'unauthorized native selection')):
            bad = copy.deepcopy(document)
            rows = bad['classes'][0]['methods']
            if mutation == 'conflict':
                rows.append(rows[0])
            elif mutation == 'dependency':
                next(r for r in rows if r['signature'] == 'rom()I')['source'] = 'failure'
            elif mutation == 'overlay':
                next(r for r in rows if r['source'] == 'overlay').pop('review')
            else:
                next(r for r in rows if r['signature'] == 'unavailable()I')['source'] = 'rom'
            path = Path(work) / 'invalid.json'
            path.write_text(json.dumps(bad), encoding='utf-8')
            run = subprocess.run([sys.executable, str(Path(__file__).resolve()), '--check',
                                  '--manifest', str(path), '--smali-root', work],
                                 capture_output=True, text=True)
            assert run.returncode == 2 and expected in run.stderr, run.stderr
        for key in ('rom_input_sha256', 'surface_sha256'):
            bad = copy.deepcopy(document)
            bad['classes'][0][key] = '0' * 64
            try:
                payload(bad, [Path(work)], root)
            except ValueError:
                pass
            else:
                raise AssertionError('accepted changed input ' + key)
    assert payload({'schema': 1, 'classes': []}, [], Path('.')) is None
    print('member_sources self-test passed (identity, ownership, conflicts, dependencies, native, overlay, deterministic, disabled)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--manifest', type=Path, default=DEFAULT)
    parser.add_argument('--smali-root', action='append', type=Path, default=[])
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.check:
        data = payload(load(args.manifest), args.smali_root, Path(__file__).resolve().parents[2])
        if data is not None:
            if args.report:
                args.report.write_bytes(data)
            print('member sources: validated SHA256 ' + digest(data))
        else:
            print('member sources: disabled (no artifact entry)')
    if args.self_test:
        self_test()
    if args.fixture:
        text = (Path(__file__).resolve().parents[2] / 'tests/dexvm/fixtures/member_composition.dexasm').read_text(encoding='utf-8')
        document, surface = fixture(text)
        args.fixture.write_bytes(validate(document, {'Ltest/MemberComposition;': text}, surface))
        # A separate test BootDex combines the controlled ROM with the real
        # exception/library closure. Production data is only read.
        import tempfile
        import build_bootdex as builder
        with tempfile.TemporaryDirectory(prefix='ogplay-member-fixture-') as work:
            root = Path(work) / 'smali'
            builder.run([str(builder.JAVA), '-jar', str(builder.SMALI / 'baksmali.jar'),
                         'disassemble', '--api', '19', '--jobs', '1', '--output', str(root), str(builder.OUTPUT)])
            sample = root / 'test/MemberComposition.smali'
            sample.parent.mkdir(parents=True, exist_ok=True)
            sample.write_text(text, encoding='utf-8')
            builder.run([str(builder.JAVA), '-jar', str(builder.SMALI / 'smali.jar'),
                         'assemble', '--api', '19', '--jobs', '1', '--output',
                         str(args.fixture.with_suffix('.dex')), str(root)])
            args.fixture.with_suffix('.jar').write_bytes(builder.make_jar(
                args.fixture.with_suffix('.dex').read_bytes(), {ENTRY: args.fixture.read_bytes()}))
    if not args.fixture and not args.self_test and not args.check:
        parser.error('--check, --self-test or --fixture required')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError) as error:
        print('member_sources: ' + str(error), file=sys.stderr)
        raise SystemExit(2)
