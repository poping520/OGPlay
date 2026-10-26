#!/usr/bin/env python3
"""Offline conservative comparison of nine frozen Result-v1 cases; never launches a process.
Matrix cases contain frozen inputs/implementation, hash-checked Result v1/evidence,
and fault_source (failed only): {phase, checkpoint, log: {path,sha256}}.
The normalizer extracts the first Java exception/member or native module+relative PC.
Phase is an audited trace annotation from Application/Activity/Surface/frame/shutdown;
checkpoint must match Result v1. Unknown evidence fails closed, never a guessed identity.
Exit code is extracted from Result firstFailure/shutdown; a raw address is never a PC.
Use freeze --group B|C --evidence-root DIR --bundle BUNDLE --output MATRIX to seal existing runs.
Use --self-test for synthetic Result v1 fixtures; no game is launched.
Exit 0=pass, 1=fail/needs_review, 2=invalid. No subprocesses are launched.
"""
import argparse
import re
import copy
import tempfile
import hashlib
import json
import sys
import zipfile
from pathlib import Path

VERSION = 'vd11-reviewed-fault-v1'
PHASES = ('Application', 'Activity', 'Surface', 'frame', 'shutdown')
SAME = {'scenario_sha256', 'profile_sha256', 'quirks_sha256', 'fixture_sha256_by_id',
        'sandbox_seed_sha256', 'display_settings', 'backend', 'hardware', 'run_options', 'budgets'}
BUILD = {'binary_sha256', 'bootdex_sha256', 'member_manifest_sha256', 'mode', 'mode_version',
         'ROM_hashes', 'font_hashes', 'build_id'}


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def file_ref(base, ref):
    path = (base / ref['path']).resolve()
    if hashlib.sha256(path.read_bytes()).hexdigest() != ref['sha256']:
        raise ValueError('evidence hash mismatch: ' + str(path))
    return path


def validate_result(result):
    required = {'schema', 'scenarioId', 'status', 'survey', 'profile', 'checkpoints', 'firstFailure', 'shutdown'}
    if (set(result) != required or type(result['schema']) is not int or result['schema'] != 1 or
            result['survey'] is not False or result['status'] not in ('passed', 'failed') or
            not isinstance(result['scenarioId'], str) or not result['scenarioId'] or
            not isinstance(result['profile'], dict) or not isinstance(result['checkpoints'], list) or
            not (result['firstFailure'] is None or isinstance(result['firstFailure'], str))):
        raise ValueError('not normal-mode Result v1')
    shutdown = result['shutdown']
    if (set(shutdown) != {'requested', 'clean', 'error'} or
            any(type(shutdown[k]) is not bool for k in ('requested', 'clean')) or
            not (shutdown['error'] is None or isinstance(shutdown['error'], str))):
        raise ValueError('invalid Result shutdown')
    checkpoint_keys = {'id', 'status', 'startFrame', 'endFrame', 'startTicks', 'endTicks',
                       'wallTimeMs', 'assertions', 'evidence', 'error'}
    failed = False
    for c in result['checkpoints']:
        if (set(c) != checkpoint_keys or not isinstance(c['id'], str) or not c['id'] or
                c['status'] not in ('passed', 'failed') or failed or
                not isinstance(c['assertions'], list) or not isinstance(c['evidence'], list) or
                not all(isinstance(e, str) and e for e in c['evidence']) or
                not (c['error'] is None or isinstance(c['error'], str))):
            raise ValueError('invalid ordered checkpoint')
        failed = c['status'] == 'failed'
        for key in ('startFrame', 'endFrame', 'startTicks', 'endTicks', 'wallTimeMs'):
            if type(c[key]) is not int or c[key] < 0:
                raise ValueError('invalid checkpoint counter')
        if c['endFrame'] < c['startFrame'] or c['endTicks'] < c['startTicks']:
            raise ValueError('checkpoint counters reversed')
        for a in c['assertions']:
            if (set(a) != {'type', 'passed', 'expected', 'actual'} or
                    not isinstance(a['type'], str) or not a['type'] or type(a['passed']) is not bool or
                    not isinstance(a['expected'], dict) or not isinstance(a['actual'], dict)):
                raise ValueError('invalid Result assertion')


