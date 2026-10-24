#!/usr/bin/env python3
"""Build/check opt-in ROM member composition without changing the pinned JNI generator.

The original build_bootdex.py is itself native-payload provenance. Keep its bytes
stable; reuse its assembly, audits, canonical ZIP and metadata implementations.
"""
from __future__ import annotations

import argparse
import copy
import io
import json
from pathlib import Path
import tempfile
import zipfile

import build_bootdex as builder
import member_sources
import theme_projection


def measure_sources(document):
    """Fixed test-only counterpart; never selected by runtime/user settings."""
    policy = json.loads(Path(__file__).with_name('view-measure-sources.json').read_text(encoding='utf-8'))
    document = copy.deepcopy(document)
    cls, = document['classes']
    if set(policy) != {'rom_methods', 'rom_fields', 'overlays', 'surface', 'surface_sha256'}:
        raise builder.BuildError('invalid test measure policy')
    method_keys = {r['signature'] for r in cls['methods']}
    field_keys = {f['signature'] for f in cls['fields']}
    if (len(set(policy['rom_methods'])) != len(policy['rom_methods'])
            or len(set(policy['rom_fields'])) != len(policy['rom_fields'])
            or set(policy['rom_methods']) & set(policy['overlays'])
            or (set(policy['rom_methods']) | set(policy['overlays'])) - method_keys
            or set(policy['rom_fields']) - field_keys):
        raise builder.BuildError('conflicting or unknown test measure member')
    for row in cls['methods']:
        key = row['signature']
        if key in policy['rom_methods']:
            row['source'] = 'rom'
            row.pop('review', None)
        elif key in policy['overlays']:
            row['source'] = 'overlay'
            row['review'] = policy['overlays'][key]
        else:
            row['source'] = 'failure'
            row.pop('review', None)
    for field in cls['fields']:
        if field['signature'] in policy['rom_fields']:
            field['source'] = 'rom'
    cls['surface'], cls['surface_sha256'] = policy['surface'], policy['surface_sha256']
    document['classes'].append(json.loads(Path(__file__).with_name('view-group-sources.json').read_text(encoding='utf-8')))
    document['classes'].append(json.loads(Path(__file__).with_name('view-system-sources.json').read_text(encoding='utf-8')))
    document['classes'].extend(json.loads(Path(__file__).with_name('view-frame-sources.json').read_text(encoding='utf-8'))['classes'])
    document['classes'].extend(json.loads(Path(__file__).with_name('view-linear-sources.json').read_text(encoding='utf-8'))['classes'])
    document['classes'].extend(json.loads(Path(__file__).with_name('view-relative-sources.json').read_text(encoding='utf-8'))['classes'])
    document['classes'].extend(json.loads(Path(__file__).with_name('view-xml-sources.json').read_text(encoding='utf-8'))['classes'])
    document['classes'].extend(json.loads(Path(__file__).with_name('view-theme-sources.json').read_text(encoding='utf-8'))['classes'])
    document['classes'].extend(json.loads(Path(__file__).with_name('view-context-sources.json').read_text(encoding='utf-8'))['classes'])
    document['classes'].extend(json.loads(Path(__file__).with_name('view-inflater-sources.json').read_text(encoding='utf-8'))['classes'])
    document['classes'].extend(json.loads(Path(__file__).with_name('view-leaf-resources-sources.json').read_text(encoding='utf-8'))['classes'])
    return document


