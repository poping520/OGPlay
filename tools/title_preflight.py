#!/usr/bin/env python3
"""Read-only APK compatibility inventory against runtime JSON and API19 ROM surface.

Potential dependencies, NOT reachable faults or a compatibility gate. No stubs,
APK execution, C++ source regexes, or runtime surface fabrication.
"""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile
import dex_survey_lib as dexlib
from title_native_scan import parse_elf, native_import_gaps, jni_references


def index_surface(surface: dict) -> dict[str,dict]:
    classes=surface.get('classes')
    if not isinstance(classes,list):raise ValueError('surface.classes must be an array')
    index={}
    for cls in classes:
        desc=cls['descriptor']
        if desc in index:raise ValueError('duplicate surface class '+desc)
        index[desc]=cls
    return index


def resolve(index: dict, owner: str, name: str, desc: str, kind: str):
    visited=set();stack=[owner]
    while stack:
        clsname=stack.pop()
        if clsname in visited:continue
        visited.add(clsname);cls=index.get(clsname)
        if cls is None:continue
        for member in cls.get('methods' if kind=='method' else 'fields',[]):
            if member['name']==name and member['descriptor']==desc:return clsname,member
        if name in ['<init>','<clinit>']:continue
        # Prefer superclass for methods; fields search direct interfaces first (Dalvik).
        parents=[p for p in [cls.get('superclass'),*cls.get('interfaces',[])] if p]
        stack.extend(reversed(parents if kind=='method' else parents[1:]+parents[:1]))
    return None


def classify_reference(ref: dict, actual: dict, expected: dict):
    kind=ref['kind'];owner=ref['owner'];name=ref.get('member','');desc=ref.get('descriptor','')
    result=dict(ref)
    if kind in ['class','hierarchy']:
        if owner in actual:return None
        result['classification']='api19-present-missing' if owner in expected else 'not-in-api19'
        result['expected_result']=None if owner in expected else 'ClassNotFoundException/NoClassDefFoundError'
        return result
    member=resolve(actual,owner,name,desc,kind)
    reference=resolve(expected,owner,name,desc,kind)
    if not member:
        result['classification']='api19-present-missing' if reference else 'not-in-api19'
        result['expected_result']=None if reference else ('NoSuchMethodError' if kind=='method' else 'NoSuchFieldError')
        if reference:
            result['reference_owner']=reference[0]
            if kind=='field' and reference[1].get('has_constant'):
                result['kind']='constant';result['expected_constant']=reference[1]['constant']
        return result
    if kind=='method' and member[1].get('status')=='unimplemented-ledger':
        # An admitted dynamic JNI native needs symbol registration at process assembly;
        # the catalog cannot prove either implementation or a deliberate failure.
        result['classification']='declared-unimplemented'
        result['implementation_certainty']='unverified' if member[1].get('binding')=='guest-jni-unverified' else 'catalog-unbound'
        return result
    if kind=='field' and reference and reference[1].get('has_constant'):
        field=member[1]
        # Missing encoded initializer is not automatically wrong: Java clinit can compute it.
        if field.get('has_constant') and field.get('constant')!=reference[1]['constant']:
            result.update(kind='constant',classification='api19-present-missing',expected_constant=reference[1]['constant'],actual_constant=field['constant'],reason='constant-value-mismatch')
            return result
    return None