def normalize_fault(case, result, base):
    if result['status'] == 'passed':
        if case.get('fault_source') is not None or case.get('fault') is not None:
            raise ValueError('passing result has fault evidence')
        return None
    source = case['fault_source']
    phase = source['phase']
    if phase not in PHASES:
        raise ValueError('unknown phase; trace review required')
    prefix = []
    failed = None
    for checkpoint in result['checkpoints']:
        if checkpoint['status'] != 'passed':
            failed = checkpoint['id']
            break
        prefix.append(checkpoint['id'])
    if source['checkpoint'] != failed:
        raise ValueError('fault checkpoint differs from Result prefix')
    if phase == 'shutdown' and (failed is not None or not result['shutdown']['requested']):
        raise ValueError('shutdown fault cannot precede successful checkpoints')
    log = file_ref(base, source['log']).read_text(encoding='utf-8')
    # Restrict extraction to the first fatal frontend report. Later cleanup
    # exceptions cannot replace an unrecognized earlier CPU/native failure.
    raw_log = log
    fatal = re.search(r'^error[^\n]*frontend\.run_apk\.[^ \n]+ |^error \[ogplay\]\r?\n', log, re.MULTILINE)
    checkpoint_timeout = (failed is not None and result['firstFailure'] ==
                          f'{failed}: checkpoint {failed} wall-time budget exceeded')
    if checkpoint_timeout and fatal and 'frontend.run_apk.teardown ' in fatal[0]:
        # Do not let a later cleanup report replace the checkpoint's first failure.
        log = raw_log[:fatal.start()]
        fatal = None
    if fatal:
        log = log[fatal.start():]
        following = re.search(r'\nerror[^\n]* ', log)
        if following:
            log = log[:following.start()]
    # Phase names are trace facts, not an arbitrary rank supplied by the matrix.
    markers = list(re.finditer(r'^\[owner\] phase=(Application|Activity|Surface|frame|shutdown)\s*$',
                               raw_log[:fatal.start()] if fatal else log, re.MULTILINE))
    proven_phase = markers[-1][1] if markers else None
    if checkpoint_timeout and fatal is None:
        # The runner already entered this checkpoint; later cleanup trace markers
        # cannot move its fault-free timeout to the shutdown stage.
        proven_phase = 'frame'
    elif fatal and 'frontend.run_apk.teardown ' in fatal[0]:
        proven_phase = 'shutdown'
    elif proven_phase is None:
        if re.search(r'Application (?:onCreate|<init>)', log):
            proven_phase = 'Application'
        elif re.search(r'(?:Activity |activity )?(?:onCreate|<init>|startup) (?:failed|raised)', log, re.MULTILINE):
            proven_phase = 'Activity'
        elif re.search(r'surface(?:Created|Changed|RedrawNeeded) raised', log):
            proven_phase = 'Surface'
        elif failed is not None:
            # Runner checkpoints start only after startup completed. This proves
            # the frame/checkpoint stage; it does not claim visual acceptance.
            proven_phase = 'frame'
    if proven_phase != phase:
        raise ValueError('phase lacks matching raw trace/Result evidence')
    # Keep the original exception, exact member descriptor, message dimensions and
    # counts. Only native PCs explicitly expressed relative to a module normalize.
    java = re.search(r'exception: (L[^;\n]+;)[\r\n]+\s*message: ([^\n]+)', log)
    if java is None:
        java = re.search(r'scheduled callback raised: (L[^;\n]+;): ([^\n]+)', log)
    native = re.search(r'(?:native fault|guest fault): ([^\n]+?) module=([^ /\n]+) relative_pc=(0x[0-9a-fA-F]+)', log)
    if java and (not native or java.start() < native.start()):
        message = java[2].strip()
        member = re.search(r'L[^;\s]+;[.>-]+[^\s:]+', message)
        if not member:
            # A Java exception without a member is still identifiable by the
            # precise first Java stack member; never discard exception text.
            member = re.search(r'^\s*at (L[^;\n]+;[.>-]+[^\s]+)',
                               log[java.end():].split('Caused by:', 1)[0], re.MULTILINE)
            identity = member[1] if member else None
        else:
            identity = member[0]
        if not identity:
            boundary = re.search(r'^\s*guest boundary: (Activity startup|Activity suspend|Activity resume|Activity frame)\s*$', log, re.MULTILINE)
            if boundary is None:
                raise ValueError('Java first fault has no exact member identity')
            # This is a host entry boundary, not an inferred Java stack member.
            signature = dict(category='java_boundary', exception=java[1], boundary=boundary[1],
                             message=message, requires_review=True)
        else:
            signature = dict(category='java', exception=java[1], member=identity, message=message)
        # Preserve the reviewed first report's cause/target chain. New cause shapes
        # cannot disappear behind an unchanged InflateException wrapper.
        causes = []
        for line in log[java.end():].splitlines():
            if re.match(r"(?:info|warn|error) \[", line):
                break
            if 'Caused by:' not in line:
                continue
            text = line.strip()
            cause = re.fullmatch(r'Caused by: (L[^;\s]+;)(?:: (.*))?', text)
            frame = re.fullmatch(r'Caused by:   at (L[^;\s]+;\.[^\s]+) \(pc ([0-9]+|unavailable)\)', text)
            if cause:
                causes.append(dict(exception=cause[1], message=cause[2] or ''))
            elif frame and causes and 'first_member' not in causes[-1]:
                causes[-1]['first_member'] = frame[1]
            else:
                raise ValueError('unreviewed Java cause shape')
        if causes:
            signature['causes'] = causes
    elif native:
        signature = dict(category='native', module=native[2], relative_pc=hex(int(native[3], 16)),
                         error=native[1])
    elif (entry := re.search(r'DexVM native invocation failed:\n\s+class=([^\s]+)\n\s+method=([^\s]+)\n\s+descriptor=(\([^\n]+)\n', log)):
        # These reports prove the exact JNI entry, but contain bare ASLR PCs.
        # Preserve the entire first report as opaque evidence; never claim a
        # module-relative native identity or automatically pass equal reports.
        if not re.search(r'stop: +pc=0x[0-9a-f]+ state=a32 reason=memory_fault\(4\)', log) or not re.search(
                r'fault: +address=0x[0-9a-f]+ access=(?:read|write|execute)\(\d+\) reason=\w+\(\d+\)', log):
            raise ValueError('unrecognized native invocation report')
        signature = dict(category='native_entry_report', member='L' + entry[1] + ';->' + entry[2] + entry[3],
                         report_sha256=hashlib.sha256(log.encode('utf-8')).hexdigest(), requires_review=True)
    elif re.match(r'error[^\n]*frontend\.run_apk\.fault UI node is stale(?:\r?\n|$)', log):
        # Exact host boundary only: do not infer a guest member or hide the fault.
        signature = dict(category='host_ui_stale_node', boundary='frontend.run_apk.fault',
                         message='UI node is stale', requires_review=True)
    elif (phase == 'shutdown' and fatal and 'frontend.run_apk.teardown ' in fatal[0] and
          re.fullmatch(r'error[^\n]*frontend\.run_apk\.teardown Android guest invocation failed: teardown cleanup wall-time budget exhausted\s*', log)):
        # A proven host cleanup budget is not a fabricated Java/native member.
        signature = dict(category='teardown_budget', message='teardown cleanup wall-time budget exhausted',
                         requires_review=True)
    elif (fatal is None and failed is not None and phase == 'frame' and
          result['firstFailure'] == f'{failed}: checkpoint {failed} wall-time budget exceeded'):
        checkpoint = next(c for c in result['checkpoints'] if c['id'] == failed)
        frames = [a for a in checkpoint['assertions'] if a['type'] == 'frame']
        no_fault = [a for a in checkpoint['assertions'] if a['type'] == 'no_guest_fault']
        if (checkpoint['error'] != f'checkpoint {failed} wall-time budget exceeded' or
                len(frames) != 1 or len(no_fault) != 1 or no_fault[0]['passed'] is not True or
                no_fault[0]['actual'] != {'guestFault': None} or frames[0]['passed'] is not False or
                type(frames[0]['actual'].get('sequence')) is not int or frames[0]['actual']['sequence'] < 0 or
                re.search(r'uncaught Java exception|exception:|native fault|guest fault|^error[^\n]*frontend\.run_apk\.|^error \[ogplay\]', log, re.MULTILINE)):
            raise ValueError('timeout lacks fault-free checkpoint frame evidence')
        signature = dict(category='timeout', checkpoint=failed, presented_frames=frames[0]['actual']['sequence'])
    else:
        raise ValueError('unrecognized first fault; manual normalizer review required')
    errors = [result['firstFailure'] or '', result['shutdown']['error'] or '']
    exits = [int(code) for error in errors for code in re.findall(r'exited with status (-?\d+)', error)]
    if len(set(exits)) > 1:
        raise ValueError('contradictory exit codes')
    timeout = any(re.search(r'timeout|timed out|wall-time budget (?:exhausted|exceeded)', e, re.IGNORECASE) for e in errors)
    if not exits and not timeout:
        raise ValueError('missing process exit/timeout evidence')
    return dict(signature=signature, phase_name=phase, phase_rank=PHASES.index(phase),
                checkpoint_prefix=prefix, exit_code=exits[0] if exits else None,
                cleanup_timeout=timeout, evidence=[source['log']])