def assemble(recipe, work, measure=False):
    if measure:
        # Replace the Legacy bounded wrapper only in this fixed ROM counterpart.
        for source, classes in recipe.items():
            for offset in range(0, len(classes), 100):
                builder.run([str(builder.JAVA), '-jar', str(builder.SMALI / 'baksmali.jar'),
                             'disassemble', '--api', '19', '--jobs', '1', '--classes',
                             ','.join(classes[offset:offset + 100]), '--output',
                             str(work / (source + '-smali')), str(builder.source_path(source))])
        guest, custom = builder.compile_guest_java(work)
        replacements = {'Landroid/view/ContextThemeWrapper;', 'Landroid/view/LayoutInflater;'}
        duplicates = {v for values in recipe.values() for v in values} & set(custom)
        if duplicates != replacements:
            raise builder.BuildError('unexpected paired guest Java replacement')
        for replacement in replacements | {'Landroid/view/LayoutInflater$LocalInflater;'}:
            (guest / (replacement[1:-1] + '.smali')).unlink()
        extra = work / 'experimental-classes'
        extra.mkdir()
        builder.run([str(builder.JAVAC), '-encoding', 'UTF-8', '-source', '7', '-target', '7',
                     '-Xlint:-options', '-g:none', '-bootclasspath', str(builder.ANDROID_JAR),
                     '-d', str(extra), str(Path(__file__).with_name('experimental-java') / 'android/view/LocalLayoutInflater.java')])
        dex_dir = work / 'experimental-dex'
        dex_dir.mkdir()
        builder.run([str(builder.JAVA), '-cp', str(builder.D8_JAR), 'com.android.tools.r8.D8',
                     '--min-api', '19', '--output', str(dex_dir), *map(str, sorted(extra.rglob('*.class')))])
        builder.run([str(builder.JAVA), '-jar', str(builder.SMALI / 'baksmali.jar'), 'disassemble',
                     '--api', '19', '--jobs', '1', '--output', str(guest), str(dex_dir / 'classes.dex')])
        output = work / 'classes.dex'
        roots = [str(work / (source + '-smali')) for source in recipe] + [str(guest)]
        builder.run([str(builder.JAVA), '-jar', str(builder.SMALI / 'smali.jar'), 'assemble',
                     '--api', '19', '--jobs', '1', '--output', str(output), *roots])
        dex = output.read_bytes()
    else:
        dex = builder.assemble(recipe, work)
    document = member_sources.load()
    if measure:
        document = measure_sources(document)
    for cls in document['classes']:
        sources = [s for s, owners in recipe.items() if cls['owner'] in owners]
        if len(sources) != 1 or (builder.ROOT / cls['rom_input']).resolve() != builder.source_path(sources[0]).resolve():
            raise builder.BuildError('composition class must come from its pinned ROM recipe input')
    roots = [work / (s + '-smali') for s in recipe] + [work / 'guest-java-smali']
    payload = member_sources.payload(document, roots, builder.ROOT,
                                     writer_owners={owner for owners in recipe.values() for owner in owners})
    return dex, payload


def build(measure=False):
    recipe = builder.load_recipe()
    if measure:
        group = json.loads(Path(__file__).with_name('view-group-sources.json').read_text(encoding='utf-8'))
        # ADR-0155 still audits every retained DEX body, including disabled
        # composition bodies. These pure-Java helpers are admitted by the
        # unchanged default selector; keep them only in this test counterpart.
        hierarchy = {group['owner'], 'Landroid/widget/FrameLayout;', 'Landroid/widget/LinearLayout;',
                     *(c['owner'] for c in measure_sources(member_sources.load())['classes']
                       if c['owner'].startswith(('Landroid/view/LayoutInflater', 'Landroid/widget/RelativeLayout', 'Landroid/content/res/XmlBlock', 'Landroid/content/res/StringBlock'))),
                     'Landroid/util/AttributeSet;', 'Landroid/content/Context;', 'Landroid/view/ContextThemeWrapper;',
                     'Landroid/content/res/AssetManager;', 'Landroid/content/res/Resources;',
                     'Landroid/content/res/Resources$Theme;', 'Landroid/content/res/TypedArray;',
                     'Landroid/content/res/AssetManager$AssetInputStream;',
                     'Landroid/app/IconPackHelper;', 'Landroid/app/IconPackHelper$ColorFilterUtils;',
                     'Landroid/app/IconPackHelper$ColorFilterUtils$Builder;',
                     'Landroid/graphics/drawable/Drawable;', 'Landroid/graphics/drawable/BitmapDrawable;',
                     'Landroid/graphics/drawable/ColorDrawable;',
                     'Landroid/view/View$ListenerInfo;',
                     'Landroid/view/ViewGroup$1;', 'Landroid/view/ViewGroup$2;',
                     'Landroid/view/ViewGroup$3;', 'Landroid/view/ViewGroup$ChildListForAccessibility;',
                     'Landroid/view/ViewGroup$ViewLocationHolder;',
                     'Landroid/content/res/StringBlock$Height;', 'Landroid/text/Annotation;',
                     'Landroid/text/style/BulletSpan;'}
        recipe['framework.jar'] = tuple(sorted(set(recipe['framework.jar']) | hierarchy))
    builder.verify_inputs(recipe)
    with tempfile.TemporaryDirectory(prefix='ogplay-composition-a-') as first, \
            tempfile.TemporaryDirectory(prefix='ogplay-composition-b-') as second:
        dex, payload = assemble(recipe, Path(first), measure)
        if (dex, payload) != assemble(recipe, Path(second), measure):
            raise builder.BuildError('two BootDex DEX/member source assemblies differ')
    with tempfile.TemporaryDirectory(prefix='ogplay-composition-java-audit-') as work:
        if measure:
            _, custom = builder.compile_guest_java(Path(work))
            selected = builder.expected_class_names(recipe, tuple(c for c in custom if c not in {'Landroid/view/ContextThemeWrapper;', 'Landroid/view/LayoutInflater;', 'Landroid/view/LayoutInflater$Factory;', 'Landroid/view/LayoutInflater$LocalInflater;'}) + ('Landroid/view/LocalLayoutInflater;',))
        else:
            selected = builder.compile_expected_class_names(recipe, Path(work))
    if builder.class_names(dex) != selected:
        raise builder.BuildError('generated classes differ from recipe')
    resources = builder.resource_payload(builder.load_resources())
    if member_sources.ENTRY in resources or builder.SYSTEM_RESOURCES_ENTRY in resources:
        raise builder.BuildError('reserved BootDex resource entry in input')
    if payload is not None:
        resources[member_sources.ENTRY] = payload
    resources[builder.SYSTEM_RESOURCES_ENTRY] = builder.system_resources_payload(dex)
    if measure:
        projected = theme_projection.generate()
        theme_projection.check_conflicts(projected, resources[builder.SYSTEM_RESOURCES_ENTRY])
        resources[theme_projection.ENTRY] = projected
    return builder.make_jar(dex, resources), dex, recipe


