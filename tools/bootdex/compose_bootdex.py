#!/usr/bin/env python3
"""Build/check opt-in ROM member composition without changing the pinned JNI generator.

The original build_bootdex.py is itself native-payload provenance. Keep its bytes
stable; reuse its assembly, audits, canonical ZIP and metadata implementations.
"""
from __future__ import annotations

import argparse
import io
import json
from pathlib import Path
import tempfile
import zipfile

import build_bootdex as builder
import member_sources


def assemble(recipe, work):
    dex = builder.assemble(recipe, work)
    document = member_sources.load()
    for cls in document['classes']:
        sources = [s for s, owners in recipe.items() if cls['owner'] in owners]
        if len(sources) != 1 or (builder.ROOT / cls['rom_input']).resolve() != builder.source_path(sources[0]).resolve():
            raise builder.BuildError('composition class must come from its pinned ROM recipe input')
    roots = [work / (s + '-smali') for s in recipe] + [work / 'guest-java-smali']
    payload = member_sources.payload(document, roots, builder.ROOT)
    return dex, payload


def build():
    recipe = builder.load_recipe()
    builder.verify_inputs(recipe)
    with tempfile.TemporaryDirectory(prefix='ogplay-composition-a-') as first, \
            tempfile.TemporaryDirectory(prefix='ogplay-composition-b-') as second:
        dex, payload = assemble(recipe, Path(first))
        if (dex, payload) != assemble(recipe, Path(second)):
            raise builder.BuildError('two BootDex DEX/member source assemblies differ')
    with tempfile.TemporaryDirectory(prefix='ogplay-composition-java-audit-') as work:
        selected = builder.compile_expected_class_names(recipe, Path(work))
    if builder.class_names(dex) != selected:
        raise builder.BuildError('generated classes differ from recipe')
    resources = builder.resource_payload(builder.load_resources())
    if member_sources.ENTRY in resources or builder.SYSTEM_RESOURCES_ENTRY in resources:
        raise builder.BuildError('reserved BootDex resource entry in input')
    if payload is not None:
        resources[member_sources.ENTRY] = payload
    resources[builder.SYSTEM_RESOURCES_ENTRY] = builder.system_resources_payload(dex)
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
    return metadata


def self_test():
    member_sources.self_test()
    return builder.self_test()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', nargs='?', choices=('build', 'check'))
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    try:
        if args.self_test:
            return self_test()
        if not args.mode:
            parser.error('build, check or --self-test required')
        jar, dex, recipe = build()
        builder.audit_crc32(dex)
        builder.audit_zip_streams(dex)
        builder.audit_native_crypto(dex)
        manifest = builder.manifest_bytes(boot_metadata(jar, dex, recipe))
        if args.mode == 'build':
            builder.OUTPUT.write_bytes(jar)
            builder.MANIFEST.write_bytes(manifest)
        elif builder.OUTPUT.read_bytes() != jar or builder.MANIFEST.read_bytes() != manifest:
            raise builder.BuildError('BootDex payload is stale; run compose_bootdex.py build')
        print(f'BootDex composition {args.mode}: {len(builder.class_names(dex))} classes, '
              f'DEX {builder.sha256(dex)}, JAR {builder.sha256(jar)}')
        return 0
    except (builder.BuildError, OSError, ValueError, KeyError, TypeError,
            builder.dex_survey_lib.DexFormatError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    raise SystemExit(main())