def matrix(path):
    doc = read(path)
    if doc['schema'] != 1 or doc['normalizer_version'] != VERSION:
        raise ValueError('unrecognized manifest or fault normalizer')
    cases = {}
    for case in doc['cases']:
        if not SAME <= case['inputs'].keys() or not BUILD <= case['implementation'].keys():
            raise ValueError('missing frozen inputs/implementation identity')
        result = read(file_ref(path.parent, case['result']))
        validate_result(result)
        checkpoints = result['checkpoints']
        ids = [c['id'] for c in checkpoints]
        if len(set(ids)) != len(ids):
            raise ValueError('duplicate checkpoints')
        for c in checkpoints:
            if c['status'] not in ('passed', 'failed') or any(type(a['passed']) is not bool for a in c['assertions']):
                raise ValueError('invalid checkpoint/assertion')
            if c['status'] == 'passed' and (c['error'] is not None or not all(a['passed'] for a in c['assertions'])):
                raise ValueError('inconsistent passing checkpoint')
        if not case['evidence']:
            raise ValueError('missing raw evidence')
        for ref in case['evidence']:
            file_ref(path.parent, ref)
        case['fault'] = normalize_fault(case, result, path.parent)
        if result['status'] == 'passed':
            if result['firstFailure'] is not None or not result['shutdown']['clean'] or not all(c['status'] == 'passed' for c in checkpoints):
                raise ValueError('inconsistent pass')
            if case['fault'] is not None:
                raise ValueError('passed with fault')
        else:
            f = case['fault']
            if not result['firstFailure'] or not f or not f['signature'].get('category') or not f['evidence']:
                raise ValueError('failed result lacks precise normalized first fault')
            if f['signature']['category'] in ('exit_status', 'unknown'):
                raise ValueError('exit status alone is not a first-fault identity')
            if type(f['phase_rank']) is not int or not f['phase_name']:
                raise ValueError('missing proven phase')
            for ref in f['evidence']:
                file_ref(path.parent, ref)
        sid = result['scenarioId']
        if sid in cases:
            raise ValueError('duplicate scenario')
        cases[sid] = (case, result)
    if len(cases) != 9:
        raise ValueError('expected all nine scenarios')
    return cases