def boot_metadata(jar, dex, recipe):
    metadata = builder.boot_metadata(jar, dex, recipe)
    with zipfile.ZipFile(io.BytesIO(jar)) as archive:
        if member_sources.ENTRY in archive.namelist():
            metadata['member_sources'] = dict(
                entry=member_sources.ENTRY,
                manifest_sha256=builder.sha256(archive.read(member_sources.ENTRY)),
                composer='tools/bootdex/compose_bootdex.py',
                composer_sha256=builder.file_sha256(Path(__file__)))
        if theme_projection.ENTRY in archive.namelist():
            metadata['theme_projection'] = dict(entry=theme_projection.ENTRY, schema_version=1,
                sha256=builder.sha256(archive.read(theme_projection.ENTRY)))
    # Publish experimental provenance independently of the production JAR bytes.
    if 'theme_projection' not in metadata:
        metadata['experimental_theme_projection'] = dict(
            scope='JavaTraversal paired payload only', entry=theme_projection.ENTRY,
            schema_version=1, sha256=builder.sha256(theme_projection.generate()),
            generator='tools/bootdex/theme_projection.py',
            inputs_sha256=builder.file_sha256(theme_projection.POLICY))
    if 'theme_projection' in metadata:
        companion = Path(__file__).with_name('experimental-java') / 'android/view/LocalLayoutInflater.java'
        metadata['experimental_inflater'] = dict(companion=str(companion.relative_to(builder.ROOT)),
            sha256=builder.file_sha256(companion), protocol='AOSP 4.4 PhoneLayoutInflater; absent from pinned ROM')
    return metadata


def self_test():
    member_sources.self_test()
    return builder.self_test()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', nargs='?', choices=('build', 'check'))
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--view-measure-test-output', type=Path,
                        help='write/check the fixed experimental test counterpart instead of production')
    args = parser.parse_args()
    try:
        if args.self_test:
            return self_test()
        if not args.mode:
            parser.error('build, check or --self-test required')
        jar, dex, recipe = build(args.view_measure_test_output is not None)
        builder.audit_crc32(dex)
        if args.view_measure_test_output:
            # The fixed ROM inflater has no Legacy nativeInflate declaration.
            expected = builder.zip_stream_native_signatures()
            expected['Landroid/view/LayoutInflater;'] = {}
            parsed = builder.dex_survey_lib.parse_dex(dex)
            observed = {owner: {} for owner in expected}
            for cls in parsed.classes:
                owner = parsed.type_name(cls.type_index)
                if owner in observed:
                    for method in cls.direct_methods + cls.virtual_methods:
                        if method.access_flags & builder.dex_survey_lib.ACC_NATIVE:
                            _, name, descriptor = parsed.method_signature(method.method_index)
                            observed[owner][name + descriptor] = method.access_flags
            if observed != expected:
                raise builder.BuildError('experimental native ABI drift: ' + str(observed))
        else:
            builder.audit_zip_streams(dex)
        builder.audit_native_crypto(dex)
        manifest = builder.manifest_bytes(boot_metadata(jar, dex, recipe))
        output = args.view_measure_test_output or builder.OUTPUT
        metadata = output.with_suffix('.manifest.json') if args.view_measure_test_output else builder.MANIFEST
        if args.mode == 'build':
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(jar)
            metadata.write_bytes(manifest)
        elif output.read_bytes() != jar or metadata.read_bytes() != manifest:
            raise builder.BuildError('BootDex payload is stale; run compose_bootdex.py build')
        print(f'BootDex composition {args.mode}: {len(builder.class_names(dex))} classes, '
              f'DEX {builder.sha256(dex)}, JAR {builder.sha256(jar)}')
        return 0
    except (builder.BuildError, OSError, ValueError, KeyError, TypeError,
            builder.dex_survey_lib.DexFormatError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    raise SystemExit(main())
