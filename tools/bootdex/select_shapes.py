#!/usr/bin/env python3
"""Select referenced, behavior-free ROM shapes; reports precede explicit recipe update.

Conservative subset: abstract interfaces/annotations and final constant containers.
Even trivial instance constructors and nonliteral clinit code are rejected.
Dependencies include signature and annotation types from pinned baksmali output.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_bootdex as builder

JARS = ('core.jar', 'framework.jar', 'framework2.jar', 'ext.jar')
OBJECT = re.compile(r'L[\w$/]+;')


def shape_kind(text: str):
    header = next((x for x in text.splitlines() if x.startswith('.class ')), '')
    flags = set(header.split()[1:-1])
    if 'enum' in flags:return None, 'enum'
    kind = 'annotation' if 'annotation' in flags else 'interface' if 'interface' in flags else 'constants-final' if 'final' in flags else None
    if not kind:return None, 'not-interface-annotation-or-final'
    fields = re.findall(r'^\.field (.+)$', text, re.M)
    for field in fields:
        decl = field.split(' = ', 1)[0];words = decl.split();typ = words[-1].split(':')[-1]
        if not {'static', 'final'}.issubset(words) or typ not in ['Z','B','S','C','I','J','F','D','Ljava/lang/String;']:
            return None, 'nonconstant-field'
        # A literal encoded value, or own literal-only clinit assignment, is required below.
        if ' = ' not in field:return None, 'no-encoded-constant'
    if kind == 'constants-final' and not fields:return None, 'empty-final'
    for signature, body in re.findall(r'^\.method ([^\n]+)\n(.*?)^\.end method', text, re.M | re.S):
        if '<clinit>()V' in signature:
            # Empty clinit is the only accepted special case: all constants already encoded.
            instructions = [x.strip() for x in body.splitlines() if x.strip() and not x.strip().startswith(('.', '#'))]
            if instructions != ['return-void']:return None, 'clinit-behavior'
        elif kind not in ('interface','annotation') or 'abstract' not in signature.split() or re.search(r'^\s+(?![.#\s])\S', body, re.M):
            return None, 'method-behavior'
    return kind, None


def self_test():
    base='.class public interface abstract Ltest/I;\n.super Ljava/lang/Object;\n'
    assert shape_kind(base+'.method public abstract f()V\n.end method\n')[0]=='interface'
    assert shape_kind(base+'.field public static final ONE:I = 0x1\n')[0]=='interface'
    assert shape_kind(base+'.method static constructor <clinit>()V\n .registers 0\n return-void\n.end method\n')[0]=='interface'
    assert shape_kind(base+'.method static constructor <clinit>()V\n invoke-static {}, Ltest/X;->f()V\n return-void\n.end method\n')[0] is None
    assert shape_kind(base+'.field public static mutable:I\n')[0] is None
    assert shape_kind('.class public final Ltest/C;\n.field public static final X:I = 0x1\n')[0]=='constants-final'
    assert shape_kind(base.replace('interface abstract','interface abstract annotation'))[0]=='annotation'
    assert shape_kind('.class public final enum Ltest/E;\n')[0] is None
    assert shape_kind('.class public final Ltest/C;\n.field public static final X:I = 0x1\n.method private constructor <init>()V\n return-void\n.end method\n')[0] is None
    print('select_shapes self-test passed (abstract, constants, clinit, enums, constructors)')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--surface',type=Path);p.add_argument('--scan-dir',type=Path)
    p.add_argument('--report',type=Path);p.add_argument('--cache',type=Path,default=builder.ROOT/'.local/analysis/preflight/wu2a/smali')
    p.add_argument('--apply',action='store_true');p.add_argument('--self-test',action='store_true');a=p.parse_args()
    if a.self_test:self_test();return 0
    if not all([a.surface,a.scan_dir,a.report]):p.error('--surface --scan-dir --report required')
    runtime={c['descriptor']:c for c in json.loads(a.surface.read_text(encoding='utf-8'))['classes']}
    recipe=json.loads(builder.RECIPE.read_text(encoding='utf-8'));builder.verify_inputs(builder.load_recipe(recipe))
    reserved={d for category,values in recipe['date_family_audit']['classifications'].items() if category in ('existing_vm_intrinsic','native_boundary') for d in values}
    summary=json.loads((a.scan_dir/'summary.json').read_text(encoding='utf-8'))
    if summary['errors'] or summary['incomplete'] or not summary['scanned'] or summary['scanned']!=summary['apks']:
        raise ValueError('complete sample library scan required')
    referenced=set();scan_count=0
    for path in a.scan_dir.glob('*.json'):
        if path.name=='summary.json':continue
        scan=json.loads(path.read_text(encoding='utf-8'))
        if not scan.get('complete'):raise ValueError('incomplete APK scan: '+str(path))
        scan_count+=1
        if scan.get('surface_sha256')!=builder.file_sha256(a.surface):raise ValueError('scan/runtime surface mismatch: '+str(path))
        for gap in scan['gaps']:
            if gap['classification']=='api19-present-missing':referenced.add(gap['owner'])
    if scan_count!=summary['scanned']:raise ValueError('sample scan count mismatch')
    candidates={};rejected=[];conflicts=[];inputs={}
    for jar in JARS:
        path=builder.source_path(jar);digest=builder.file_sha256(path);inputs[jar]=digest
        dest=a.cache/(jar+'-'+digest[:16]);stamp=dest/'.complete'
        if not stamp.exists():
            dest.mkdir(parents=True,exist_ok=True)
            subprocess.run([str(builder.JAVA),'-jar',str(builder.SMALI/'baksmali.jar'),'disassemble','--api','19','--jobs','1','--output',str(dest),str(path)],check=True)
            stamp.write_text(digest,encoding='utf-8')
        for path in sorted(dest.rglob('*.smali')):
            text=path.read_text(encoding='utf-8');kind,reason=shape_kind(text)
            owner='L'+path.relative_to(dest).as_posix()[:-6]+';'
            if not kind:
                rejected.append(dict(owner=owner,jar=jar,reason=reason));continue
            if owner in runtime or owner in reserved:
                conflicts.append(dict(owner=owner,jar=jar,source=runtime.get(owner,{}).get('source','intrinsic-policy'),reason='already-runtime-owned'));continue
            if owner in candidates:raise ValueError('duplicate ROM class '+owner)
            # Remove string literals/comments before descriptor extraction; include annotations.
            metadata=re.sub(r'"(?:\\.|[^"\\])*"','""',text)
            deps=sorted(set(OBJECT.findall(metadata))-{owner})
            candidates[owner]=dict(owner=owner,jar=jar,kind=kind,dependencies=deps,referenced=owner in referenced)
    # Existing owners must link; a failed runtime export cannot certify closure.
    available={d for d,c in runtime.items() if not c.get('link_error')}
    # Only choose classes whose complete dependency set is already available.
    # No opportunistic addition of unreferenced dependency classes.
    selected=[]
    for owner,candidate in sorted(candidates.items()):
        missing=sorted(set(candidate['dependencies'])-available)
        candidate['missing_dependencies']=missing
        candidate['selected']=candidate['referenced'] and not missing
        if candidate['selected']:selected.append(owner)
    report=dict(schema=1,inputs=inputs,surface_sha256=builder.file_sha256(a.surface),
                policy='conservative encoded scalar/String constants; empty clinit only; no constructors/enums; dependencies already linked in runtime',
                candidates=list(candidates.values()),conflicts=conflicts,rejected=rejected,selected=selected)
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(report,ensure_ascii=True,indent=2)+'\n',encoding='utf-8')
    # Report is committed to disk before any recipe mutation; explicit --apply required.
    if a.apply:
        for owner in selected:
            group=recipe['classes'].setdefault(candidates[owner]['jar'],[])
            if owner not in group:group.append(owner);group.sort()
        builder.RECIPE.write_text(json.dumps(recipe,ensure_ascii=True,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(candidates=len(candidates),conflicts=len(conflicts),selected=len(selected),report=str(a.report),applied=a.apply)))
    return 0

if __name__=='__main__':raise SystemExit(main())