def compare(old, new):
    a, ar = old
    b, br = new
    reasons = []
    review = []
    if any(case.get('fault') and case['fault']['signature'].get('requires_review') for case in (a, b)):
        review.append('first report has only a reviewed host/entry boundary; precise guest fault review required')
    if a['inputs'] != b['inputs'] or ar['profile'] != br['profile']:
        return {'verdict': 'invalid', 'reasons': ['frozen inputs differ']}
    successful = []
    for c in ar['checkpoints']:
        if c['status'] != 'passed':
            break
        successful.append(c)
    for i, prior in enumerate(successful):
        if i >= len(br['checkpoints']):
            reasons.append('missing previously successful checkpoint')
            break
        current = br['checkpoints'][i]
        expectations = lambda c: [(x['type'], x['expected']) for x in c['assertions']]
        if current['id'] != prior['id'] or current['status'] != 'passed' or expectations(current) != expectations(prior):
            reasons.append('successful checkpoint prefix regressed')
            break
    if ar['status'] == 'passed' and br['status'] != 'passed':
        reasons.append('previous pass now fails')
    elif br['status'] == 'failed':
        af, bf = a['fault'], b['fault']
        if bf['signature'] != af['signature']:
            review.append('changed first fault requires chronology review; no automatic pass')
        old_failed = next((c['id'] for c in ar['checkpoints'] if c['status'] == 'failed'), None)
        new_failed = next((c['id'] for c in br['checkpoints'] if c['status'] == 'failed'), None)
        if old_failed != new_failed and old_failed is not None and old_failed not in bf['checkpoint_prefix']:
            reasons.append('failed checkpoint changed without a proven prefix')
        if bf['phase_rank'] < af['phase_rank']:
            reasons.append('first fault moved earlier')
        if bf['exit_code'] != af['exit_code'] or (bf['cleanup_timeout'] and not af['cleanup_timeout']):
            reasons.append('shutdown deteriorated')
    return {'verdict': 'needs_review' if review else 'fail' if reasons else 'pass',
            'reasons': reasons + review, 'baseline': ar['status'], 'candidate': br['status'],
            'baseline_first_fault': a['fault'], 'candidate_first_fault': b['fault'],
            'checkpoint_prefix': [x['id'] for x in successful],
            'implementation': b['implementation'],
            'evidence_sha256': [a['result']['sha256'], b['result']['sha256']],
            'shutdown': br['shutdown'], 'new_fault': bool(review)}


