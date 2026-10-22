#!/usr/bin/env python3
"""Extract API19 ContextImpl registration and System.initSystemProperties evidence.

Generation alone reads the pinned local AOSP checkout; conformance uses the JSON.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
FILES = {
    'context': 'framework/base/core/java/android/content/Context.java',
    'registry': 'framework/base/core/java/android/app/ContextImpl.java',
    'system': 'libcore/luni/src/main/java/java/lang/System.java',
    'binding': 'framework/base/core/java/android/app/ActivityThread.java',
}


def extract(root):
    texts = {key: (root / path).read_text(encoding='utf-8') for key, path in FILES.items()}
    constants = dict(re.findall(r'\bString\s+(\w+)\s*=\s*"([^"]+)"', texts['context']))
    registrations = re.findall(r'\bregisterService\(\s*([A-Z][A-Z0-9_]+)\s*,', texts['registry'])
    if len(registrations) != len(set(registrations)):
        raise ValueError('duplicate service registration')
    services = {constants[name]: name for name in registrations}
    body = texts['system'].split('private static void initSystemProperties() {', 1)[1].split('systemProperties = p;', 1)[0]
    expressions = dict(re.findall(r'p\.put\("([^"]+)",\s*([^;]+)\);', body))
    properties = {}
    variables = {'projectName': 'The Android Project', 'projectUrl': 'http://www.android.com/'}
    for key, expression in expressions.items():
        entry = {'expression': expression}
        if expression.startswith('"'):
            entry['value'] = json.loads(expression)
        elif expression in variables:
            entry['value'] = variables[expression]
        elif expression.startswith('info.'):
            entry['source'] = 'uname.' + expression[5:]
        elif expression.startswith('passwd.'):
            entry['source'] = 'passwd.' + expression[7:]
        elif expression.startswith('ICU.'):
            entry['source'] = expression
        elif expression == 'javaHome':
            entry.update(source='getenv.JAVA_HOME', fallback='/system')
        elif expression == 'ldLibraryPath':
            entry.update(source='getenv.LD_LIBRARY_PATH', conditional=True)
        else:
            entry['source'] = expression
        properties[key] = entry
    # Keep the ActivityThread override separately: /tmp is correct before binding.
    if 'System.setProperty("java.io.tmpdir", cacheDir.getAbsolutePath())' not in texts['binding']:
        raise ValueError('missing API19 cache binding rule')
    properties['java.io.tmpdir']['application_binding'] = 'Context.getCacheDir().getAbsolutePath()'
    if len(services) != 44 or len(properties) != 37:
        raise ValueError(f'unexpected pinned source inventory: {len(services)} services, {len(properties)} properties')
    return {
        'schema': 1, 'api': 19, 'reference': 'AOSP android-4.4.4_r2.0.1',
        'sources': {key: {'path': path, 'sha256': hashlib.sha256((root/path).read_bytes()).hexdigest()} for key, path in FILES.items()},
        'system_services': dict(sorted(services.items())),
        'system_properties': dict(sorted(properties.items())),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aosp', type=Path, default=ROOT/'.local/aosp')
    parser.add_argument('--output', type=Path, default=Path(__file__).with_name('api19_platform_tables.json'))
    args = parser.parse_args()
    result = extract(args.aosp)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"Extracted {len(result['system_services'])} services and {len(result['system_properties'])} properties")


if __name__ == '__main__':
    main()
