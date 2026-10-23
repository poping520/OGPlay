#!/usr/bin/env python3
"""Rule-based admission of pure-Java ROM classes into BootDex (ADR-0148).

The audited object is the rule set, not individual classes. A skeleton class from the four
pinned ROM jars is admitted when it has no native method, its superclass chain is Object /
an admitted class / an implemented intrinsic or BootDex parent (ADR-0156), its <clinit> transitive call closure
touches only implemented code, and no implemented intrinsic signature or C++ source names it.
Virtual/interface targets that are not uniquely determined are treated as possibly reaching
unimplemented code (conservative: reject rather than mistakenly admit).

Default output is a report only; --apply writes tools/bootdex/api19.json (then build/check).
"""
from __future__ import annotations
import argparse
import collections
import json
from pathlib import Path
import re
import subprocess
import struct
import zipfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_bootdex as builder
from api19_skeleton import unpack

JARS = ('core.jar', 'framework.jar', 'framework2.jar', 'ext.jar')
ACC_PRIVATE, ACC_STATIC, ACC_FINAL, ACC_NATIVE, ACC_ABSTRACT, ACC_INTERFACE = 0x2, 0x8, 0x10, 0x100, 0x400, 0x200
BINDER_NAMES = frozenset('Landroid/os/' + n + ';' for n in (
    'Binder', 'IBinder', 'IInterface', 'ServiceManager', 'ServiceManagerNative', 'Parcel', 'BinderProxy'))
BINDER_ROOTS = ('Landroid/os/IInterface;', 'Landroid/os/Binder;', 'Landroid/os/IBinder;')
DYNAMIC_TARGETS = frozenset((
    ('Ljava/lang/Class;', 'forName'), ('Ljava/lang/Class;', 'newInstance'),
    ('Ljava/lang/ClassLoader;', 'loadClass'), ('Ljava/lang/reflect/Method;', 'invoke'),
    ('Ljava/lang/reflect/Constructor;', 'newInstance'), ('Ljava/lang/Thread;', 'start')))
PRIORITY = ('native-method', 'parent-intrinsic-unimplemented-member', 'overrides-nonoverridable',
            'parent-not-admitted', 'missing-dependency', 'binder-in-clinit',
            'clinit-unimplemented-target', 'clinit-unimplemented-field', 'clinit-virtual-nonunique',
            'clinit-dynamic-dispatch', 'clinit-unresolved', 'requires-rejected-class')

CLASS_RE = re.compile(r'^\.class (.*?)(L[^;\s]+;)\s*$', re.M)
SUPER_RE = re.compile(r'^\.super (\S+)', re.M)
IMPL_RE = re.compile(r'^\.implements (\S+)', re.M)
METHOD_RE = re.compile(r'^\.method ([^\n]+)\n(.*?)^\.end method', re.M | re.S)
INVOKE_RE = re.compile(r'^(invoke-[\w-]+(?:/range)?)\s+\{([^}]*)\},\s*(\S+?)->([^(\s]+)(\(\S*)$')
FIELD_RE = re.compile(r'^([is](?:get|put)\S*)\s+(.*?),\s*(\[*L[^;\s]+;)->([^:\s]+):(\S+)$')
NEW_RE = re.compile(r'^new-instance\s+(\S+),\s*(L[^;\s]+;)$')
REF_RE = re.compile(r'^(const-class|check-cast|instance-of|new-array|filled-new-array(?:/range)?)\b.*?(\[*L[^;\s]+;)\s*$')
MOVE_OBJ_RE = re.compile(r'^move-object(?:/\w+)?\s+(\S+),\s*(\S+)$')
SIG_TYPE_RE = re.compile(r'L[^;\s()\[]+;')
SRC_TOKEN_RE = re.compile(r'[A-Za-z_][\w$]*(?:[/.][A-Za-z_][\w$]*)+')
NO_DEST = ('invoke', 'iput', 'sput', 'aput', 'return', 'if-', 'goto', 'throw', 'monitor', 'fill-array',
           'packed-switch', 'sparse-switch', 'nop', 'check-cast')


def array_base(desc: str) -> str:
    return desc.lstrip('[')


def parse_smali_class(text: str) -> dict:
    """Header, member flags and compact per-method op lists from baksmali output."""
    header = CLASS_RE.search(text)
    methods = {}
    for signature, body in METHOD_RE.findall(text):
        words = signature.split()
        name_desc = words[-1]
        name = name_desc[:name_desc.index('(')]
        methods[(name, name_desc[len(name):])] = dict(flags=set(words[:-1]), ops=parse_ops(body))
    return dict(desc=header.group(2), flags=set(header.group(1).split()),
                super=(SUPER_RE.search(text) or [None, None])[1],
                interfaces=IMPL_RE.findall(text), methods=methods,
                constant_fields=set(re.findall(r'^\.field .*?\b([\w$]+):\S+ = ', text, re.M)))


def parse_ops(body: str) -> list:
    ops = []
    for raw in body.splitlines():
        line = raw.strip()
        if not line or line.startswith(('.', '#')):
            continue
        if line.startswith(':'):
            ops.append(('label',))
            continue
        m = INVOKE_RE.match(line)
        if m:
            regs = [x.strip() for x in m.group(2).replace('..', ',').split(',') if x.strip()]
            ops.append(('invoke', m.group(1), regs[0] if regs else None, array_base(m.group(3)) if m.group(3).startswith('[') and m.group(4) != 'clone' else m.group(3), m.group(4), m.group(5)))
            continue
        m = FIELD_RE.match(line)
        if m:
            ops.append(('field', m.group(1), m.group(3), m.group(4)))
            continue
        m = NEW_RE.match(line)
        if m:
            ops.append(('new', m.group(1), m.group(2)))
            continue
        m = REF_RE.match(line)
        if m:
            ops.append(('ref', array_base(m.group(2))))
            continue
        m = MOVE_OBJ_RE.match(line)
        if m:
            ops.append(('move-object', m.group(1), m.group(2)))
            continue
        opcode, _, rest = line.partition(' ')
        if not opcode.startswith(NO_DEST):
            dest = rest.split(',')[0].strip()
            if re.fullmatch(r'[vp]\d+', dest):
                ops.append(('write', dest, opcode))
    return ops


def next_reg(reg: str) -> str:
    return reg[0] + str(int(reg[1:]) + 1)