def dex_references(archives: list[tuple[str,bytes]], actual: dict, expected: dict):
    app={};parsed=[];refs=[]
    for name,data in archives:
        dex=dexlib.parse_dex(data);parsed.append((name,dex))
        for cls in dex.classes:
            desc=dex.type_name(cls.type_index)
            entry={'descriptor':desc,'superclass':dex.type_name(cls.superclass_index) if cls.superclass_index is not None else None,
                   'interfaces':[dex.type_name(i) for i in cls.interface_type_indices],'methods':[],'fields':[]}
            for method in cls.direct_methods+cls.virtual_methods:
                _,mn,md=dex.method_signature(method.method_index)
                entry['methods'].append(dict(name=mn,descriptor=md,static=bool(method.access_flags&8),status='implemented'))
            for field in cls.static_fields+cls.instance_fields:
                _,ti,ni=dex.field_ids[field.field_index];entry['fields'].append(dict(name=dex.strings[ni],descriptor=dex.type_name(ti),static=bool(field.access_flags&8)))
            if desc in app:raise ValueError('duplicate application class across DEX: '+desc)
            app[desc]=entry
    def platform(desc):return desc in expected or desc in actual or dexlib.is_platform_descriptor(desc) or desc.startswith(('Lorg/w3c/','Lcom/android/','Lsun/'))
    # Bootstrap wins only when it is actually supplied. App-bundled non-platform support remains app-owned.
    app={d:c for d,c in app.items() if d not in actual and not dexlib.is_platform_descriptor(d)}
    combined_actual={**app,**actual};combined_expected={**app,**expected}
    for filename,dex in parsed:
        def add(kind,owner,member='',descriptor='',**extra):
            refs.append(dict(kind=kind,owner=owner,member=member,descriptor=descriptor,source='java',confidence='high',evidence=filename+': constant-pool/hierarchy (not reachability)',**extra))
        for desc in dex.type_descriptors:
            component=desc.lstrip('[')
            if component.startswith('L') and component not in app and platform(component):add('class',component)
        for cls in dex.classes:
            app_name=dex.type_name(cls.type_index)
            # Real bootstrap precedence: ignore dormant duplicate APK definitions.
            if app_name in actual:continue
            pending=[x for x in [dex.type_name(cls.superclass_index) if cls.superclass_index is not None else None,*[dex.type_name(i) for i in cls.interface_type_indices]] if x]
            visited=set()
            while pending:
                ancestor=pending.pop()
                if ancestor in visited:continue
                visited.add(ancestor)
                if platform(ancestor):add('hierarchy',ancestor,required_by=app_name)
                definition=combined_expected.get(ancestor,{})
                pending += [x for x in [definition.get('superclass'),*definition.get('interfaces',[])] if x]
        for i in range(len(dex.method_ids)):
            owner,name,desc=dex.method_signature(i)
            if owner.startswith('['):continue
            if owner in app and resolve(app,owner,name,desc,'method'):continue
            if platform(owner) or (owner in app and resolve(combined_expected,owner,name,desc,'method')):
                add('method',owner,name,desc)
        for ci,ti,ni in dex.field_ids:
            owner=dex.type_name(ci);name=dex.strings[ni];desc=dex.type_name(ti)
            if owner in app and resolve(app,owner,name,desc,'field'):continue
            if platform(owner) or (owner in app and resolve(combined_expected,owner,name,desc,'field')):
                add('field',owner,name,desc)
    return refs,combined_actual,combined_expected


def engine_names(names: list[str]) -> list[str]:
    joined=' '.join(names).lower();engines=[]
    for name,pattern in [('Unity',r'libunity|libmono'),('cocos2d-x',r'cocos2d|libcocos'),('Unreal',r'libue3|libue4|libunreal'),('Gameloft',r'libasphalt|liband[a-z0-9]*\.so|libdungeon|libwildblood|libgameloft'),('libgdx',r'libgdx')]:
        if re.search(pattern,joined):engines.append(name)
    return engines or ['unknown']



def hierarchy_references(refs: list[dict], actual: dict, expected: dict) -> list[dict]:
    expanded=set();hierarchy_refs=[]
    for ref in refs:
        pending=[ref['owner']];seen=set()
        while pending:
            owner=pending.pop()
            if owner in seen:continue
            seen.add(owner)
            cls=actual.get(owner) or expected.get(owner,{})
            for parent in [cls.get('superclass'),*cls.get('interfaces',[])]:
                if not parent:continue
                pending.append(parent)
                key=(ref['source'],parent,ref['owner'])
                if parent not in actual and key not in expanded:
                    expanded.add(key)
                    hierarchy_refs.append(dict(ref,kind='hierarchy',owner=parent,member='',descriptor='',required_by=ref['owner']))
    return hierarchy_refs

