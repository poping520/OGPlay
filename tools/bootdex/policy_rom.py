#!/usr/bin/env python3
"""Independent, hash-locked policy ROM reference; never extends the API19 skeleton.

Only the reviewed inflater is selected. The complete source class inventory is
checked for collisions before selection, including nonselected classes.
"""
from __future__ import annotations

import json
from pathlib import Path
import zipfile

import build_bootdex as builder

SOURCE = 'android.policy.jar'
SHA256 = '660fa192659bf02285cfeb559d3fad547a72d4986aa295ef5a9453b2b2372ad1'
OWNER = 'Lcom/android/internal/policy/impl/PhoneLayoutInflater;'
REFERENCE = Path(__file__).with_name('policy-reference.json')
BASELINE = Path(__file__).with_name('policy-baseline.json')
JARS = ('core.jar', 'framework.jar', 'framework2.jar', 'ext.jar', SOURCE)


def records(image):
    out = {}
    for cls in image.classes:
        owner = image.type_name(cls.type_index)
        methods = sorted([*image.method_signature(m.method_index)[1:], m.access_flags]
                         for m in cls.direct_methods + cls.virtual_methods)
        fields = []
        for f in cls.static_fields + cls.instance_fields:
            _, typ, name = image.field_ids[f.field_index]
            fields.append([image.strings[name], image.type_name(typ), f.access_flags])
        out[owner] = dict(superclass=image.type_name(cls.superclass_index) if cls.superclass_index is not None else None,
                          interfaces=[image.type_name(i) for i in cls.interface_type_indices],
                          access_flags=cls.access_flags, methods=methods, fields=sorted(fields))
    return out


def verify_sources():
    path = builder.source_path(SOURCE)
    if builder.file_sha256(path) != SHA256:
        raise builder.BuildError('pinned policy ROM input hash mismatch')
    from api19_skeleton import PINNED
    for source, digest in PINNED.items():
        if builder.file_sha256(builder.source_path(source)) != digest:
            raise builder.BuildError('pinned framework ROM input hash mismatch: ' + source)
    owners = {}
    policy = None
    for source in JARS:
        with zipfile.ZipFile(builder.source_path(source)) as archive:
            image = builder.dex_survey_lib.parse_dex(archive.read('classes.dex'))
        for cls in image.classes:
            owner = image.type_name(cls.type_index)
            if owner in owners:
                raise builder.BuildError('conflicting ROM class sources: ' + owner)
            owners[owner] = source
        if source == SOURCE:
            policy = records(image)
    reference = json.loads(REFERENCE.read_text(encoding='utf-8'))
    if reference != dict(schema=1, source=SOURCE, sha256=SHA256, classes={OWNER: policy[OWNER]}):
        raise builder.BuildError('policy ROM reference shape drift')
    baseline = json.loads(BASELINE.read_text(encoding='utf-8'))
    if baseline != dict(schema=1, reference_sha256=builder.file_sha256(REFERENCE),
                        selected=[OWNER], shape_deviations=[]):
        raise builder.BuildError('policy baseline selection/identity drift')
    return reference['classes']


def extend_surface(surface):
    """Expose missing policy candidates using their independent ROM reference.

    Existing runtime ownership/status always wins; no implemented status is
    invented for a missing class. Admission still runs the normal selector.
    """
    reference = verify_sources()
    owners = {c['descriptor'] for c in surface}
    extra = []
    for owner, shape in reference.items():
        if owner in owners:
            continue
        extra.append(dict(descriptor=owner, source='skeleton', link_error='',
            superclass=shape['superclass'], interfaces=shape['interfaces'],
            access_flags=shape['access_flags'],
            methods=[dict(name=n, descriptor=d, access_flags=f,
                status='unimplemented-ledger', binding='intrinsic') for n, d, f in shape['methods']],
            fields=[dict(name=n, descriptor=d, access_flags=f, has_constant=False,
                status='unimplemented-ledger') for n, d, f in shape['fields']]))
    return [*surface, *extra]


def check_selection(recipe, dex=None):
    reference = verify_sources()
    if tuple(recipe.get(SOURCE, ())) != (OWNER,):
        raise builder.BuildError('policy recipe must select only the reviewed inflater')
    if any(OWNER in values for source, values in recipe.items() if source != SOURCE):
        raise builder.BuildError('policy recipe source collision')
    if dex is not None:
        selected = records(builder.dex_survey_lib.parse_dex(dex))
        if any(selected.get(owner) != shape for owner, shape in reference.items()):
            raise builder.BuildError('selected policy shape differs from ROM baseline')
        if any(owner.startswith('Lcom/android/internal/policy/') and owner not in reference for owner in selected):
            raise builder.BuildError('unreviewed policy class selected')


def self_test():
    from unittest.mock import patch
    import tempfile
    verify_sources()
    proposed, = extend_surface([])
    assert proposed['descriptor'] == OWNER
    assert all(m['status'] == 'unimplemented-ledger' for m in proposed['methods'])
    existing = dict(descriptor=OWNER, source='bootdex', methods=[])
    assert extend_surface([existing]) == [existing]

    def rejects(operation):
        try:
            operation()
        except builder.BuildError:
            return
        raise AssertionError('invalid policy evidence accepted')

    with patch(__name__ + '.SHA256', '0' * 64):
        rejects(verify_sources)
    with patch(__name__ + '.JARS', ('core.jar', 'core.jar')):
        rejects(verify_sources)
    with tempfile.TemporaryDirectory() as directory:
        invalid = Path(directory) / 'reference.json'
        original = json.loads(REFERENCE.read_text(encoding='utf-8'))
        original['classes'][OWNER]['methods'][0][2] ^= 8
        invalid.write_text(json.dumps(original), encoding='utf-8')
        with patch(__name__ + '.REFERENCE', invalid):
            rejects(verify_sources)
        invalid.write_text('{}', encoding='utf-8')
        with patch(__name__ + '.BASELINE', invalid):
            rejects(verify_sources)
    rejects(lambda: check_selection({SOURCE: (OWNER, 'Lcom/android/internal/policy/impl/Policy;')}))
    rejects(lambda: check_selection({SOURCE: (OWNER,), 'framework.jar': (OWNER,)}))
    print('policy ROM self-test passed (hash, source collision, flags, baseline, selection)')


if __name__ == '__main__':
    import sys
    if sys.argv[1:] == ['--self-test']:
        self_test()
    elif sys.argv[1:]:
        raise SystemExit('usage: policy_rom.py [--self-test]')
    else:
        verify_sources()
        print('policy ROM input/reference/baseline verified')