class Analyzer:
    """Pure analysis over a runtime surface and ROM smali; no I/O."""

    def __init__(self, surface: list[dict], smali: dict[str, dict], jar_of: dict[str, str],
                 recipe_classes: set[str], guest: set[str], reserved: set[str],
                 source_tokens: set[str] | None = None, cha_precise: bool = False,
                 replacements: set[str] | None = None, native_bindings: set[tuple] | None = None,
                 boot_smali: dict[str, dict] | None = None):
        self.boot_smali = boot_smali or {}
        self.replacements = replacements or set()
        self.native_bindings = native_bindings or set()
        self.cls = {c['descriptor']: c for c in surface}
        self.smali, self.jar_of, self.cha_precise = smali, jar_of, cha_precise
        self.recipe_classes, self.guest, self.reserved = recipe_classes, guest, reserved
        self.source_tokens = source_tokens or set()
        self.meth = {d: {(m['name'], m['descriptor']): m for m in c['methods']} for d, c in self.cls.items()}
        self.fields = {d: {f['name']: f for f in c['fields']} for d, c in self.cls.items()}
        self.children = collections.defaultdict(list)
        for d, c in self.cls.items():
            for parent in ([c['superclass']] if c.get('superclass') else []) + list(c.get('interfaces', [])):
                self.children[parent].append(d)
        # Candidates: skeleton-owned classes that exist in the ROM jars.
        self.candidates = sorted(d for d, c in self.cls.items()
                                 if c['source'] == 'skeleton' and d in smali and d in jar_of)
        self.cand_set = set(self.candidates)
        self._summary_cache, self._subtype_cache, self._impl_cache = {}, {}, {}
        self.local_reasons: dict[str, list] = {}
        self.required: dict[str, set] = {}
        self.interaction = self._interaction_index()

    # ---- hierarchy helpers -------------------------------------------------
    def superchain(self, desc):
        seen = []
        while desc and desc in self.cls and desc not in seen:
            seen.append(desc)
            desc = self.cls[desc].get('superclass')
        return seen

    def ancestors(self, desc):
        out, stack = set(), [desc]
        while stack:
            d = stack.pop()
            if d in out or d not in self.cls:
                continue
            out.add(d)
            c = self.cls[d]
            stack.extend(([c['superclass']] if c.get('superclass') else []) + list(c.get('interfaces', [])))
        return out

    def subtypes(self, desc):
        if desc not in self._subtype_cache:
            out, stack = set(), [desc]
            while stack:
                d = stack.pop()
                if d in out:
                    continue
                out.add(d)
                stack.extend(self.children.get(d, ()))
            self._subtype_cache[desc] = out
        return self._subtype_cache[desc]

    def is_interface(self, desc):
        return bool(self.cls[desc]['access_flags'] & ACC_INTERFACE) if desc in self.cls else False

    # ---- member status -----------------------------------------------------
    def method_kind(self, owner, key):
        """candidate | bytecode | intrinsic | abstract | unimpl for an existing own method."""
        m = self.meth[owner][key]
        if (owner, *key) in self.native_bindings:
            return 'intrinsic'
        if owner in self.cand_set:
            flags = m['access_flags']
            return 'abstract' if flags & ACC_ABSTRACT else 'native' if flags & ACC_NATIVE else 'candidate'
        if m['status'] == 'implemented':
            if m['binding'] == 'abstract-contract' or m['access_flags'] & ACC_ABSTRACT:
                return 'abstract'
            return 'bytecode' if m['binding'] == 'bytecode' else 'intrinsic'
        return 'unimpl'

    def find_method(self, desc, key):
        for owner in self.superchain(desc):
            if key in self.meth.get(owner, {}):
                return owner
        stack, seen = [desc], set()
        while stack:
            d = stack.pop()
            if d in seen or d not in self.cls:
                continue
            seen.add(d)
            if key in self.meth.get(d, {}):
                return d
            stack.extend(self.cls[d].get('interfaces', []))
            if self.cls[d].get('superclass'):
                stack.append(self.cls[d]['superclass'])
        if self.is_interface(desc) and key in self.meth.get('Ljava/lang/Object;', {}):
            return 'Ljava/lang/Object;'
        return None

    def find_field(self, desc, name):
        for d in self.ancestors_ordered(desc):
            if name in self.fields.get(d, {}):
                return d
        return None

    def ancestors_ordered(self, desc):
        order = self.superchain(desc)
        rest = sorted(self.ancestors(desc) - set(order))
        return order + rest

    def body_ops(self, owner, key):
        """Bytecode available for traversal: candidates and BootDex bytecode of ROM classes."""
        if owner in self.guest:
            return None
        info = self.smali.get(owner)
        if info is None or owner not in self.cand_set and owner not in self.recipe_classes:
            return None
        m = info['methods'].get(key)
        return m['ops'] if m else None

    # ---- virtual target uniqueness (CHA) ------------------------------------
    def virtual_impls(self, static_type, key):
        cache_key = (static_type, key)
        if cache_key not in self._impl_cache:
            impls = set()
            for k in self.subtypes(static_type):
                c = self.cls.get(k)
                if not c or c['access_flags'] & (ACC_ABSTRACT | ACC_INTERFACE):
                    continue
                owner = self.find_method(k, key)
                if owner:
                    impls.add(owner)
            self._impl_cache[cache_key] = impls
        return self._impl_cache[cache_key]

    # ---- per-method summary -------------------------------------------------
    def summary(self, node):
        """(fails, required classes, callee nodes). Independent of the admitted set."""
        if node in self._summary_cache:
            return self._summary_cache[node]
        owner, name, desc = node
        fails, required, calls = [], set(), []
        ops = self.body_ops(owner, (name, desc)) or []
        exact = {}
        for op in ops:
            kind = op[0]
            if kind == 'label':
                exact.clear()
            elif kind == 'new':
                exact[op[1]] = op[2]
                self.trigger(op[2], fails, required, calls)
            elif kind == 'move-object':
                if op[2] in exact:
                    exact[op[1]] = exact[op[2]]
                else:
                    exact.pop(op[1], None)
            elif kind == 'write':
                wide = op[2].endswith('-wide') or op[2].startswith('const-wide') or '-to-long' in op[2] or '-to-double' in op[2] or op[2].endswith(('-long', '-double'))
                exact.pop(op[1], None)
                if wide:
                    exact.pop(next_reg(op[1]), None)
                    for reg in [r for r in exact if r[0] == 'p']:
                        del exact[reg]
            elif kind == 'ref':
                self.note_reference(op[1], fails)
            elif kind == 'field':
                self.field_access(op, fails, required, calls)
            elif kind == 'invoke':
                self.invoke(owner, op, exact, fails, required, calls)
        result = (fails, required, calls)
        self._summary_cache[node] = result
        return result

    def note_reference(self, desc, fails):
        if desc in BINDER_NAMES or self.ancestors(desc) & set(BINDER_ROOTS):
            fails.append(('binder-in-clinit', desc))

    def trigger(self, desc, fails, required, calls):
        """Class initialization: super chain <clinit> + class use."""
        self.note_reference(desc, fails)
        for d in self.superchain(desc):
            if d in self.cand_set:
                required.add(d)
            if self.body_ops(d, ('<clinit>', '()V')) is not None:
                calls.append((d, '<clinit>', '()V'))

    def field_access(self, op, fails, required, calls):
        _, _, cls, name = op
        cls = array_base(cls)
        self.note_reference(cls, fails)
        owner = self.find_field(cls, name)
        if owner is None:
            fails.append(('clinit-unresolved', cls + '->' + name))
            return
        field = self.fields[owner][name]
        if field.get('has_constant') and field.get('static'):
            return
        if owner in self.cand_set:
            required.add(owner)
            if field.get('static'):
                self.trigger(owner, fails, required, calls)
            return
        if field['status'] != 'implemented':
            fails.append(('clinit-unimplemented-field', owner + '->' + name))
        elif field.get('static'):
            self.trigger(owner, fails, required, calls)

    def invoke(self, current, op, exact, fails, required, calls):
        _, opcode, receiver, cls, name, desc = op
        key = (name, desc)
        self.note_reference(array_base(cls), fails)
        if (cls, name) in DYNAMIC_TARGETS:
            fails.append(('clinit-dynamic-dispatch', cls + '->' + name))
        base = opcode.split('/')[0]
        if base in ('invoke-virtual', 'invoke-interface'):
            static_type = array_base(cls)
            if receiver in exact:
                owners = {self.find_method(exact[receiver], key)}
            else:
                owners = self.virtual_impls(static_type, key)
            if None in owners or not owners:
                fails.append(('clinit-unresolved', f'{cls}->{name}{desc}'))
                return
            if len(owners) != 1 and not self.cha_precise:
                fails.append(('clinit-virtual-nonunique', f'{cls}->{name}{desc}'))
                return
            for owner in sorted(owners):
                self.reach_method(owner, key, fails, required, calls)
            return
        start = self.cls[current]['superclass'] if base == 'invoke-super' and current in self.cls else cls
        owner = self.find_method(array_base(start), key) if start in self.cls or array_base(start) in self.cls else None
        if owner is None:
            fails.append(('clinit-unresolved', f'{cls}->{name}{desc}'))
            return
        self.reach_method(owner, key, fails, required, calls)
        if base == 'invoke-static':
            self.trigger(owner, fails, required, calls)

    def reach_method(self, owner, key, fails, required, calls):
        kind = self.method_kind(owner, key)
        target = f'{owner}->{key[0]}{key[1]}'
        if kind == 'unimpl':
            fails.append(('clinit-unimplemented-target', target))
        elif kind == 'native':
            required.add(owner)
            fails.append(('clinit-unimplemented-target', target + ' (native)'))
        elif kind == 'candidate':
            required.add(owner)
            calls.append((owner, key[0], key[1]))
        elif kind == 'bytecode':
            if self.body_ops(owner, key) is not None:
                calls.append((owner, key[0], key[1]))
        elif kind == 'abstract':
            fails.append(('clinit-unresolved', target + ' (abstract)'))

    def closure(self, desc):
        """All failures and required candidate classes reachable from desc.<clinit>."""
        start = (desc, '<clinit>', '()V')
        if self.body_ops(desc, ('<clinit>', '()V')) is None:
            return [], set()
        fails, required, seen, stack = [], set(), set(), [start]
        while stack:
            node = stack.pop()
            if node in seen:
                continue
            seen.add(node)
            f, r, calls = self.summary(node)
            fails.extend(f)
            required |= r
            stack.extend(calls)
        return fails, required

    # ---- interaction with intrinsic code --------------------------------------
    def _interaction_index(self):
        """Class -> first implemented intrinsic-bound signature/field that mentions it."""
        index = {}
        for d, c in self.cls.items():
            for m in c['methods']:
                if m['status'] == 'implemented' and m['binding'] == 'intrinsic':
                    for t in SIG_TYPE_RE.findall(m['descriptor']):
                        index.setdefault(t, f"{d}->{m['name']}{m['descriptor']}")
            if c['source'] == 'intrinsic':
                for f in c['fields']:
                    if f['status'] == 'implemented':
                        for t in SIG_TYPE_RE.findall(f['descriptor']):
                            index.setdefault(t, f"{d}.{f['name']}")
        return index

    def interaction_reason(self, desc):
        # ADR-0154: these interactions require manual removal/review in the same batch.
        if desc in self.replacements:
            return None
        if desc in self.interaction:
            return 'intrinsic-signature: ' + self.interaction[desc]
        inner = desc[1:-1]
        if inner in self.source_tokens or 'L' + inner in self.source_tokens or inner.replace('/', '.') in self.source_tokens:
            return 'intrinsic-source-reference'
        return None

    # ---- admission -------------------------------------------------------------
    def parent_member_reasons(self, desc):
        """ADR-0156: audit every own body against the implemented parent surface."""
        parents = self.superchain(desc)[1:]
        protected = {d for d in parents if self.cls[d]['source'] in ('intrinsic', 'bootdex')}
        reasons = set()
        for key, method in self.smali[desc]['methods'].items():
            if key[0] not in ('<init>', '<clinit>') and not method['flags'] & {'static', 'private'}:
                for parent in parents:
                    inherited = self.meth.get(parent, {}).get(key)
                    # Bytecode uses ROM flags; the exported overridable bit is an intrinsic contract.
                    if inherited and not inherited['access_flags'] & (ACC_STATIC | ACC_PRIVATE) and (
                            inherited['binding'] == 'intrinsic' and inherited.get('overridable') is False or
                            inherited['access_flags'] & ACC_FINAL):
                        reasons.add(('overrides-nonoverridable', parent + '->' + ''.join(key)))
            for op in method['ops']:
                if op[0] == 'invoke':
                    owner, name, signature = op[3:]
                    if name == '<init>':
                        resolved = owner if (name, signature) in self.meth.get(owner, {}) else None
                    else:
                        resolved = self.find_method(owner, (name, signature))
                    member = self.meth.get(resolved, {}).get((name, signature))
                    target = owner + '->' + name + signature
                elif op[0] == 'field':
                    owner, name = op[2:]
                    resolved = self.find_field(owner, name)
                    member = self.fields.get(resolved, {}).get(name)
                    target = owner + '->' + name
                else:
                    continue
                if (resolved in protected or resolved is None and owner in protected) and (
                        member is None or member['status'] != 'implemented'):
                    reasons.add(('parent-intrinsic-unimplemented-member', target))
        return sorted(reasons)

    def analyze(self):
        self.interacting = {}
        for d in self.candidates:
            reasons = []
            flags = self.cls[d]['methods']
            if any(m['access_flags'] & ACC_NATIVE and
                   (d, m['name'], m['descriptor']) not in self.native_bindings for m in flags):
                reasons.append(('native-method', ', '.join(sorted(m['name'] for m in flags if m['access_flags'] & ACC_NATIVE))))
            c = self.cls[d]
            for ref in [c.get('superclass')] + list(c.get('interfaces', [])):
                if ref is not None and (ref not in self.cls or self.cls[ref].get('link_error')):
                    reasons.append(('missing-dependency', ref))
            for t in sorted({t for m in c['methods'] for t in SIG_TYPE_RE.findall(m['descriptor'])} |
                            {t for f in c['fields'] for t in SIG_TYPE_RE.findall(f['descriptor'])}):
                if t not in self.cls and t != d:
                    reasons.append(('missing-dependency', t))
                    break
            reasons.extend(self.parent_member_reasons(d))
            fails, required = self.closure(d)
            reasons.extend(fails)
            self.required[d] = required - {d}
            self.local_reasons[d] = reasons
            why = self.interaction_reason(d)
            if why:
                self.interacting[d] = why
        admitted = {d for d in self.candidates if not self.local_reasons[d] and d not in self.interacting}
        parent_problem = {}
        changed = True
        while changed:
            changed = False
            for d in sorted(admitted):
                problem = None
                for parent in self.superchain(d)[1:]:
                    if parent == 'Ljava/lang/Object;' or parent in admitted:
                        continue
                    if parent in self.cand_set:
                        problem = ('parent-not-admitted', parent + ('' if parent not in self.interacting else ' [interaction]'))
                    elif (self.cls.get(parent, {}).get('source') not in ('intrinsic', 'bootdex') or
                          any(p in BINDER_NAMES for p in self.superchain(d)[1:])):
                        problem = ('parent-not-admitted', parent + f" [{self.cls.get(parent, {}).get('source', 'absent')}]")
                    if problem:
                        break
                if problem is None:
                    bad = sorted(r for r in self.required[d] if r not in admitted)
                    if bad:
                        problem = ('requires-rejected-class', bad[0])
                if problem:
                    parent_problem[d] = problem
                    admitted.discard(d)
                    changed = True
        self.admitted = admitted
        self.rejected = {}
        for d in self.candidates:
            if d in admitted:
                continue
            reasons = list(self.local_reasons[d])
            if d in parent_problem:
                reasons.append(parent_problem[d])
            self.rejected[d] = reasons
        # Parent walk for locally rejected classes is irrelevant: they are rejected anyway.
        return admitted

    def closure_of(self, seeds):
        """Seeds plus admitted superclasses and <clinit>-required classes (faithful hierarchy/init)."""
        out, stack = set(), list(seeds)
        while stack:
            d = stack.pop()
            if d in out or d not in self.admitted:
                continue
            out.add(d)
            stack.extend(self.superchain(d)[1:])
            stack.extend(self.required.get(d, ()))
        return out

    def reachable_closure(self):
        """ADR-0155: ROM body reachability from recipe and intrinsic declarations.

        Rejected classes are boundaries, not bridges into otherwise unreachable code.
        Each admitted dependency contributes all its bodies, parents and init closure.
        Existing BootDex seeds must be traversed even though they are not candidates.
        """
        seeds = set(self.recipe_classes) | set(self.boot_smali)
        for d, c in self.cls.items():
            if c['source'] != 'intrinsic':
                continue
            seeds.update(c.get('interfaces', []))
            if c.get('superclass'):
                seeds.add(c['superclass'])
            for member in c['methods'] + c['fields']:
                seeds.update(SIG_TYPE_RE.findall(member['descriptor']))
        selected, seen, stack = set(), set(), list(seeds)
        while stack:
            d = stack.pop()
            if d in seen:
                continue
            seen.add(d)
            if d in self.admitted:
                dependencies = self.closure_of({d})
                selected.update(dependencies)
                stack.extend(dependencies - {d})
            elif d not in self.recipe_classes and d not in self.boot_smali:
                continue
            body = self.boot_smali.get(d, self.smali.get(d, {}))
            for method in body.get('methods', {}).values():
                for op in method['ops']:
                    if op[0] == 'new':
                        stack.append(op[2])
                    elif op[0] == 'invoke':
                        stack.append(array_base(op[3]))
                    elif op[0] == 'field':
                        stack.append(array_base(op[2]))
                    elif op[0] == 'ref':
                        stack.append(op[1])
        return {d for d in selected if self.cls[d]['source'] == 'skeleton'}

    def primary(self, d):
        if d in self.interacting and not self.local_reasons[d]:
            return 'intrinsic-interaction'
        reasons = self.rejected[d]
        return min((r[0] for r in reasons), key=lambda r: PRIORITY.index(r) if r in PRIORITY else 99)

    def report_tables(self):
        by_primary = collections.Counter(self.primary(d) for d in self.rejected)
        detail = collections.Counter()
        for d, reasons in self.rejected.items():
            key = self.primary(d)
            first = next((r for r in reasons if r[0] == key), None)
            detail[(key, first[1] if first else self.interacting.get(d, ''))] += 1
        return by_primary, detail