def scan_apk(apk: Path, surface: dict, reference: dict, system: dict) -> dict:
    actual=index_surface(surface);expected=index_surface(reference);diagnostics=[]
    with zipfile.ZipFile(apk) as archive:
        names=archive.namelist();dexes=sorted(n for n in names if re.fullmatch(r'classes(?:[2-9]\d*)?\.dex',n))
        if not dexes:raise ValueError('APK has no root classes.dex')
        try:refs,combined_actual,combined_expected=dex_references([(n,archive.read(n)) for n in dexes],actual,expected)
        except (dexlib.DexFormatError,ValueError,IndexError) as error:
            # Explicit partial result; no successful full-APK scan claim.
            refs=[];combined_actual=actual;combined_expected=expected;diagnostics.append('DEX scan incomplete: '+str(error))
        abi=next((abi for abi in ['armeabi-v7a','armeabi'] if any(n.startswith('lib/'+abi+'/') and n.endswith('.so') for n in names)),None)
        libraries={};tables=[]
        for n in sorted(names):
            if abi is None or not n.startswith('lib/'+abi+'/') or not n.endswith('.so'):continue
            try:
                elf=parse_elf(archive.read(n),n);libraries[Path(n).name]=elf
                native,t= jni_references(elf,expected)
                refs.extend(dict(r,source='native',library=n) for r in native)
                tables.extend(dict(item,library=n) for item in t)
            except (ValueError,IndexError,struct_error()) as error:diagnostics.append(n+': native scan incomplete: '+str(error))
        if not abi and any(n.startswith('lib/') and n.endswith('.so') for n in names):diagnostics.append('no supported ARM32 ABI; native scan not performed')
    refs.extend(hierarchy_references(refs,combined_actual,combined_expected))
    gaps=[]
    for ref in refs:
        gap=classify_reference(ref,combined_actual,combined_expected)
        if gap:gaps.append(gap)
    try:
        for gap in native_import_gaps(libraries,system,surface.get('native_exports',[])):
            gap.update(source='native',classification='native-import-missing',confidence='high',evidence='ELF DT_NEEDED closure/versioned dynamic symbol lookup');gaps.append(gap)
    except ValueError as error:diagnostics.append('native import scan incomplete: '+str(error))
    # Preserve all provenance on each deduplicated identity.
    unique={}
    for gap in gaps:
        key=tuple(gap.get(k,'') for k in ['source','classification','kind','owner','member','descriptor'])
        if key in unique:
            old=unique[key];old['occurrences']+=1
            if gap['confidence']=='high' or old['confidence']=='low':old['confidence']=gap['confidence']
            evidence=gap['evidence']
            if evidence not in old['evidence'] and len(old['evidence'])<8:old['evidence'].append(evidence)
        else:unique[key]=dict(gap,occurrences=1,evidence=[gap['evidence']])
    gaps=sorted(unique.values(),key=lambda g:tuple(str(g.get(k,'')) for k in ['source','kind','classification','owner','member','descriptor']))
    return dict(schema=1,apk=str(apk),apk_sha256=hashlib.sha256(apk.read_bytes()).hexdigest(),abi=abi,engines=engine_names(list(libraries)),
                complete=not diagnostics,diagnostics=diagnostics,surface_link_errors=[dict(owner=owner,error=actual[owner]['link_error']) for owner in sorted({r['owner'] for r in refs}) if owner in actual and actual[owner].get('link_error')],summary=dict(total=len(gaps),classifications=dict(collections.Counter(g['classification'] for g in gaps)),
                sources=dict(collections.Counter(g['source'] for g in gaps)),kinds=dict(collections.Counter(g['kind'] for g in gaps))),gaps=gaps,register_natives_candidates=tables,
                limitations=['all DEX references, including dormant/optional paths; not a must-fix/reachability gate',
                'native JNI pairing is heuristic; RegisterNatives owner is not recoverable from pointer triples alone',
                'guest-jni-unverified denotes runtime binding unknown, not proven absence of a guest implementation',
                'no syscall argument discovery; no dlopen names constructed at runtime; APK assets containing .so are not executed or unpacked'])


