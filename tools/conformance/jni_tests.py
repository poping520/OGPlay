#!/usr/bin/env python3
"""Standalone JNI probes. No CTS runtime/test library dependency; see DIAG-8."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import dalvik_tests

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / 'tests/conformance/jni'
BASELINE = Path(__file__).with_name('jni_baseline.json')
BACKENDS = ('switch', 'threaded')
# Scope exclusions are fixed, reviewed facts, never inferred from failed runs.
EXCLUDED_CASES = {}
EXCLUDED_FUNCTIONS = {'DestroyJavaVM': 'The tool owns process termination; guest VM destruction is outside run-dex scope.'}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def header_functions(text):
    """Read only the two C ABI structs, not C++ inline wrappers."""
    result = []
    for struct in ('JNINativeInterface', 'JNIInvokeInterface'):
        body = re.search(r'struct\s+' + struct + r'\s*\{(.*?)\n\};', text, re.S)
        if not body:
            raise ValueError('missing JNI struct: ' + struct)
        result.extend(re.findall(r'\(\s*\*\s*(\w+)\s*\)\s*\(', body[1]))
    return result


def scan_calls(text, names):
    # Strip comments and string literals so mentions do not inflate coverage.
    text = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', '', text, flags=re.S)
    if '##' in text:
        raise ValueError('JNI probe token-pasting (##) is forbidden')
    calls = set(re.findall(r'\(\s*\*\s*(?:env|vm)\s*\)\s*->\s*(\w+)\s*\(', text))
    # Optional suite macros must take an entire authoritative JNI function name.
    calls.update(re.findall(r'\bJNI_CALL\s*\(\s*(\w+)\s*[,)]', text))
    unknown = calls - set(names)
    if unknown:
        raise ValueError('unknown JNI calls: ' + str(sorted(unknown)))
    return calls


def ndk_tools():
    ndk = Path(os.environ.get('OGPLAY_ANDROID_NDK', ROOT / '.local/toolchain/android-ndk-r25c'))
    if 'Pkg.Revision = 25.2.9519653' not in (ndk / 'source.properties').read_text(encoding='utf-8'):
        raise ValueError('requires NDK r25c')
    hosts = [p for p in (ndk / 'toolchains/llvm/prebuilt').iterdir() if p.is_dir()]
    if len(hosts) != 1:
        raise ValueError('expected one NDK host toolchain')
    return hosts[0]


def coverage(host, out):
    ndk_header = host / 'sysroot/usr/include/jni.h'
    names = header_functions(ndk_header.read_text(encoding='utf-8'))
    if len(names) != 234 or len(set(names)) != 234:
        raise ValueError('expected 229 JNIEnv + 5 JavaVM functions')
    reference = ROOT / '.local/aosp-cts/libnativehelper/include/nativehelper/jni.h'
    reference_match = None
    if reference.exists():
        reference_match = names == header_functions(reference.read_text(encoding='utf-8'))
        if not reference_match:
            raise ValueError('NDK / API19 JNI function order differs')
    calls = set()
    for file in sorted((SOURCES / 'native').glob('*')):
        if file.suffix in ('.c', '.h'):
            calls |= scan_calls(file.read_text(encoding='utf-8'), names)
    excluded = {k: v for k, v in EXCLUDED_FUNCTIONS.items() if k in names}
    result = {'header': str(ndk_header), 'api19_header_order_matches': reference_match,
              'covered': sorted(calls), 'uncovered': sorted(set(names) - calls - set(excluded)),
              'excluded': excluded, 'total': len(names), 'eligible': len(names) - len(excluded),
              'percent': round(100 * len(calls) / (len(names) - len(excluded)), 2)}
    write_json(out / 'coverage.json', result)
    print(f"coverage {len(calls)}/{result['eligible']} ({result['percent']}%), excluded {len(excluded)}")
    for key in ('covered', 'uncovered', 'excluded'):
        (out / ('coverage-' + key + '.txt')).write_text(
            '\n'.join(f'{k}: {v}' for k, v in result[key].items()) + '\n' if key == 'excluded'
            else '\n'.join(result[key]) + '\n', encoding='utf-8')
    return result


def checked(cmd):
    done = subprocess.run(list(map(str, cmd)), capture_output=True, text=True)
    if done.returncode:
        raise RuntimeError(' '.join(map(str, cmd)) + '\n' + done.stdout + done.stderr)
    return done.stdout


def build(host, out):
    work = out / 'build'
    work.mkdir(parents=True, exist_ok=True)
    suffix = '.exe' if os.name == 'nt' else ''
    driver = '.cmd' if os.name == 'nt' else ''
    clang = host / ('bin/armv7a-linux-androideabi19-clang' + driver)
    objects = []
    version14 = None
    for source in sorted((SOURCES / 'native').glob('*.c')):
        obj = work / (source.stem + '.o')
        checked([clang, '-std=c11', '-march=armv7-a', '-mfloat-abi=softfp', '-fPIC',
                 '-fno-stack-protector', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter',
                 '-Werror', '-c', source, '-o', obj])
        if source.stem == "library_version14":
            version14 = obj
        else:
            objects.append(obj)
    libraries = out / 'lib'
    libraries.mkdir(exist_ok=True)
    library = libraries / 'libjniprobe.so'
    builtins = list(host.glob('lib*/clang/*/lib/linux/libclang_rt.builtins-arm-android.a'))
    if len(builtins) != 1:
        raise ValueError('NDK ARM builtins missing/ambiguous')
    checked([host / ('bin/ld.lld' + suffix), '-shared', '--hash-style=sysv', '-soname', library.name,
             '-z', 'max-page-size=4096', '--no-undefined', *objects, builtins[0],
             ROOT / 'data/android/19/lib/libc.so', '-o', library])
    checked([host / ('bin/ld.lld' + suffix), '-shared', '--hash-style=sysv', '-soname',
             'libjniprobe14.so', '-z', 'max-page-size=4096', '--no-undefined', version14,
             '-o', libraries / 'libjniprobe14.so'])
    elf14 = checked([host / ('bin/llvm-readelf' + suffix), '-h', '-d', libraries / 'libjniprobe14.so'])
    if not all(x in elf14 for x in ('ELF32', 'ARM', 'DYN', 'Library soname: [libjniprobe14.so]')) or 'Shared library:' in elf14:
        raise ValueError('wrong JNI 1.4 ELF ABI/SONAME/DT_NEEDED: ' + elf14)
    (work / 'elf14.txt').write_text(elf14, encoding='utf-8')
    elf = checked([host / ('bin/llvm-readelf' + suffix), '-h', '-d', library])
    needed = re.findall(r'Shared library: \[([^]]+)\]', elf)
    if not all(x in elf for x in ('ELF32', 'ARM', 'DYN', 'Library soname: [libjniprobe.so]')) or needed != ['libc.so']:
        raise ValueError('wrong ELF ABI/SONAME/DT_NEEDED: ' + elf)
    (work / 'elf.txt').write_text(elf, encoding='utf-8')
    javac, java, android_jar, dx = dalvik_tests.toolchain()
    classes = work / 'classes'
    if classes.exists():
        shutil.rmtree(classes)
    classes.mkdir()
    checked([javac, '-nowarn', '-encoding', 'UTF-8', '-source', '1.7', '-target', '1.7',
             '-bootclasspath', android_jar, '-d', classes, *sorted((SOURCES / 'java').rglob('*.java'))])
    dex = work / 'classes.dex'
    checked([java, '-Xmx1024m', '-cp', dx, 'com.android.dx.command.Main', '--dex', '--debug',
             '--output=' + str(dex), classes])
    return dex, libraries


def classify(ids, stdout, code, timed_out, ledger):
    rows = {}
    for line in stdout.splitlines():
        match = re.fullmatch(r'(PASS|FAIL) ([\w.]+)(?:: (.*))?', line)
        if not match or match[2] not in ids:
            continue
        id = match[2]
        if id in rows:
            rows[id] = {'outcome': 'fail:assert', 'detail': 'duplicate result line'}
            continue
        detail = match[3] or ''
        outcome = 'pass' if match[1] == 'PASS' else 'fail:assert'
        if outcome != 'pass' and (ledger or re.search(r'unimplemented|not implemented|unsupported|unbound', detail, re.I)):
            outcome = 'fail:unimplemented'
        rows[id] = {'outcome': outcome, 'detail': detail, 'output': line, 'ledger': ledger}
    named_gap = bool(ledger or re.search(r'unimplemented|not implemented|unsupported|unbound JNI', stdout, re.I))
    for id in ids:
        if id in EXCLUDED_CASES:
            rows[id] = {'outcome': 'excluded:' + EXCLUDED_CASES[id], 'detail': EXCLUDED_CASES[id]}
        elif id not in rows:
            outcome = 'fail:timeout' if timed_out else ('fail:unimplemented' if named_gap else ('fail:crash' if code else 'fail:missing'))
            rows[id] = {'outcome': outcome, 'detail': stdout[-2000:], 'ledger': ledger}
    return rows


def execute(cli, dex, libraries, backend, args, path, timeout):
    path.parent.mkdir(parents=True, exist_ok=True)
    ledger_file = Path(str(path) + '.ledger.json')
    ledger_file.unlink(missing_ok=True)
    cmd = [str(cli), 'dexvm', 'run-dex', str(dex), '--class', 'probe.Main', '--backend', backend,
           '--native-lib-dir', str(libraries), '--ledger-json', str(ledger_file), '--', *args]
    timed_out = False
    try:
        done = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        stdout, code = done.stdout, done.returncode
    except subprocess.TimeoutExpired as error:
        stdout, code, timed_out = error.stdout or b'', None, True
    text = stdout.decode('utf-8', errors='replace')
    Path(str(path) + '.log').write_bytes(stdout)
    ledger = json.loads(ledger_file.read_text(encoding='utf-8')).get('unimplemented', []) if ledger_file.exists() else []
    return text, code, timed_out, ledger


def compare_baseline(baseline, current, backends, ids):
    changes = {'regressions': [], 'new_passes': [], 'other_changes': []}
    for backend in backends:
        for id in ids:
            before = baseline.get(backend, {}).get(id, 'missing')
            after = current.get(backend, {}).get(id, 'missing')
            if before == after:
                continue
            kind = 'regressions' if before == 'pass' else ('new_passes' if after == 'pass' else 'other_changes')
            changes[kind].append({'backend': backend, 'case': id, 'before': before, 'after': after})
    return changes


def save_baseline(path, baseline, current, changes):
    if changes['regressions']:
        return False
    merged = {b: dict(rows) for b, rows in baseline.items()}
    for backend, rows in current.items():
        merged.setdefault(backend, {}).update(rows)
    write_json(path, merged)
    return True


def self_test():
    def outcome(output='', code=0, timeout=False, ledger=None):
        return classify(['a'], output, code, timeout, ledger or [])['a']['outcome']
    assert outcome('PASS a') == 'pass'
    assert outcome('FAIL a: wrong') == 'fail:assert'
    assert outcome('FAIL a: unimplemented Foo') == 'fail:unimplemented'
    assert outcome('FAIL a: wrong', ledger=[{'id': 'Foo'}]) == 'fail:unimplemented'
    assert outcome('unbound JNI guest slot: DefineClass', code=70) == 'fail:unimplemented'
    assert outcome(code=-11) == 'fail:crash'
    assert outcome(timeout=True) == 'fail:timeout'
    assert outcome() == 'fail:missing'
    assert outcome('PASS a\nPASS a') == 'fail:assert'
    assert classify(['a'], 'FAIL a: bad', 0, False, [])['a']['output'] == 'FAIL a: bad'
    EXCLUDED_CASES['a'] = 'self-test fixed exclusion'
    assert outcome() == 'excluded:self-test fixed exclusion'
    del EXCLUDED_CASES['a']
    baseline = {'switch': {'a': 'pass'}, 'threaded': {'a': 'pass'}}
    for failure in ('fail:assert', 'fail:unimplemented', 'fail:crash', 'fail:timeout', 'fail:missing', 'excluded:reason', 'missing'):
        current = {'switch': {'a': failure}}
        changes = compare_baseline(baseline, current, BACKENDS, ['a'])
        assert len(changes['regressions']) == 2
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp) / 'baseline.json'
            write_json(p, baseline)
            before = p.read_bytes()
            assert not save_baseline(p, baseline, current, changes)
            assert p.read_bytes() == before
    current = {'switch': {'a': 'pass', 'b': 'pass'}}
    changes = compare_baseline(baseline, current, ['switch'], ['a', 'b'])
    assert not changes['regressions'] and len(changes['new_passes']) == 1
    with tempfile.TemporaryDirectory() as temp:
        p = Path(temp) / 'b.json'
        assert save_baseline(p, baseline, current, changes)
        assert json.loads(p.read_text())['threaded'] == baseline['threaded']
    names = ['GetVersion', 'GetEnv']
    assert scan_calls('(*env)->GetVersion(env); (*vm)->GetEnv(vm,p,0);', names) == set(names)
    assert scan_calls('/* (*env)->GetVersion(env) */ "(*vm)->GetEnv("', names) == set()
    assert scan_calls('JNI_CALL(GetVersion, env)', names) == {'GetVersion'}
    try:
        scan_calls('#define CALL(x) (*env)->Call##x##Method(env)', names)
        raise AssertionError('token pasting accepted')
    except ValueError:
        pass
    synthetic = 'struct JNINativeInterface {\n void *reserved0;\n int (*GetVersion)(void*);\n};\nstruct JNIInvokeInterface {\n int (*GetEnv)(void*);\n};'
    assert header_functions(synthetic) == names
    print('JNI probe self-test passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--out', type=Path, default=ROOT / '.local/analysis/wu8-0')
    parser.add_argument('--cli', type=Path, default=ROOT / 'build/release/ogplay-cli')
    parser.add_argument('--backend', choices=BACKENDS, action='append')
    parser.add_argument('--group', action='append')
    parser.add_argument('--case', action='append')
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--coverage', action='store_true')
    parser.add_argument('--check-baseline', action='store_true')
    parser.add_argument('--update-baseline', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    args.out = args.out.resolve()
    host = ndk_tools()
    if args.coverage:
        coverage(host, args.out)
    dex, libraries = build(host, args.out)
    backends = args.backend or BACKENDS
    # --list runs before loading the probe library; inventory comes from guest Java.
    listing, code, timeout, ledger = execute(args.cli, dex, libraries, backends[0], ['--list'], args.out / 'list', args.timeout)
    if code or timeout:
        raise RuntimeError('cannot enumerate cases: ' + listing)
    all_ids = [s for s in listing.splitlines() if re.fullmatch(r'[a-z_]+\.[\w.]+', s)]
    if not all_ids or len(set(all_ids)) != len(all_ids):
        raise ValueError('invalid or empty --list inventory')
    ids = [id for id in all_ids if (not args.group or id.split('.')[0] in args.group) and (not args.case or id in args.case)]
    if not ids or (args.case and set(args.case) - set(all_ids)) or (args.group and set(args.group) - {id.split('.')[0] for id in all_ids}):
        raise ValueError('unknown/empty case or group selection')
    results = {}
    for backend in backends:
        rows = results[backend] = {}
        for group in sorted({id.split('.')[0] for id in ids}):
            group_ids = [id for id in ids if id.startswith(group + '.')]
            run_ids = group_ids if args.case else None
            if run_ids:
                for id in run_ids:
                    data = execute(args.cli, dex, libraries, backend, ['--case', id], args.out / backend / id, args.timeout)
                    rows.update(classify([id], *data))
            else:
                data = execute(args.cli, dex, libraries, backend, ['--group', group], args.out / backend / group, args.timeout)
                group_rows = classify(group_ids, *data)
                if data[2] or data[1]:
                    for id in group_ids:
                        isolated = execute(args.cli, dex, libraries, backend, ['--case', id], args.out / backend / id, args.timeout)
                        group_rows.update(classify([id], *isolated))
                rows.update(group_rows)
            print(backend, group, dict(Counter(rows[id]['outcome'] for id in group_ids)), flush=True)
        for id, row in rows.items():
            write_json(args.out / backend / (id + '.json'), row)
    current = {b: {id: row['outcome'] for id, row in rows.items()} for b, rows in results.items()}
    baseline = json.loads(BASELINE.read_text(encoding='utf-8')) if BASELINE.exists() else {}
    # Full runs also detect disappeared baseline cases; selections compare their scope only.
    comparison_ids = ids if args.group or args.case else sorted(set(ids) | {id for b in backends for id in baseline.get(b, {})})
    changes = compare_baseline(baseline, current, backends, comparison_ids)
    write_json(args.out / 'summary.json', {'results': results, 'counts': {b: dict(Counter(rows.values())) for b, rows in current.items()}, 'baseline_changes': changes})
    for b, rows in current.items():
        passed = sum(v == 'pass' for v in rows.values())
        total = sum(not v.startswith('excluded:') for v in rows.values())
        print(f'{b}: {passed}/{total} ({100*passed/total:.1f}%)')
    if args.update_baseline and not save_baseline(BASELINE, baseline, current, changes):
        return 1
    return int(bool(changes['regressions'])) if args.check_baseline or args.update_baseline else 0


if __name__ == '__main__':
    raise SystemExit(main())