# ---- I/O drivers ----------------------------------------------------------------------
def disassemble(cache: Path) -> dict[str, dict]:
    """Baksmali the four ROM jars (cached) and return {descriptor: (jar, smali text)}."""
    texts = {}
    for jar in JARS:
        path = builder.source_path(jar)
        digest = builder.file_sha256(path)
        dest = cache / (jar + '-' + digest[:16])
        stamp = dest / '.complete'
        if not stamp.exists():
            dest.mkdir(parents=True, exist_ok=True)
            subprocess.run([str(builder.JAVA), '-jar', str(builder.SMALI / 'baksmali.jar'), 'disassemble', '--api', '19',
                            '--jobs', '1', '--output', str(dest), str(path)], check=True)
            stamp.write_text(digest, encoding='utf-8')
        for smali in dest.rglob('*.smali'):
            owner = 'L' + smali.relative_to(dest).as_posix()[:-6] + ';'
            if owner in texts:
                texts[owner] = (None, None)  # duplicate ROM class: never a candidate
            else:
                texts[owner] = (jar, smali)
    return texts


def disassemble_bootdex(cache: Path, surface: list[dict]) -> dict[str, dict]:
    """Traverse the actual payload too: ROM overrides, Conscrypt and guest Java.

    Keep this separate from the unchanged ADR-0148 initialization admission audit.
    """
    digest = builder.file_sha256(builder.OUTPUT)
    dest = cache / ('bootdex-' + digest[:16])
    stamp = dest / '.complete'
    if not stamp.exists():
        dest.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(builder.JAVA), '-jar', str(builder.SMALI / 'baksmali.jar'),
                        'disassemble', '--api', '19', '--jobs', '1', '--output', str(dest),
                        str(builder.OUTPUT)], check=True)
        stamp.write_text(digest, encoding='utf-8')
    out = {}
    for c in surface:
        if c['source'] != 'bootdex':
            continue
        d = c['descriptor']
        path = dest / (d[1:-1] + '.smali')
        if not path.is_file():
            raise ValueError('BootDex surface/payload mismatch: ' + d)
        out[d] = parse_smali_class(path.read_text(encoding='utf-8'))
    return out