def freeze(group, evidence_root, bundle, output):
    """Rebind frozen run identities to existing evidence, never infer historical inputs.

    A JSON bundle is a preserved schema-1 comparison matrix. A staged directory
    contains comparison-manifest.json with the same frozen inputs/ROM identities;
    executable, libraries, BootDex pairing and fonts are hashed from that directory.
    """
    bundle = bundle.resolve()
    template = bundle / 'comparison-manifest.json' if bundle.is_dir() else bundle
    if output == template.resolve():
        raise ValueError('freeze output must not replace its preserved bundle')
    doc = read(template)
    if doc['schema'] != 1 or doc['normalizer_version'] != VERSION or len(doc['cases']) != 9:
        raise ValueError('freeze requires nine frozen input/implementation identities')
    doc = copy.deepcopy(doc)

    def ref(path):
        return dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())

    for row in doc['cases']:
        if not SAME <= row['inputs'].keys() or not BUILD <= row['implementation'].keys():
            raise ValueError('missing frozen inputs/implementation identity')
        # scenarioId may be supplied explicitly by a run manifest; preserved
        # matrices instead carry a hash-checked Result from the original run.
        sid = row.get('scenarioId')
        original = None
        if sid is None:
            original = read(file_ref(template.parent, row['result']))
            validate_result(original)
            sid = original['scenarioId']
        matches = []
        for result_path in evidence_root.glob('*/result.json'):
            if read(result_path).get('scenarioId') == sid:
                matches.append(result_path)
        if len(matches) != 1:
            raise ValueError('freeze needs exactly one Result for ' + str(sid))
        result_path = matches[0]
        result = read(result_path)
        validate_result(result)
        if original is not None and original['profile'] != result['profile']:
            raise ValueError('preserved run profile differs for ' + sid)
        directory = result_path.parent
        log_path = directory / 'stderr.log'
        if directory.resolve() in output.parents:
            raise ValueError('freeze output must not replace raw evidence')
        identity = row['implementation']
        if bundle.is_dir():
            cli = bundle / ('ogplay-cli.exe' if (bundle / 'ogplay-cli.exe').exists() else 'ogplay-cli')
            jar = bundle / 'data/android/19/framework/bootdex.jar'
            with zipfile.ZipFile(jar) as archive:
                if len(archive.namelist()) != len(set(archive.namelist())):
                    raise ValueError('duplicate BootDex entry')
                pairing_name = 'META-INF/ogplay/vm-pairing.json'
                pairing = None
                if pairing_name in archive.namelist():
                    pairing = json.loads(archive.read(pairing_name))
                    if (pairing.get('schema'), pairing.get('mode'), pairing.get('version')) != (1, 'java-traversal', 1):
                        raise ValueError('unknown BootDex pairing')
                    for key, entry in [('dex_sha256', 'classes.dex'),
                                       ('member_sources_sha256', 'META-INF/ogplay/member-sources.json'),
                                       ('theme_projection_sha256', 'META-INF/ogplay/theme-projection.json')]:
                        if pairing[key] != hashlib.sha256(archive.read(entry)).hexdigest():
                            raise ValueError('BootDex pairing mismatch')
                mode = 'JavaTraversal' if pairing else 'Legacy'
                member = hashlib.sha256(archive.read('META-INF/ogplay/member-sources.json')).hexdigest()
            identity.update(binary_sha256=ref(cli)['sha256'], build_id=ref(cli)['sha256'],
                            bootdex_sha256=ref(jar)['sha256'], member_manifest_sha256=member,
                            mode=mode, mode_version=1 if pairing else 0)
            identity['runtime_library_hashes'] = {
                p.name: ref(p)['sha256'] for p in sorted(bundle.iterdir()) if p.is_file() and
                (p.suffix.lower() in {'.dylib', '.so', '.dll'} or '.so.' in p.name)}
            identity['font_hashes'] = {
                str(p.relative_to(bundle)): ref(p)['sha256'] for p in sorted((bundle / 'data').rglob('*'))
                if p.is_file() and p.suffix.lower() in {'.ttf', '.otf'}}
        if identity['mode'] != ('JavaTraversal' if group == 'C' else 'Legacy'):
            raise ValueError('group does not match preserved bundle mode')
        failed = next((c['id'] for c in result['checkpoints'] if c['status'] == 'failed'), None)
        row['result'] = ref(result_path)
        row['evidence'] = [ref(p) for p in sorted(directory.rglob('*'))
                           if p.is_file() and 'sandbox' not in p.relative_to(directory).parts]
        row.pop('fault', None)
        row['scenarioId'] = sid
        if result['status'] == 'passed':
            row['fault_source'] = None
        else:
            # Ask the same strict normalizer to prove the stage, rather than
            # duplicating its fatal-report precedence or guessing from duration.
            phases = []
            for phase in PHASES:
                row['fault_source'] = dict(phase=phase, checkpoint=failed, log=ref(log_path))
                try:
                    normalize_fault(row, result, output.parent)
                    phases.append(phase)
                except ValueError:
                    pass
            if len(phases) != 1:
                raise ValueError('unreviewed or ambiguous first fault for ' + sid)
            row['fault_source']['phase'] = phases[0]
    # Validate the complete matrix before replacing an existing output.
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent) as temporary:
        candidate = Path(temporary) / 'matrix.json'
        candidate.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        matrix(candidate)
        candidate.replace(output)
    return 0


