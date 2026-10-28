#!/usr/bin/env python3
"""Build/check the sealed JavaTraversal ROM member composition without changing the pinned JNI generator.

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
from unittest.mock import patch

import build_bootdex as builder
import member_sources
import theme_projection
import policy_rom

# Extend bounded clients here: the JNI payload generator remains hash-pinned.
builder.FRAMEWORK_JAVA_NAMES = tuple(sorted((*builder.FRAMEWORK_JAVA_NAMES,
    'android/content/ContentResolver.java')))


_original_compile_guest_java = builder.compile_guest_java


def compile_guest_java(work):
    # The facade source hides the SDK nested type from javac. This shape is
    # compile-only: remove it before D8; production keeps the selected ROM class.
    manager_source = builder.FRAMEWORK_JAVA_ROOT / 'android/app/ActivityManager.java'
    manager_compile = work / 'activity-manager-compile-only/android/app/ActivityManager.java'
    manager_compile.parent.mkdir(parents=True, exist_ok=True)
    text = manager_source.read_text(encoding='utf-8')
    manager_compile.write_text(text[:text.rfind('}')] + """
    public static class RunningAppProcessInfo {
        public static final int REASON_UNKNOWN = 0;
        public int uid, flags, importance, lru, lastTrimLevel;
        public int importanceReasonCode, importanceReasonPid;
        public android.content.ComponentName importanceReasonComponent;
        public RunningAppProcessInfo(String name, int pid, String[] packages) {
            throw new AssertionError();
        }
    }
}
""", encoding='utf-8')
    sources = tuple(manager_compile if source == manager_source else source
                    for source in builder.guest_java_sources())
    original_run = builder.run

    def run(command):
        result = original_run(command)
        if command[0] == str(builder.JAVAC):
            (work / 'guest-java-classes/android/app/ActivityManager$RunningAppProcessInfo.class').unlink()
        return result

    with patch.object(builder, 'guest_java_sources', return_value=sources), patch.object(builder, 'run', run):
        return _original_compile_guest_java(work)


builder.compile_guest_java = compile_guest_java


CANDIDATE_RECIPE = Path(__file__).with_name('java-traversal.json')
VIDEO_ENTRY = 'META-INF/ogplay/view-video-adapter.json'
RECIPE_ENTRY = 'META-INF/ogplay/java-traversal-recipe.json'


def candidate_policy():
    policy = json.loads(CANDIDATE_RECIPE.read_text(encoding='utf-8'))
    if (set(policy) != {'schema', 'mode', 'version', 'base_recipe', 'view_policy',
                        'composition_sources', 'class_additions', 'theme_policy', 'video_review'}
            or (policy['schema'], policy['mode'], policy['version']) != (1, 'java-traversal', 1)
            or policy['base_recipe'] != builder.RECIPE.name
            or policy['theme_policy'] != theme_projection.POLICY.name
            or policy['video_review'] != 'view-video-adapter.json'):
        raise builder.BuildError('invalid JavaTraversal candidate recipe')
    paths = [policy['view_policy'], *policy['composition_sources']]
    if len(set(paths)) != len(paths) or any(Path(p).name != p for p in paths):
        raise builder.BuildError('duplicate or invalid candidate composition input')
    return policy


def candidate_classes():
    policy = candidate_policy()
    document = json.loads(builder.RECIPE.read_text(encoding='utf-8'))
    additions = copy.deepcopy(policy['class_additions'])
    policy_classes = additions.pop(policy_rom.SOURCE, [])
    for source, owners in additions.items():
        if owners != sorted(set(owners)) or set(owners) & set(document['classes'].get(source, [])):
            raise builder.BuildError('duplicate candidate class addition')
        document['classes'][source] = sorted(document['classes'].get(source, []) + owners)
    recipe = builder.load_recipe(document)
    recipe[policy_rom.SOURCE] = tuple(policy_classes)
    policy_rom.check_selection(recipe)
    return recipe


def candidate_provenance():
    policy = candidate_policy()
    paths = [builder.RECIPE, member_sources.DEFAULT, CANDIDATE_RECIPE,
             theme_projection.POLICY, Path(theme_projection.__file__),
             Path(__file__).with_name('theme_inputs.py'),
             Path(__file__).with_name('framework_res_audit.py'),
             Path(__file__).with_name(policy['view_policy']),
             Path(__file__).with_name(policy['video_review']),
             *(Path(__file__).with_name(p) for p in policy['composition_sources'])]
    return member_sources.canonical(dict(recipe=policy, input_sha256={
        str(p.relative_to(builder.ROOT)): builder.file_sha256(p) for p in paths}))


def measure_sources(document):
    """Fixed candidate composition, shared by the experimental fixture."""
    policy = json.loads(Path(__file__).with_name(candidate_policy()['view_policy']).read_text(encoding='utf-8'))
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
    for name in candidate_policy()['composition_sources']:
        rows = json.loads(Path(__file__).with_name(name).read_text(encoding='utf-8'))
        document['classes'].extend(rows['classes'] if 'classes' in rows else [rows])
    return document


def assemble(recipe, work):
    # Replace the frozen generator wrappers with the paired ROM classes.
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
    output = work / 'classes.dex'
    roots = [str(work / (source + '-smali')) for source in recipe] + [str(guest)]
    builder.run([str(builder.JAVA), '-jar', str(builder.SMALI / 'smali.jar'), 'assemble',
                 '--api', '19', '--jobs', '1', '--output', str(output), *roots])
    dex = output.read_bytes()
    document = measure_sources(member_sources.load())
    for cls in document['classes']:
        sources = [s for s, owners in recipe.items() if cls['owner'] in owners]
        if len(sources) != 1 or (builder.ROOT / cls['rom_input']).resolve() != builder.source_path(sources[0]).resolve():
            raise builder.BuildError('composition class must come from its pinned ROM recipe input')
    roots = [work / (s + '-smali') for s in recipe] + [work / 'guest-java-smali']
    payload = member_sources.payload(document, roots, builder.ROOT,
                                     writer_owners={owner for owners in recipe.values() for owner in owners})
    return dex, payload


def build():
    recipe = candidate_classes()
    builder.verify_inputs(recipe)
    with tempfile.TemporaryDirectory(prefix='ogplay-composition-a-') as first, \
            tempfile.TemporaryDirectory(prefix='ogplay-composition-b-') as second:
        dex, payload = assemble(recipe, Path(first))
        if (dex, payload) != assemble(recipe, Path(second)):
            raise builder.BuildError('two BootDex DEX/member source assemblies differ')
    with tempfile.TemporaryDirectory(prefix='ogplay-composition-java-audit-') as work:
        _, custom = builder.compile_guest_java(Path(work))
        selected = builder.expected_class_names(recipe, tuple(c for c in custom if c not in {'Landroid/view/ContextThemeWrapper;', 'Landroid/view/LayoutInflater;', 'Landroid/view/LayoutInflater$Factory;', 'Landroid/view/LayoutInflater$LocalInflater;'}))
    policy_rom.check_selection(recipe, dex)
    if builder.class_names(dex) != selected:
        raise builder.BuildError('generated classes differ from recipe')
    resources = builder.resource_payload(builder.load_resources())
    if member_sources.ENTRY in resources or builder.SYSTEM_RESOURCES_ENTRY in resources:
        raise builder.BuildError('reserved BootDex resource entry in input')
    if payload is not None:
        resources[member_sources.ENTRY] = payload
    resources[builder.SYSTEM_RESOURCES_ENTRY] = builder.system_resources_payload(dex)
    projected = theme_projection.generate()
    theme_projection.check_conflicts(projected, resources[builder.SYSTEM_RESOURCES_ENTRY])
    resources[theme_projection.ENTRY] = projected
    review = json.loads(Path(__file__).with_name('view-video-adapter.json').read_text(encoding='utf-8'))
    if (review['implementation_source'] != 'adapter' or review['rom_algorithm_credit'] is not False
            or review['owner'] != 'Landroid/widget/VideoView;' or review['signature'] != 'onMeasure(II)V'
            or any(not review[k] for k in ('reason', 'inputs', 'outputs', 'state_owner', 'failure_boundary'))):
        raise builder.BuildError('invalid bounded VideoView adapter review')
    resources[VIDEO_ENTRY] = member_sources.canonical(review)
    resources[RECIPE_ENTRY] = candidate_provenance()
    resources['META-INF/ogplay/vm-pairing.json'] = member_sources.canonical(dict(
        schema=1, mode='java-traversal', version=1,
        dex_sha256=builder.sha256(dex),
        member_sources_sha256=builder.sha256(payload),
        theme_projection_sha256=builder.sha256(projected),
        video_adapter_sha256=builder.sha256(resources[VIDEO_ENTRY]),
        candidate_recipe_sha256=builder.sha256(resources[RECIPE_ENTRY])))
    return builder.make_jar(dex, resources), dex, recipe


def boot_metadata(jar, dex, recipe):
    metadata = builder.boot_metadata(jar, dex,
        {source: owners for source, owners in recipe.items() if source != policy_rom.SOURCE})
    if policy_rom.SOURCE in recipe:
        metadata['sources'].insert(-1, dict(source_project='platform/frameworks/base',
            source_jar=policy_rom.SOURCE, source_jar_sha256=policy_rom.SHA256))
    with zipfile.ZipFile(io.BytesIO(jar)) as archive:
        if RECIPE_ENTRY in archive.namelist():
            metadata['candidate_recipe'] = dict(path=str(CANDIDATE_RECIPE.relative_to(builder.ROOT)),
                sha256=builder.file_sha256(CANDIDATE_RECIPE), entry=RECIPE_ENTRY,
                payload_sha256=builder.sha256(archive.read(RECIPE_ENTRY)))
        if 'META-INF/ogplay/vm-pairing.json' in archive.namelist():
            metadata['vm_pairing'] = dict(entry='META-INF/ogplay/vm-pairing.json',
                sha256=builder.sha256(archive.read('META-INF/ogplay/vm-pairing.json')),
                mode='java-traversal', version=1)
        if member_sources.ENTRY in archive.namelist():
            metadata['member_sources'] = dict(
                entry=member_sources.ENTRY,
                manifest_sha256=builder.sha256(archive.read(member_sources.ENTRY)),
                composer='tools/bootdex/compose_bootdex.py',
                composer_sha256=builder.file_sha256(Path(__file__)))
        if theme_projection.ENTRY in archive.namelist():
            metadata['theme_projection'] = dict(entry=theme_projection.ENTRY, schema_version=1,
                sha256=builder.sha256(archive.read(theme_projection.ENTRY)))
    if 'theme_projection' in metadata:
        review = Path(__file__).with_name('view-video-adapter.json')
        metadata['experimental_video_adapter'] = dict(review=str(review.relative_to(builder.ROOT)), sha256=builder.file_sha256(review), entry='META-INF/ogplay/view-video-adapter.json', rom_algorithm_credit=False)
        metadata['experimental_inflater'] = dict(source=policy_rom.SOURCE,
            sha256=policy_rom.SHA256, owner=policy_rom.OWNER,
            reference_sha256=builder.file_sha256(policy_rom.REFERENCE),
            baseline_sha256=builder.file_sha256(policy_rom.BASELINE))
    return metadata


def self_test():
    policy = candidate_policy()
    recipe = candidate_classes()
    assert recipe[policy_rom.SOURCE] == (policy_rom.OWNER,)
    assert len({c['owner'] for c in measure_sources(member_sources.load())['classes']}) > 1
    assert candidate_provenance() == candidate_provenance()
    for change in ('duplicate_class', 'duplicate_source', 'unknown_mode'):
        invalid = copy.deepcopy(policy)
        if change == 'duplicate_class':
            invalid['class_additions']['framework.jar'] *= 2
        elif change == 'duplicate_source':
            invalid['class_additions']['core.jar'] = [policy_rom.OWNER]
        else:
            invalid['mode'] = 'unknown'
        with tempfile.TemporaryDirectory(prefix='ogplay-candidate-policy-') as work:
            path = Path(work) / 'recipe.json'
            path.write_text(json.dumps(invalid), encoding='utf-8')
            with patch(__name__ + '.CANDIDATE_RECIPE', path):
                try:
                    candidate_classes()
                except builder.BuildError:
                    pass
                else:
                    raise AssertionError('invalid candidate recipe accepted: ' + change)
    print('JavaTraversal candidate recipe self-test passed')
    member_sources.self_test()
    return builder.self_test()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', nargs='?', choices=('build', 'check'))
    parser.add_argument('--self-test', action='store_true')
    outputs = parser.add_mutually_exclusive_group()
    outputs.add_argument('--candidate-output', type=Path,
                         help='write/check the fixed production candidate; isolated debug output of the default recipe')
    outputs.add_argument('--view-measure-test-output', type=Path,
                        help='write/check the fixed sealed JavaTraversal fixture instead of the default path')
    args = parser.parse_args()
    try:
        if args.self_test:
            return self_test()
        if not args.mode:
            parser.error('build, check or --self-test required')
        paired_output = args.candidate_output or args.view_measure_test_output
        if paired_output and paired_output.resolve() == builder.OUTPUT.resolve():
            raise builder.BuildError('paired output must not overwrite the default BootDex')
        jar, dex, recipe = build()
        builder.audit_crc32(dex)
        # The fixed ROM inflater has no Legacy nativeInflate declaration.
        expected = builder.zip_stream_native_signatures()
        expected['Landroid/view/LayoutInflater;'] = {}
        expected['Landroid/content/ContentResolver;'] = {
            'query(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;': 0x111,
            'getType(Landroid/net/Uri;)Ljava/lang/String;': 0x111,
            'hasHandler(Landroid/database/ContentObserver;)Z': 0x10a,
        }
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
            raise builder.BuildError('JavaTraversal native ABI drift: ' + str(observed))
        builder.audit_native_crypto(dex)
        manifest = builder.manifest_bytes(boot_metadata(jar, dex, recipe))
        output = paired_output or builder.OUTPUT
        metadata = output.with_suffix('.manifest.json') if paired_output else builder.MANIFEST
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