def guest_descriptors() -> set[str]:
    names = []
    for group in ('JAVA_SOURCE_NAMES', 'MATH_JAVA_NAMES', 'ZIP_JAVA_NAMES', 'IO_JAVA_NAMES', 'FRAMEWORK_JAVA_NAMES'):
        names.extend(getattr(builder, group))
    return {'L' + n[:-5] + ';' for n in names}


def source_tokens(root: Path) -> set[str]:
    tokens = set()
    for sub in ('src', 'include'):
        for path in (root / sub).rglob('*'):
            if path.suffix in ('.cpp', '.h', '.hpp', '.cc', '.inc', '.def', '.mm') and path.is_file():
                tokens.update(SRC_TOKEN_RE.findall(path.read_text(encoding='utf-8', errors='replace')))
    return tokens


def frequencies(scan_dir: Path, surface_sha: str) -> dict:
    summary = json.loads((scan_dir / 'summary.json').read_text(encoding='utf-8'))
    if summary['errors'] or summary['incomplete'] or not summary['scanned'] or summary['scanned'] != summary['apks']:
        raise ValueError('complete sample library scan required')
    classes, members, count = collections.Counter(), collections.defaultdict(set), 0
    for path in scan_dir.glob('*.json'):
        if path.name == 'summary.json':
            continue
        scan = json.loads(path.read_text(encoding='utf-8'))
        if not scan.get('complete') or scan.get('surface_sha256') != surface_sha:
            raise ValueError('incomplete or stale APK scan: ' + str(path))
        count += 1
        owners = set()
        for gap in scan['gaps']:
            if gap['classification'] == 'declared-unimplemented':
                owners.add(gap['owner'])
                members[(gap['owner'], gap['kind'], gap['member'], gap['descriptor'])].add(path.name)
        classes.update(owners)
    if count != summary['scanned']:
        raise ValueError('sample scan count mismatch')
    return dict(apks=count, classes=classes, members=members)