def struct_error():
    import struct
    return struct.error


def load_system(directory: Path) -> dict:
    result={}
    for path in sorted(directory.glob('*.so')):
        elf=parse_elf(path.read_bytes(),path.name);result[path.name]=elf
    if not result:raise ValueError('no system ELF libraries in '+str(directory))
    return result


def self_test() -> int:
    parent=dict(descriptor='Ljava/test/Base;',superclass=None,interfaces=[],methods=[dict(name='read',descriptor='()I',static=False,status='implemented'),dict(name='todo',descriptor='()V',static=False,status='unimplemented-ledger')],fields=[dict(name='ONE',descriptor='I',static=True,has_constant=True,constant=1)])
    child=dict(descriptor='Ljava/test/Child;',superclass=parent['descriptor'],interfaces=[],methods=[],fields=[])
    actual=index_surface(dict(classes=[parent,child]));reference=index_surface(dict(classes=[parent,child]))
    ref=dict(kind='method',owner=child['descriptor'],member='read',descriptor='()I')
    assert classify_reference(ref,actual,reference) is None
    assert classify_reference(dict(ref,member='missing'),actual,reference)['classification']=='not-in-api19'
    assert classify_reference(dict(ref,member='todo',descriptor='()V'),actual,reference)['classification']=='declared-unimplemented'
    missing={child['descriptor']:child}
    assert classify_reference(ref,missing,reference)['classification']=='api19-present-missing'
    field=dict(kind='field',owner=child['descriptor'],member='ONE',descriptor='I')
    assert classify_reference(field,missing,reference)['kind']=='constant'
    assert resolve(actual,child['descriptor'],'<init>','()V','method') is None
    assert classify_reference(dict(kind='hierarchy',owner=parent['descriptor']),missing,reference)['classification']=='api19-present-missing'
    inherited=hierarchy_references([dict(kind='class',owner=child['descriptor'],source='native')],missing,reference)
    assert any(r['owner']==parent['descriptor'] and r['kind']=='hierarchy' for r in inherited)
    cyc=dict(parent,superclass=child['descriptor']);assert resolve({**actual,parent['descriptor']:cyc},child['descriptor'],'absent','()V','method') is None
    # A class occurring in the reference ROM is not automatically owned by the
    # selected bootstrap: APK-provided classes outside the runtime prefix rule win.
    from unittest.mock import patch
    appdesc='Lorg/kxml2/io/KXmlParser;'
    sample=dexlib.DexFile([], [appdesc], [], [], [], [dexlib.DexClass(0,1,None,[])])
    with patch.object(dexlib,'parse_dex',return_value=sample):
        refs,merged,_=dex_references([('classes.dex',b'fixture')],{}, {appdesc:dict(child,descriptor=appdesc)})
    assert appdesc in merged and not refs
    from title_native_scan import Elf, jni_references
    data=b'java/test/Base\0read\0()I\0ONE\0I\0'
    fake=Elf(data,'probe.so',loads=[(0,0,len(data))])
    native,_=jni_references(fake,reference)
    assert any(r['kind']=='method' and r['member']=='read' for r in native)
    assert any(r['kind']=='field' and r['member']=='ONE' and r['descriptor']=='I' for r in native)

    root=Elf(b'','app.so',needed=['liba.so'],soname='app.so',symbols=[dict(name='f',defined=False,binding=1,required_version=None,required_library=None)])
    unrelated=Elf(b'','libb.so',soname='libb.so',symbols=[dict(name='f',defined=True,binding=1,visibility=0,version=None,hidden_version=False)])
    assert native_import_gaps({'app.so':root},{'libb.so':unrelated},[])
    assert not native_import_gaps({'app.so':root},{},[dict(library='liba.so',symbol='f')])
    try:parse_elf(b'bad','bad.so');raise AssertionError('malformed ELF accepted')
    except ValueError:pass
    print('title_preflight self-test passed (inheritance, constants, classifications, cycles, constructor, import scope)');return 0


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--surface',type=Path);parser.add_argument('--reference',type=Path)
    parser.add_argument('--system-libs',type=Path,default=Path(__file__).resolve().parent.parent/'data/android/19/lib')
    parser.add_argument('--apk',type=Path);parser.add_argument('--games-dir',type=Path)
    parser.add_argument('--output',type=Path);parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--self-test',action='store_true');args=parser.parse_args()
    if args.self_test:return self_test()
    if not args.surface or not args.reference or bool(args.apk)==bool(args.games_dir):parser.error('provide --surface --reference and exactly one --apk/--games-dir')
    surface=json.loads(args.surface.read_text(encoding='utf-8'));reference=json.loads(args.reference.read_text(encoding='utf-8'));system=load_system(args.system_libs)
    apks=[args.apk] if args.apk else sorted(args.games_dir.rglob('*.apk'));reports=[];errors=[]
    if not apks:parser.error('no APK found')
    if args.games_dir and not args.output_dir:parser.error('--games-dir requires --output-dir')
    if args.output_dir:args.output_dir.mkdir(parents=True,exist_ok=True)
    for apk in apks:
        try:report=scan_apk(apk,surface,reference,system)
        except (OSError,ValueError,zipfile.BadZipFile) as error:
            errors.append(dict(apk=str(apk),error=str(error)));print(str(apk)+': '+str(error),file=sys.stderr);continue
        report['surface_sha256']=hashlib.sha256(args.surface.read_bytes()).hexdigest();report['reference_sha256']=hashlib.sha256(args.reference.read_bytes()).hexdigest();reports.append(report)
        output=args.output if args.apk else args.output_dir/(report['apk_sha256'][:16]+'.json')
        if output:output.write_text(json.dumps(report,ensure_ascii=True,indent=2)+'\n',encoding='utf-8')
        else:print(json.dumps(report,ensure_ascii=True,indent=2))
        print(f'{apk}: '+str(report['summary'])+(' INCOMPLETE' if not report['complete'] else ''),file=sys.stderr)
    if args.games_dir:
        frequency=collections.Counter();engines=collections.Counter();class_frequency=collections.Counter();engine_frequency=collections.Counter()
        for r in reports:
            for engine in r['engines']:engines[engine]+=1
            seen=set()
            class_frequency.update(set(g['owner'] for g in r['gaps'] if g['owner'].startswith('L')))
            for g in r['gaps']:
                key=tuple(g.get(k,'') for k in ['classification','kind','owner','member','descriptor'])
                if key in seen:continue
                seen.add(key);frequency[key]+=1
                for engine in r['engines']:engine_frequency[(engine,*key)]+=1
        summary=dict(schema=1,apks=len(apks),scanned=len(reports),incomplete=sum(not r['complete'] for r in reports),errors=errors,engines=dict(engines),
                     class_frequency=[dict(owner=k,apks=v) for k,v in class_frequency.most_common()],
                     engine_frequency=[dict(engine=k[0],classification=k[1],kind=k[2],owner=k[3],member=k[4],descriptor=k[5],apks=v) for k,v in engine_frequency.most_common()],
                     frequency=[dict(classification=k[0],kind=k[1],owner=k[2],member=k[3],descriptor=k[4],apks=v) for k,v in frequency.most_common()])
        (args.output_dir/'summary.json').write_text(json.dumps(summary,ensure_ascii=True,indent=2)+'\n',encoding='utf-8')
        (args.output_dir/'summary.md').write_text('# 静态缺口频次\n\n'+f'APK {len(apks)}；完成扫描 {len(reports)}；不完整 {summary["incomplete"]}；错误 {len(errors)}。\n\n'+
            '| 分类 | 类型 | 类/成员 | APK频次 |\n| --- | --- | --- | --- |\n'+'\n'.join('| '+ ' | '.join([row['classification'],row['kind'],row['owner']+'->'+row['member']+row['descriptor'],str(row['apks'])])+' |' for row in summary['frequency'][:100])+'\n',encoding='utf-8')
    return 1 if errors or any(not r['complete'] for r in reports) else 0


if __name__=='__main__':raise SystemExit(main())