def self_test():
    # Complete Result-v1-shaped synthetic matrices exercise hashing, normalization,
    # checkpoint chronology and CLI verdict exit codes without subprocess/game IO.
    with tempfile.TemporaryDirectory() as directory:
        base = Path(directory)
        def ref(name, value):
            path = base / name
            path.write_text(value if isinstance(value, str) else json.dumps(value), encoding='utf-8')
            return dict(path=name, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        def checkpoint(name, passed=True):
            return dict(id=name, status='passed' if passed else 'failed', startFrame=0, endFrame=1,
                        startTicks=0, endTicks=1, wallTimeMs=1, assertions=[], evidence=[],
                        error=None if passed else 'fault')
        def manifest(tag, status='passed', phase='shutdown', member='stopService()V', earlier=False):
            cases = []
            for i in range(9):
                result = dict(schema=1, scenarioId=str(i), status=status, survey=False, profile={},
                              checkpoints=[checkpoint('baseline', not earlier)],
                              firstFailure=None if status == 'passed' else 'OGPlay exited with status 1',
                              shutdown=dict(requested=True, clean=status == 'passed',
                                            error=None if status == 'passed' else 'OGPlay exited with status 1'))
                log = ref(f'{tag}-{i}.log', f'[owner] phase={phase}\nexception: Ljava/lang/UnsupportedOperationException;\n'
                          f'  message: unimplemented: Landroid/view/View;.{member}\n')
                cases.append(dict(result=ref(f'{tag}-{i}.json', result),
                                  inputs={k: {} for k in SAME}, implementation={k: {} for k in BUILD},
                                  evidence=[log], fault_source=None if status == 'passed' else
                                  dict(log=log, phase=phase, checkpoint='baseline' if earlier else None)))
            path = base / (tag + '.json')
            path.write_text(json.dumps(dict(schema=1, normalizer_version=VERSION, cases=cases)), encoding='utf-8')
            return matrix(path)
        passed = manifest('passed')
        failed = manifest('failed', 'failed')
        changed = manifest('changed', 'failed', member='requestFocus()Z')
        earlier = manifest('earlier', 'failed', phase='Activity', earlier=True)
        for baseline, candidate, verdict in [(passed, passed, 'pass'), (failed, failed, 'pass'),
                                              (failed, earlier, 'fail'), (failed, changed, 'needs_review'),
                                              (passed, failed, 'fail'), (failed, passed, 'pass')]:
            results = [compare(baseline[k], candidate[k]) for k in baseline]
            assert all(r['verdict'] == verdict for r in results), results
            assert (0 if all(r['verdict'] == 'pass' for r in results) else 1) == (0 if verdict == 'pass' else 1)
        for old, new, expected in [('passed', 'passed', 0), ('failed', 'failed', 0),
                                    ('failed', 'earlier', 1), ('failed', 'changed', 1)]:
            assert main(['--baseline', str(base / (old + '.json')), '--candidate',
                         str(base / (new + '.json')), '--output', str(base / 'comparison.json')]) == expected
        source = copy.deepcopy(failed['0'][0])
        source['fault_source']['log'] = ref('native.log', '[owner] phase=shutdown\nnative fault: read module=libguest.so relative_pc=0x001234')
        fault = normalize_fault(source, failed['0'][1], base)
        assert fault['signature']['relative_pc'] == '0x1234'
        source['fault_source']['log'] = ref('unknown.log', '[owner] phase=shutdown\nread pc=0x30881234')
        try:
            normalize_fault(source, failed['0'][1], base)
        except ValueError:
            pass
        else:
            raise AssertionError('bare PC admitted')
        source['fault_source']['log'] = ref('teardown-budget.log',
            'error [f=0] frontend.run_apk.teardown Android guest invocation failed: teardown cleanup wall-time budget exhausted\n')
        fault = normalize_fault(source, failed['0'][1], base)
        assert fault['signature']['category'] == 'teardown_budget'
        assert fault['signature']['requires_review'] is True
        source = copy.deepcopy(failed['0'][0])
        result = copy.deepcopy(failed['0'][1])
        source['fault_source'].update(phase='Activity', checkpoint=None)
        source['fault_source']['log'] = ref('startup.log', 'error [ogplay]\n  onCreate failed: uncaught Java exception\n'
            '    exception: Ljava/lang/UnsatisfiedLinkError;\n'
            '    message: intrinsic handler is not implemented: Landroid/view/View;.getViewTreeObserver()Landroid/view/ViewTreeObserver;\n')
        fault = normalize_fault(source, result, base)
        assert fault['phase_name'] == 'Activity' and fault['signature']['category'] == 'java'
        source['fault_source']['log'] = ref('startup-boundary.log', 'error [ogplay]\n'
            '  Activity startup failed: uncaught Java exception\n'
            '    guest boundary: Activity startup\n'
            '    exception: Ljava/lang/IllegalStateException;\n'
            '    message: Geometry requires a bound window root\n')
        boundary = normalize_fault(source, result, base)
        assert boundary['signature']['category'] == 'java_boundary'
        assert boundary['signature']['requires_review'] is True
        source['fault_source']['log'] = ref('host-stale.log',
            '[owner] phase=Activity\nerror [f=0] frontend.run_apk.fault UI node is stale\n')
        stale = normalize_fault(source, result, base)
        assert stale['signature'] == dict(category='host_ui_stale_node', boundary='frontend.run_apk.fault',
                                          message='UI node is stale', requires_review=True)
        cause_log = ('[owner] phase=Activity\nerror [f=1] frontend.run_apk.fault scheduled callback raised: '
            'Landroid/view/InflateException;: root inflate failed\n'
            '  at Landroid/view/LayoutInflater;.createView (pc 20)\n'
            'Caused by: Ljava/lang/reflect/InvocationTargetException;\n'
            'Caused by: Ljava/lang/UnsupportedOperationException;: scrollbar unavailable\n'
            'Caused by:   at Landroid/content/res/Resources$Theme;.obtainStyledAttributes (pc 23)\n')
        source['fault_source']['log'] = ref('cause.log', cause_log)
        cause_fault = normalize_fault(source, result, base)
        assert cause_fault['signature']['causes'][-1] == dict(
            exception='Ljava/lang/UnsupportedOperationException;', message='scrollbar unavailable',
            first_member='Landroid/content/res/Resources$Theme;.obtainStyledAttributes')
        source['fault_source']['log'] = ref('unknown-cause.log', cause_log + 'Caused by: [unknown target]\n')
        try:
            normalize_fault(source, result, base)
        except ValueError:
            pass
        else:
            raise AssertionError('unknown cause admitted')
        for text in ['error [ogplay]\n  unknown CLI failure\n',
                     'error [ogplay]\n  dexvm::VmJavaThrow\n'
                     '    exception: Ljava/lang/IllegalStateException;\n    message: unknown shape\n']:
            source['fault_source']['log'] = ref('unknown-startup.log', text)
            try:
                normalize_fault(source, result, base)
            except ValueError:
                pass
            else:
                raise AssertionError('unknown startup admitted')
        result['checkpoints'] = [checkpoint('baseline', False)]
        result['checkpoints'][0].update(error='checkpoint baseline wall-time budget exceeded', assertions=[
            dict(type='frame', passed=False, actual={'sequence': 0}, expected={'minimum_sequence': 1800}),
            dict(type='no_guest_fault', passed=True, actual={'guestFault': None}, expected={})])
        result['firstFailure'] = 'baseline: checkpoint baseline wall-time budget exceeded'
        result['shutdown']['error'] = 'OGPlay required forced process cleanup'
        source['fault_source'].update(phase='frame', checkpoint='baseline', log=ref('timeout.log', 'info window running\n'))
        fault = normalize_fault(source, result, base)
        assert fault['signature'] == dict(category='timeout', checkpoint='baseline', presented_frames=0)
        # Actual C shape: checkpoint timeout first, teardown timeout only later.
        result['checkpoints'][0].update(endFrame=36671, endTicks=36671000)
        result['checkpoints'][0]['assertions'][0]['actual']['sequence'] = 1
        source['fault_source']['log'] = ref('cleanup-timeout.log',
            'info window running\n[owner] phase=shutdown\nerror [f=0] frontend.run_apk.teardown Android guest invocation failed: '
            'teardown cleanup wall-time budget exhausted\n')
        fault = normalize_fault(source, result, base)
        assert fault['phase_name'] == 'frame'
        assert fault['signature'] == dict(category='timeout', checkpoint='baseline', presented_frames=1)
        # Freeze accepts either reviewed group from a preserved bundle matrix.
        for group, tag, status in [('B', 'freeze-pass', 'passed'), ('C', 'freeze-fail', 'failed')]:
            manifest(tag, status)
            document = read(base / (tag + '.json'))
            manifest_path = base / ('bundle-' + group + '.json')
            evidence = base / ('evidence-' + group)
            evidence.mkdir()
            for i, row in enumerate(document['cases']):
                row['implementation']['mode'] = 'JavaTraversal' if group == 'C' else 'Legacy'
                directory = evidence / str(i)
                directory.mkdir()
                (directory / 'result.json').write_bytes((base / row['result']['path']).read_bytes())
                text = (base / row['fault_source']['log']['path']).read_text() if row['fault_source'] else ''
                (directory / 'stderr.log').write_text(text)
            manifest_path.write_text(json.dumps(document))
            output = base / (group + '-frozen.json')
            assert main(['freeze', '--group', group, '--evidence-root', str(evidence),
                         '--bundle', str(manifest_path), '--output', str(output)]) == 0
            assert len(matrix(output)) == 9
            staged = base / ('staged-' + group)
            (staged / 'data/android/19/framework').mkdir(parents=True)
            (staged / 'ogplay-cli').write_bytes(b'preserved executable')
            entries = {'classes.dex': b'dex fixture', 'META-INF/ogplay/member-sources.json': b'{}',
                       'META-INF/ogplay/theme-projection.json': b'{}'}
            if group == 'C':
                pairing = dict(schema=1, mode='java-traversal', version=1,
                    dex_sha256=hashlib.sha256(entries['classes.dex']).hexdigest(),
                    member_sources_sha256=hashlib.sha256(entries['META-INF/ogplay/member-sources.json']).hexdigest(),
                    theme_projection_sha256=hashlib.sha256(entries['META-INF/ogplay/theme-projection.json']).hexdigest())
                entries['META-INF/ogplay/vm-pairing.json'] = json.dumps(pairing).encode()
            with zipfile.ZipFile(staged / 'data/android/19/framework/bootdex.jar', 'w') as archive:
                for name, value in entries.items():
                    archive.writestr(name, value)
            # Relative references belong to the preserved manifest, so the staged
            # sidecar uses explicit scenario IDs rather than stale relative paths.
            staged_doc = copy.deepcopy(document)
            for i, row in enumerate(staged_doc['cases']):
                row['scenarioId'] = str(i)
            (staged / 'comparison-manifest.json').write_text(json.dumps(staged_doc))
            staged_output = base / ('staged-' + group + '.json')
            assert main(['freeze', '--group', group, '--evidence-root', str(evidence),
                         '--bundle', str(staged), '--output', str(staged_output)]) == 0
            assert len(matrix(staged_output)) == 9
            # Wrong group never re-labels the archived executable/BootDex mode.
            try:
                freeze('B' if group == 'C' else 'C', evidence, staged, staged_output)
            except ValueError:
                pass
            else:
                raise AssertionError('bundle mode mismatch admitted')
        # Failed freeze leaves the prior output intact.
        prior = output.read_bytes()
        (evidence / '0' / 'stderr.log').write_text('unknown first fault')
        try:
            main(['freeze', '--group', 'C', '--evidence-root', str(evidence),
                  '--bundle', str(manifest_path), '--output', str(output)])
        except ValueError:
            pass
        else:
            raise AssertionError('unknown freeze admitted')
        assert output.read_bytes() == prior
        for malformed in ['error [ogplay]\n  unknown failure', 'native fault: read pc=0x1234',
                          'exception: Ljava/lang/Exception;\n']:
            source['fault_source']['log'] = ref('malformed.log', malformed)
            try:
                normalize_fault(source, result, base)
            except ValueError:
                pass
            else:
                raise AssertionError('unknown fault admitted as timeout')
    print('UI scenario comparison: synthetic Result v1 pass/same/earlier/changed and native normalization passed')
    return 0


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == 'freeze':
        p = argparse.ArgumentParser(description=freeze.__doc__)
        p.add_argument('--group', choices=('B', 'C'), required=True)
        p.add_argument('--evidence-root', type=Path, required=True)
        p.add_argument('--bundle', type=Path, required=True)
        p.add_argument('--output', type=Path, required=True)
        args = p.parse_args(argv[1:])
        return freeze(args.group, args.evidence_root.resolve(), args.bundle, args.output.resolve())
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-test', action='store_true')
    p.add_argument('--baseline', type=Path)
    p.add_argument('--candidate', type=Path)
    p.add_argument('--output', type=Path)
    args = p.parse_args(argv)
    if args.self_test:
        return self_test()
    if not all((args.baseline, args.candidate, args.output)):
        p.error('--baseline, --candidate and --output are required')
    output = args.output.resolve()
    a, b = matrix(args.baseline.resolve()), matrix(args.candidate.resolve())
    if a.keys() != b.keys():
        raise ValueError('scenario sets differ')
    results = {sid: compare(a[sid], b[sid]) for sid in sorted(a)}
    ok = all(x['verdict'] == 'pass' for x in results.values())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({'schema': 1, 'passed': ok, 'scenarios': results}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return 2 if any(x['verdict'] == 'invalid' for x in results.values()) else 0 if ok else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, KeyError, TypeError, OSError, zipfile.BadZipFile) as e:
        print(json.dumps({'status': 'invalid', 'reason': str(e)}, ensure_ascii=False))
        raise SystemExit(2)