def replacement_surface(surface, rom, replacements):
    """Use pinned ROM members, retaining only previously reviewed native endpoints.

    Source/signature coupling is an explicit migration obligation, not proof of safety.
    No replacement request can authorize a new native binding or a frozen date class.
    """
    by = {c['descriptor']: c for c in surface}
    bindings = set()
    for d in replacements:
        if d not in by or by[d]['source'] != 'intrinsic' or d not in rom:
            raise ValueError('replacement must be a current intrinsic in pinned ROM: ' + d)
        old = by[d]
        parent, interfaces, flags, methods, fields = rom[d]
        existing = {(m['name'], m['descriptor']) for m in old['methods']
                    if m['status'] == 'implemented' and m['binding'] == 'intrinsic'
                    and m.get('origin') == 'handwritten'}
        bindings.update((d, n, t) for n, t, f in methods if f & ACC_NATIVE and (n, t) in existing)
        by[d] = dict(descriptor=d, source='skeleton', superclass=parent, interfaces=interfaces,
                     access_flags=flags, link_error='',
                     methods=[dict(name=n, descriptor=t, access_flags=f,
                                   status='implemented' if f & ACC_ABSTRACT else 'unimplemented-ledger',
                                   binding='abstract-contract' if f & ACC_ABSTRACT else 'intrinsic')
                              for n, t, f in methods],
                     fields=[dict(name=n, descriptor=t, access_flags=f, has_constant=bool(k),
                                  status='implemented' if k else 'unimplemented-ledger')
                             for n, t, f, k, v in fields])
    return list(by.values()), bindings


def run(args) -> int:
    recipe = json.loads(builder.RECIPE.read_text(encoding='utf-8'))
    builder.verify_inputs(builder.load_recipe(recipe))
    surface = json.loads(args.surface.read_text(encoding='utf-8'))['classes']
    replacements = set(args.replace_intrinsic or ())
    native_bindings = set()
    if replacements:
        surface, native_bindings = replacement_surface(
            surface, unpack((builder.ROOT / 'data/dexvm/api19-skeleton.bin').read_bytes()), replacements)
    # Frozen DVM-102 audit: every class it classifies as something other than boot_dex stays out.
    reserved = {d for cat, values in recipe['date_family_audit']['classifications'].items()
                if cat != 'boot_dex' for d in values}
    texts = disassemble(args.cache)
    surface_by = {c['descriptor']: c for c in surface}
    smali, jar_of = {}, {}
    for d, c in surface_by.items():
        # Only parse bodies we may traverse: skeleton candidates and ROM BootDex classes.
        if d in texts and texts[d][0] and (c['source'] == 'skeleton' or d in {x for v in recipe['classes'].values() for x in v}):
            smali[d] = parse_smali_class(texts[d][1].read_text(encoding='utf-8'))
            jar_of[d] = texts[d][0]
    # Duplicate-ROM classes lose candidacy.
    for d, (jar, _) in texts.items():
        if jar is None:
            jar_of.pop(d, None)
    if replacements & reserved:
        raise ValueError('replacement conflicts with frozen date audit: ' + str(sorted(replacements & reserved)))
    recipe_classes = {x for v in recipe['classes'].values() for x in v}
    analyzer = Analyzer(surface, smali, jar_of, recipe_classes, guest_descriptors(), reserved,
                        source_tokens(builder.ROOT), cha_precise=args.cha_precise,
                        replacements=replacements, native_bindings=native_bindings,
                        boot_smali=disassemble_bootdex(args.cache, surface))
    for d in sorted(reserved & set(analyzer.candidates)):
        analyzer.cand_set.discard(d)
    analyzer.candidates = [d for d in analyzer.candidates if d not in reserved]
    admitted = analyzer.analyze()
    freq = frequencies(args.scan_dir, builder.file_sha256(args.surface)) if args.scan_dir else None
    by_primary, detail = analyzer.report_tables()
    skeleton_total = sum(1 for c in surface if c['source'] == 'skeleton')
    owner_runtime = collections.Counter(c['source'] for c in surface)
    def estimate_for(scope: set) -> collections.Counter:
        # declared-unimplemented rows whose defining class (owner or inherited) enters BootDex;
        # abstract contract methods stop being "unimplemented" but are not original bodies.
        out = collections.Counter()
        for (owner, kind, member, descriptor) in freq['members']:
            if kind == 'method':
                key = (member, descriptor)
                definer = analyzer.find_method(owner, key)
                if definer in scope:
                    abstract = analyzer.meth[definer][key]['access_flags'] & ACC_ABSTRACT
                    out['methods_abstract' if abstract else 'methods'] += 1
            elif kind == 'field':
                definer = analyzer.find_field(owner, member)
                if definer in scope:
                    out['fields'] += 1
        return out
    estimate = estimate_for(admitted) if freq else collections.Counter()
    ranked = sorted(admitted, key=lambda d: (-(freq['classes'][d] if freq else 0), d))
    report = dict(
        schema=1, policy='ADR-0154 explicit replacement' if replacements else 'ADR-0148 rule-based admission',
        replacements=sorted(replacements), retained_native_bindings=sorted(native_bindings),
        runtime_classes=dict(owner_runtime), skeleton_classes=skeleton_total,
        candidates=len(analyzer.candidates), admitted=len(admitted), rejected=len(analyzer.rejected),
        rejection_by_primary=dict(by_primary.most_common()),
        rejection_top20=[dict(reason=k[0], detail=k[1], classes=n) for k, n in detail.most_common(20)],
        estimate_declared_unimplemented_to_original=dict(estimate), estimate_total=sum(estimate.values()),
        scope=args.scope, admitted_ranked=[dict(owner=d, jar=jar_of[d], apks=(freq['classes'][d] if freq else None)) for d in ranked],
        interaction_list=[dict(owner=d, why=w, otherwise_admissible=not analyzer.local_reasons[d])
                          for d, w in sorted(analyzer.interacting.items())],
        rejected_detail={d: [list(r) for r in reasons[:5]] for d, reasons in sorted(analyzer.rejected.items())})
    chosen = sorted(admitted)
    closure_missing = sorted(analyzer.reachable_closure())
    report['closure_missing'] = closure_missing
    report['closure_missing_count'] = len(closure_missing)
    if args.scope == 'closure':
        chosen = closure_missing
    if args.scope == 'referenced':
        if not freq:
            raise ValueError('--scope referenced requires --scan-dir')
        chosen = sorted(analyzer.closure_of({d for d in admitted if freq['classes'][d]}))
    report['selected'] = len(chosen)
    report['selected_classes'] = chosen
    if freq:
        selected_estimate = estimate_for(set(chosen))
        report['estimate_selected'] = dict(selected_estimate)
        report['estimate_selected_total'] = sum(selected_estimate.values())
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=True, indent=1) + '\n', encoding='utf-8')
    requested = replacements or set(args.only or ())
    if requested:
        missing = [d for d in sorted(requested) if d not in admitted]
        for d in missing:
            print(f'REJECTED {d}: {analyzer.rejected.get(d, "not a candidate")}', file=sys.stderr)
        if missing:
            return 2
        chosen = sorted(analyzer.closure_of(requested)) if replacements else sorted(args.only)
    report['selected'] = len(chosen)
    report['selected_classes'] = chosen
    args.report.write_text(json.dumps(report, ensure_ascii=True, indent=1) + '\n', encoding='utf-8')
    if args.check_closure:
        with zipfile.ZipFile(builder.OUTPUT) as archive:
            dex = archive.read('classes.dex')
        indices = dict(method_ids=struct.unpack_from('<I', dex, 88)[0],
                       field_ids=struct.unpack_from('<I', dex, 80)[0])
        report['dex_indices'] = indices
        args.report.write_text(json.dumps(report, ensure_ascii=True, indent=1) + '\n', encoding='utf-8')
        over_budget = any(n > 60000 for n in indices.values())
        if over_budget:
            print('single DEX index budget exceeded (60000): ' + str(indices), file=sys.stderr)
        for d in closure_missing:
            print('CLOSURE GAP ' + d, file=sys.stderr)
        print(f'closure gaps: {len(closure_missing)}')
        return 1 if closure_missing or over_budget else 0
    if args.apply:
        for d in chosen:
            group = recipe['classes'].setdefault(jar_of[d], [])
            if d not in group:
                group.append(d)
                group.sort()
        builder.RECIPE.write_text(json.dumps(recipe, ensure_ascii=True, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(candidates=len(analyzer.candidates), admitted=len(admitted), rejected=len(analyzer.rejected),
                          interaction=len(analyzer.interacting), by_primary=dict(by_primary.most_common()),
                          estimate=report['estimate_total'], report=str(args.report),
                          selected=len(chosen), applied=len(chosen) if args.apply else 0)))
    return 0


