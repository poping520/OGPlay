#!/usr/bin/env python3
"""Copy an isolated CLI/data bundle with a sealed JavaTraversal BootDex (no execution)."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PAIRING = 'META-INF/ogplay/vm-pairing.json'


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate(bootdex: Path) -> dict:
    with zipfile.ZipFile(bootdex) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError('duplicate BootDex entry')
        pairing = json.loads(archive.read(PAIRING))
        if (pairing.get('schema'), pairing.get('mode'), pairing.get('version')) != (1, 'java-traversal', 1):
            raise ValueError('unknown BootDex pairing')
        for key, entry in [('dex_sha256', 'classes.dex'),
                           ('member_sources_sha256', 'META-INF/ogplay/member-sources.json'),
                           ('theme_projection_sha256', 'META-INF/ogplay/theme-projection.json')]:
            if pairing.get(key) != sha(archive.read(entry)):
                raise ValueError('BootDex pairing identity mismatch: ' + key)
        return pairing


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ogplay-cli', type=Path, required=True)
    parser.add_argument('--bootdex', type=Path, required=True)
    parser.add_argument('--data', type=Path, default=ROOT / 'data')
    parser.add_argument('--output', type=Path, required=True, help='new directory; existing paths rejected')
    args = parser.parse_args()
    cli, data, output = args.ogplay_cli.resolve(strict=True), args.data.resolve(strict=True), args.output.resolve()
    pairing = validate(args.bootdex)
    if not cli.is_file() or not data.is_dir():
        parser.error('CLI file and data directory required')
    if output.exists() or output == data or output in data.parents or data in output.parents:
        parser.error('output must be a new directory outside source data')
    # Copy the executable-adjacent shared libraries. This deliberately includes
    # optional ANGLE/SwiftShader libraries, preserving relocatable install names.
    libraries = sorted(p for p in cli.parent.iterdir() if p.is_file() and
                       (p.suffix.lower() in {'.dll', '.dylib', '.so'} or '.so.' in p.name))
    output.mkdir(parents=True)
    shutil.copy2(cli, output / cli.name)
    for library in libraries:
        shutil.copy2(library, output / library.name)
    shutil.copytree(data, output / 'data')
    shutil.copy2(args.bootdex, output / 'data/android/19/framework/bootdex.jar')
    files = {str(p.relative_to(output)): sha(p.read_bytes()) for p in sorted(output.rglob('*')) if p.is_file()}
    expected = {str(p.relative_to(data)): sha(p.read_bytes()) for p in sorted(data.rglob('*')) if p.is_file()}
    replaced = 'android/19/framework/bootdex.jar'
    assert {k[5:]: v for k, v in files.items() if k.startswith('data/') and k[5:] != replaced} == {
        k: v for k, v in expected.items() if k != replaced}
    manifest = dict(schema=1, pairing=pairing, files=files, replaced_data=[replaced],
                    source_cli_sha256=sha(cli.read_bytes()), source_bootdex_sha256=sha(args.bootdex.read_bytes()))
    (output / 'staging-manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(dict(output=str(output), files=len(files), bootdex_sha256=manifest['source_bootdex_sha256'])))


if __name__ == '__main__':
    main()