# ---- self-test ------------------------------------------------------------------------
def _method(name, desc, flags=1, status='unimplemented-ledger', binding='intrinsic'):
    return dict(name=name, descriptor=desc, static=bool(flags & ACC_STATIC), access_flags=flags, status=status, binding=binding)


def _surface(desc, source, super_='Ljava/lang/Object;', methods=(), fields=(), interfaces=(), flags=1):
    return dict(descriptor=desc, source=source, link_error='', access_flags=flags, superclass=super_,
                interfaces=list(interfaces), methods=list(methods), fields=list(fields))


def _smali(desc, body='', super_='Ljava/lang/Object;', methods=''):
    return f'.class public {desc}\n.super {super_}\n{methods}\n'


def self_test():
    obj = 'Ljava/lang/Object;'
    impl = lambda n, d, f=1: _method(n, d, f, 'implemented', 'intrinsic')
    unimpl = lambda n, d, f=1: _method(n, d, f)
    skel = lambda n, d, f=1: _method(n, d, f)
    clinit = lambda ops: '.method static constructor <clinit>()V\n .registers 2\n' + ops + ' return-void\n.end method\n'
    ctor = '.method public constructor <init>()V\n .registers 1\n invoke-direct {p0}, Ljava/lang/Object;-><init>()V\n return-void\n.end method\n'
    classes, smali = [], {}

    def add(desc, source, methods=(), super_=obj, fields=(), clinit_ops=None, extra='', interfaces=(), flags=1):
        classes.append(_surface(desc, source, super_, methods, fields, interfaces, flags))
        if source == 'skeleton':
            smali[desc] = parse_smali_class(_smali(desc, super_=super_, methods=(clinit(clinit_ops) if clinit_ops is not None else '') + ctor + extra))
    add(obj, 'intrinsic', [impl('<init>', '()V'), impl('toString', '()Ljava/lang/String;')], super_=None)
    add('Ljava/lang/String;', 'intrinsic', [impl('length', '()I')])
    add('Landroid/os/Binder;', 'bootdex', [_method('<init>', '()V', 1, 'implemented', 'bytecode')])
    add('Landroid/os/Handler;', 'intrinsic', [impl('<init>', '()V'), unimpl('post', '(Ljava/lang/Runnable;)Z'),
                                              impl('consume', '(Ltest/Passed;)V')])
    # --- positive cases
    add('Ltest/Plain;', 'skeleton', [skel('<init>', '()V'), skel('f', '()V')], clinit_ops='', flags=1)
    add('Ltest/Const;', 'skeleton', [skel('<init>', '()V'), skel('<clinit>', '()V', 8)], clinit_ops=' new-instance v0, Ljava/lang/Object;\n invoke-direct {v0}, Ljava/lang/Object;-><init>()V\n')
    add('Ltest/ExactType;', 'skeleton', [skel('<init>', '()V'), skel('<clinit>', '()V', 8)],
        clinit_ops=' new-instance v0, Ltest/Base;\n invoke-direct {v0}, Ltest/Base;-><init>()V\n invoke-virtual {v0}, Ltest/Base;->f()V\n')
    add('Ltest/Base;', 'skeleton', [skel('<init>', '()V'), skel('f', '()V')], extra='.method public f()V\n return-void\n.end method\n')
    add('Ltest/Cycle1;', 'skeleton', [skel('<init>', '()V'), skel('<clinit>', '()V', 8), skel('g', '()V', 9)],
        clinit_ops=' invoke-static {}, Ltest/Cycle2;->g()V\n', extra='.method public static g()V\n return-void\n.end method\n')
    add('Ltest/Cycle2;', 'skeleton', [skel('<init>', '()V'), skel('<clinit>', '()V', 8), skel('g', '()V', 9)],
        clinit_ops=' invoke-static {}, Ltest/Cycle1;->g()V\n', extra='.method public static g()V\n return-void\n.end method\n')
    # --- parent admission and rejections: native, unimplemented target, Binder, virtual chain
    add('Ltest/Native;', 'skeleton', [skel('<init>', '()V'), skel('n', '()V', 0x101)])
    add('Ltest/ExtendsIntrinsic;', 'skeleton', [skel('<init>', '()V')], super_='Landroid/os/Handler;')
    smali['Ltest/ExtendsIntrinsic;'] = parse_smali_class(_smali(
        'Ltest/ExtendsIntrinsic;', super_='Landroid/os/Handler;',
        methods=ctor.replace('Ljava/lang/Object;', 'Landroid/os/Handler;')))
    add('Ltest/CallsUnimpl;', 'skeleton', [skel('<init>', '()V'), skel('<clinit>', '()V', 8)],
        clinit_ops=' new-instance v0, Landroid/os/Handler;\n invoke-direct {v0}, Landroid/os/Handler;-><init>()V\n invoke-virtual {v0, v1}, Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z\n')
    add('Ltest/TouchesBinder;', 'skeleton', [skel('<init>', '()V'), skel('<clinit>', '()V', 8)],
        clinit_ops=' new-instance v0, Landroid/os/Binder;\n invoke-direct {v0}, Landroid/os/Binder;-><init>()V\n')
    # <clinit> -> static helper -> virtual call on a type with a subclass whose override is unimplemented
    add('Ltest/Shape;', 'skeleton', [skel('<init>', '()V'), skel('area', '()I', 0x401)], flags=0x401,
        extra='.method public abstract area()I\n.end method\n')
    add('Ltest/Square;', 'skeleton', [skel('<init>', '()V'), skel('area', '()I')], super_='Ltest/Shape;',
        extra='.method public area()I\n const/4 v0, 0x1\n return v0\n.end method\n')
    add('Ltest/Circle;', 'skeleton', [skel('<init>', '()V'), skel('area', '()I')], super_='Ltest/Shape;',
        extra='.method public area()I\n const/4 v0, 0x2\n return v0\n.end method\n')
    add('Ltest/Helper;', 'skeleton', [skel('<init>', '()V'), skel('run', '(Ltest/Shape;)I', 9)],
        extra='.method public static run(Ltest/Shape;)I\n .registers 2\n invoke-virtual {p0}, Ltest/Shape;->area()I\n move-result v0\n return v0\n.end method\n')
    add('Ltest/ViaVirtualChain;', 'skeleton', [skel('<init>', '()V'), skel('<clinit>', '()V', 8)],
        clinit_ops=' const/4 v0, 0x0\n invoke-static {v0}, Ltest/Helper;->run(Ltest/Shape;)I\n')
    # unique-by-CHA target admitted; same shape but a second concrete override makes it non-unique
    add('Ltest/Solo;', 'skeleton', [skel('<init>', '()V'), skel('name', '()I')],
        extra='.method public name()I\n const/4 v0, 0x1\n return v0\n.end method\n')
    add('Ltest/SoloHelper;', 'skeleton', [skel('<init>', '()V'), skel('run', '(Ltest/Solo;)I', 9)],
        extra='.method public static run(Ltest/Solo;)I\n .registers 2\n invoke-virtual {p0}, Ltest/Solo;->name()I\n move-result v0\n return v0\n.end method\n')
    add('Ltest/ViaUniqueChain;', 'skeleton', [skel('<init>', '()V'), skel('<clinit>', '()V', 8)],
        clinit_ops=' const/4 v0, 0x0\n invoke-static {v0}, Ltest/SoloHelper;->run(Ltest/Solo;)I\n')
    # parent cascade, interaction, existing
    add('Ltest/Child;', 'skeleton', [skel('<init>', '()V')], super_='Ltest/Native;')
    add('Ltest/Passed;', 'skeleton', [skel('<init>', '()V')])
    add('Ltest/Existing;', 'bootdex', [_method('<init>', '()V', 1, 'implemented', 'bytecode')])
    for c in classes:
        if c['descriptor'] in smali:
            continue
    jar_of = {d: 'core.jar' for d in smali}
    analyzer = Analyzer(classes, smali, jar_of, set(), set(), set(), source_tokens={'test/FromCpp'})
    admitted = analyzer.analyze()
    ok = {'Ltest/Plain;', 'Ltest/Const;', 'Ltest/ExactType;', 'Ltest/Base;', 'Ltest/Cycle1;', 'Ltest/Cycle2;',
          'Ltest/Shape;', 'Ltest/Square;', 'Ltest/Circle;', 'Ltest/Helper;', 'Ltest/Solo;', 'Ltest/SoloHelper;',
          'Ltest/ViaUniqueChain;', 'Ltest/ExtendsIntrinsic;'}
    assert admitted == ok, (sorted(admitted ^ ok), analyzer.rejected)
    reasons = lambda d: [r[0] for r in analyzer.rejected[d]]
    assert 'native-method' in reasons('Ltest/Native;')
    assert 'Ltest/ExtendsIntrinsic;' in admitted
    assert 'clinit-unimplemented-target' in reasons('Ltest/CallsUnimpl;')
    assert 'binder-in-clinit' in reasons('Ltest/TouchesBinder;')
    # Indirect virtual chain: Shape.area has two concrete implementations -> possibly unimplemented.
    assert reasons('Ltest/ViaVirtualChain;') == ['clinit-virtual-nonunique'], analyzer.rejected['Ltest/ViaVirtualChain;']
    assert reasons('Ltest/Child;') == ['parent-not-admitted'] or 'parent-not-admitted' in reasons('Ltest/Child;')
    assert analyzer.primary('Ltest/Passed;') == 'intrinsic-interaction' and 'Ltest/Passed;' in analyzer.interacting
    assert 'Ltest/Existing;' not in analyzer.rejected and 'Ltest/Existing;' not in admitted
    # ADR-0156: all own bodies, inherited resolution, final methods and overlays.
    add('Ltest/BadParentCall;', 'skeleton', super_='Landroid/os/Handler;',
        extra='.method public f()V\n invoke-virtual {p0, v0}, Ltest/BadParentCall;->post(Ljava/lang/Runnable;)Z\n return-void\n.end method\n')
    final_method = impl('fixed', '()V', ACC_FINAL | 1)
    final_method['overridable'] = False
    add('Ltest/Overlay;', 'bootdex', [impl('<init>', '()V'), final_method],
        fields=[dict(name='missing', descriptor='I', status='unimplemented-ledger')])
    add('Ltest/OverlayChild;', 'skeleton', super_='Ltest/Overlay;')
    add('Ltest/BadOverride;', 'skeleton', super_='Ltest/Overlay;',
        extra='.method public fixed()V\n return-void\n.end method\n')
    add('Ltest/BadParentField;', 'skeleton', super_='Ltest/Overlay;',
        extra='.method public f()V\n iget v0, p0, Ltest/BadParentField;->missing:I\n return-void\n.end method\n')
    bytecode = _method('ordinary', '()V', 1, 'implemented', 'bytecode')
    bytecode['overridable'] = False  # Exported bit is not used for bytecode methods.
    add('Ltest/BytecodeParent;', 'bootdex', [bytecode])
    add('Ltest/BytecodeChild;', 'skeleton', super_='Ltest/BytecodeParent;',
        extra='.method public ordinary()V\n return-void\n.end method\n')
    add('Ltest/BadParentConstructor;', 'skeleton', super_='Landroid/os/Handler;',
        extra='.method public constructor <init>(I)V\n invoke-direct {p0, p1}, Landroid/os/Handler;-><init>(I)V\n return-void\n.end method\n')
    add('Ltest/BinderChild;', 'skeleton', super_='Landroid/os/Binder;')
    jar_of.update({d: 'core.jar' for d in smali})
    parent_audit = Analyzer(classes, smali, jar_of, set(), set(), set())
    parent_audit.analyze()
    assert {'Ltest/ExtendsIntrinsic;', 'Ltest/OverlayChild;', 'Ltest/BytecodeChild;'} <= parent_audit.admitted
    for d in ('Ltest/BadParentCall;', 'Ltest/BadParentField;', 'Ltest/BadParentConstructor;'):
        assert 'parent-intrinsic-unimplemented-member' in [r[0] for r in parent_audit.rejected[d]]
    assert 'overrides-nonoverridable' in [r[0] for r in parent_audit.rejected['Ltest/BadOverride;']]
    assert 'parent-not-admitted' in [r[0] for r in parent_audit.rejected['Ltest/BinderChild;']]
    # ADR-0155: existing BootDex clinit and ordinary bodies, class constants,
    # field/invoke owners, a second hop, rejected boundaries, intrinsic declarations.
    seed = 'Ltest/Existing;'
    smali[seed] = parse_smali_class(_smali(seed, methods=clinit(
        ' new-instance v0, Ltest/ExactType;\n') + '.method public f()V\n'
        ' const-class v0, Ltest/Const;\n'
        ' check-cast v0, Ltest/Native;\n'
        ' sget-object v0, Ltest/Cycle1;->value:Ljava/lang/Object;\n'
        ' invoke-static {}, Ltest/SoloHelper;->run(Ltest/Solo;)I\n'
        ' return-void\n.end method\n'))
    smali['Ltest/Native;'] = parse_smali_class(_smali('Ltest/Native;', methods=
        '.method public f()V\n const-class v0, Ltest/Plain;\n return-void\n.end method\n'))
    analyzer.recipe_classes = {seed}
    reachable = analyzer.reachable_closure()
    assert {'Ltest/ExactType;', 'Ltest/Base;', 'Ltest/Const;', 'Ltest/Cycle1;',
            'Ltest/Cycle2;', 'Ltest/SoloHelper;', 'Ltest/Solo;'} <= reachable
    assert 'Ltest/Native;' not in reachable and 'Ltest/Plain;' not in reachable
    classes.append(_surface('Ltest/IntrinsicSeed;', 'intrinsic', methods=[
        unimpl('declaration', '()Ltest/Plain;')]))
    seeded = Analyzer(classes, smali, jar_of, {seed}, set(), set())
    seeded.analyze()
    assert 'Ltest/Plain;' in seeded.reachable_closure()
    assert parse_ops('instance-of v0, v1, Ltest/Const;') == [('ref', 'Ltest/Const;')]
    assert parse_ops('const-class v0, [Ltest/Const;') == [('ref', 'Ltest/Const;')]
    payload = Analyzer(classes, smali, jar_of, set(), set(), set(), boot_smali={
        'Ltest/GuestPayload;': parse_smali_class(_smali('Ltest/GuestPayload;', methods=
            '.method public f()V\n const-class v0, Ltest/Plain;\n return-void\n.end method\n'))})
    payload.analyze()
    assert 'Ltest/Plain;' in payload.reachable_closure()
    # The historical scope deliberately does not repair an existing BootDex seed.
    assert analyzer.closure_of({seed}) == set()
    # A second Solo override must turn the unique chain into a rejected one (sensitivity).
    add('Ltest/Solo2;', 'skeleton', [skel('<init>', '()V'), skel('name', '()I')], super_='Ltest/Solo;',
        extra='.method public name()I\n const/4 v0, 0x2\n return v0\n.end method\n')
    jar_of = {d: 'core.jar' for d in smali}
    second = Analyzer(classes, smali, jar_of, set(), set(), set())
    second.analyze()
    assert 'Ltest/ViaUniqueChain;' not in second.admitted
    # --cha-precise reports (does not default to) all-targets-implemented admission.
    precise = Analyzer(classes, smali, jar_of, set(), set(), set(), cha_precise=True)
    precise.analyze()
    assert 'Ltest/ViaUniqueChain;' in precise.admitted and 'Ltest/ViaVirtualChain;' in precise.admitted
    # ADR-0154: explicit only, ROM members replace handwritten shape; no new native.
    target = 'Ltest/Replacement;'
    add(target, 'intrinsic', [_method('old', '()V')])
    classes[-1]['methods'][0]['origin'] = 'handwritten'
    native = _method('endpoint', '()V', ACC_NATIVE | ACC_STATIC | 1, 'implemented')
    native['origin'] = 'handwritten'
    classes[-1]['methods'].append(native)
    rom = {target: ('Ljava/lang/Object;', [], 1,
                   [('endpoint', '()V', ACC_NATIVE | ACC_STATIC | 1), ('newEndpoint', '()V', ACC_NATIVE | 1)], [])}
    migrated, bindings = replacement_surface(classes, rom, {target})
    assert bindings == {(target, 'endpoint', '()V')}
    assert 'old' not in {m['name'] for c in migrated if c['descriptor'] == target for m in c['methods']}
    smali[target] = parse_smali_class(_smali(target))
    jar_of[target] = 'core.jar'
    replacement = Analyzer(migrated, smali, jar_of, set(), set(), set(),
                           source_tokens={'test/Replacement'}, replacements={target}, native_bindings=bindings)
    replacement.analyze()
    assert 'native-method' in [r[0] for r in replacement.rejected[target]]
    rom[target] = ('Ljava/lang/Object;', [], 1, [('endpoint', '()V', ACC_NATIVE | ACC_STATIC | 1)], [])
    migrated, bindings = replacement_surface(classes, rom, {target})
    replacement = Analyzer(migrated, smali, jar_of, set(), set(), set(),
                           source_tokens={'test/Replacement'}, replacements={target}, native_bindings=bindings)
    replacement.analyze()
    assert target in replacement.admitted
    # Explicit replacement follows the same parent audit and Binder rules.
    rom[target] = ('Landroid/os/Handler;', [], 1, [], [])
    migrated, bindings = replacement_surface(classes, rom, {target})
    parent = Analyzer(migrated, smali, jar_of, set(), set(), set(), replacements={target})
    parent.analyze()
    assert target in parent.admitted
    rom[target] = ('Ljava/lang/Object;', [], 1, [('<clinit>', '()V', ACC_STATIC)], [])
    migrated, bindings = replacement_surface(classes, rom, {target})
    smali[target] = parse_smali_class(_smali(target, methods=clinit(' new-instance v0, Landroid/os/Binder;\n')))
    binder = Analyzer(migrated, smali, jar_of, set(), set(), set(), replacements={target})
    binder.analyze()
    assert 'binder-in-clinit' in [r[0] for r in binder.rejected[target]]
    default = Analyzer(classes, smali, jar_of, set(), set(), set())
    assert target not in default.cand_set
    try:
        replacement_surface(classes, rom, {'Ltest/Plain;'})
        assert False, 'skeleton must not be accepted as an intrinsic replacement'
    except ValueError:
        pass
    print('select_rom_java self-test passed (admit, native, intrinsic parent, unimplemented target, '
          'binder, indirect virtual chain, CHA uniqueness, cascade, interaction, cycles, '
          'body reachability, class constants, rejected boundary, closure fixed point)')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--surface', type=Path)
    p.add_argument('--scan-dir', type=Path)
    p.add_argument('--report', type=Path)
    p.add_argument('--cache', type=Path, default=builder.ROOT / '.local/analysis/preflight/wu2a/smali')
    p.add_argument('--only', nargs='*', help='apply only these classes; they must all be admitted by the rules')
    p.add_argument('--replace-intrinsic', nargs='+', metavar='DESCRIPTOR',
                   help='ADR-0154 explicit migration; includes admitted initialization dependencies')
    p.add_argument('--cha-precise', action='store_true', help='sensitivity only: accept non-unique targets that are all implemented')
    p.add_argument('--scope', choices=('all', 'referenced', 'closure'), default='all',
                   help='closure: BootDex/intrinsic declaration body reachability; all: every admitted class; '
                        'referenced: classes the sample library uses plus their '
                        'superclass/<clinit>-required closure (single classes.dex has a 64K id limit)')
    p.add_argument('--check-closure', action='store_true',
                   help='fail and list admitted skeleton classes reachable from BootDex/intrinsic declarations')
    p.add_argument('--apply', action='store_true')
    p.add_argument('--self-test', action='store_true')
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not (args.surface and args.report):
        p.error('--surface and --report required')
    if args.scope == 'closure' and (args.only or args.replace_intrinsic or args.cha_precise):
        p.error('--scope closure requires default admission rules without explicit selection')
    if args.check_closure and (args.apply or args.only or args.replace_intrinsic or args.cha_precise):
        p.error('--check-closure requires unchanged default admission rules and cannot apply')
    if args.replace_intrinsic and (args.only or args.cha_precise):
        p.error('--replace-intrinsic cannot be combined with --only or --cha-precise')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
