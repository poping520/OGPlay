#!/usr/bin/env python3
"""ELF/JNI metadata for title_preflight; never loads or executes APK code."""
from __future__ import annotations
import re
import bisect
import struct
from dataclasses import dataclass, field


@dataclass
class Elf:
    data: bytes
    name: str
    needed: list[str] = field(default_factory=list)
    soname: str = ''
    symbols: list[dict] = field(default_factory=list)
    loads: list[tuple[int, int, int]] = field(default_factory=list)
    relocations: dict[int, int] = field(default_factory=dict)
    word: int = 4
    machine: int = 40

    def offset(self, address: int) -> int:
        for va, off, size in self.loads:
            if va <= address < va + size: return off + address - va
        raise ValueError('ELF pointer is outside file-backed segments')

    def cstring(self, address: int) -> str:
        off = self.offset(address); end = self.data.find(b'\0', off, off+4096)
        if end < 0: raise ValueError('unterminated ELF string')
        return self.data[off:end].decode('ascii')


def parse_elf(data: bytes, name: str) -> Elf:
    if data[:4] != b'\x7fELF' or data[4:6] not in (b'\x01\x01', b'\x02\x01'):
        raise ValueError('only little-endian ELF32/64 is supported')
    wide = data[4] == 2; result = Elf(data, name, word=8 if wide else 4)
    def unpack(fmt, off):
        if off < 0 or off + struct.calcsize(fmt) > len(data): raise ValueError('truncated ELF')
        return struct.unpack_from(fmt, data, off)
    result.machine = unpack('<H',18)[0]
    phoff = unpack('<Q' if wide else '<I',32 if wide else 28)[0]
    phsize, phnum = unpack('<HH',54 if wide else 42)
    dynamic = None
    for i in range(phnum):
        p=unpack('<IIQQQQQQ' if wide else '<IIIIIIII', phoff+i*phsize)
        typ=p[0];off,va,size=(p[2],p[3],p[5]) if wide else (p[1],p[2],p[4])
        if off+size>len(data): raise ValueError('ELF segment outside file')
        if typ==1: result.loads.append((va,off,size))
        if typ==2: dynamic=(off,size)
    if dynamic is None: raise ValueError('missing PT_DYNAMIC')
    tags={}
    for off in range(dynamic[0],sum(dynamic),2*result.word):
        tag,val=unpack('<QQ' if wide else '<II',off)
        if not tag: break
        tags.setdefault(tag,[]).append(val)
    def tag(n): return tags.get(n,[0])[0]
    strings=result.offset(tag(5))
    strsize=tag(10)
    if strings+strsize>len(data): raise ValueError('invalid DT_STRSZ')
    def string(n):
        if n>=strsize: raise ValueError('ELF string index outside DT_STRTAB')
        end=data.find(b'\0',strings+n,strings+strsize)
        if end<0: raise ValueError('unterminated dynamic string')
        return data[strings+n:end].decode('ascii')
    result.needed=[string(n) for n in tags.get(1,[])]
    result.soname=string(tag(14)) if tag(14) else name.rsplit('/',1)[-1]
    count=0
    if tag(4): count=unpack('<II',result.offset(tag(4)))[1]
    elif tag(0x6ffffef5):
        off=result.offset(tag(0x6ffffef5));nb,base,bloom,_=unpack('<IIII',off)
        buckets=off+16+bloom*result.word;chains=buckets+nb*4
        for i in range(nb):
            sym=unpack('<I',buckets+4*i)[0]
            if sym<base:continue
            while True:
                h=unpack('<I',chains+4*(sym-base))[0];count=max(count,sym+1)
                if h&1:break
                sym+=1
    else: raise ValueError('no supported symbol hash table')
    # Version names and dependency are retained; unrelated APK libraries never satisfy a root.
    requirements={};definitions={}
    if tag(0x6ffffffe):
        off=result.offset(tag(0x6ffffffe))
        for _ in range(tag(0x6fffffff)):
            _,n,lib,aux,nxt=unpack('<HHIII',off);a=off+aux
            for _ in range(n):
                _,_,idx,nm,anext=unpack('<IHHII',a);requirements[idx]=(string(nm),string(lib));a+=anext
            if not nxt:break
            off+=nxt
    if tag(0x6ffffffc):
        off=result.offset(tag(0x6ffffffc))
        for _ in range(tag(0x6ffffffd)):
            _,_,idx,_,_,aux,nxt=unpack('<HHHHIII',off);nm,_=unpack('<II',off+aux);definitions[idx]=string(nm)
            if not nxt:break
            off+=nxt
    symoff=result.offset(tag(6));symsize=tag(11) or (24 if wide else 16)
    for i in range(count):
        if wide:n,info,vis,section,val,size=unpack('<IBBHQQ',symoff+i*symsize)
        else:n,val,size,info,vis,section=unpack('<IIIBBH',symoff+i*symsize)
        version=unpack('<H',result.offset(tag(0x6ffffff0))+2*i)[0] if tag(0x6ffffff0) else 1
        idx=version&0x7fff
        result.symbols.append(dict(name=string(n),defined=section!=0,binding=info>>4,visibility=vis&3,value=val,size=size,
                                   version=definitions.get(idx),required_version=requirements.get(idx,(None,None))[0],
                                   required_library=requirements.get(idx,(None,None))[1],hidden_version=bool(version&0x8000)))
    # Relative relocations recover ordinary static JNINativeMethod pointer triples.
    rels=[(17,18,19,False),(7,8,9,True)]
    for ptr,sz,ent,rela in rels:
        if not tag(ptr):continue
        size=tag(ent) or result.word*(3 if rela else 2);off=result.offset(tag(ptr))
        for pos in range(off,off+tag(sz),size):
            items=unpack(('<QQq' if rela else '<QQ') if wide else ('<IIi' if rela else '<II'),pos)
            addr,info=items[:2];symbol=info>> (32 if wide else 8);typ=info& (0xffffffff if wide else 255)
            try:addend=items[2] if rela else unpack('<Q' if wide else '<I',result.offset(addr))[0]
            except ValueError:continue
            if symbol==0 and typ in (23,1027,8):result.relocations[addr]=addend
            elif symbol<len(result.symbols) and result.symbols[symbol]['defined']:
                result.relocations[addr]=result.symbols[symbol]['value']+addend
    return result


def native_import_gaps(libraries: dict[str,Elf], system: dict[str,Elf], exports: list[dict]) -> list[dict]:
    virtual={}
    for e in exports:virtual.setdefault(e['library'],set()).add(e['symbol'])
    aliases={}
    for name,elf in {**system,**libraries}.items():
        for alias in [name,elf.soname]:
            if alias in aliases and aliases[alias] is not elf:raise ValueError('ambiguous ELF SONAME '+alias)
            aliases[alias]=elf
    gaps=[]
    export_index={id(elf): {} for elf in aliases.values()}
    for elf in aliases.values():
        for sym in elf.symbols:
            if sym['defined'] and sym['binding'] in (1,2) and sym['visibility'] in (0,3):
                export_index[id(elf)].setdefault(sym['name'],[]).append(sym)
    for root in libraries.values():
        closure=[];virtual_scope=set();seen=set()
        def visit(elf):
            if elf.name in seen:return
            seen.add(elf.name);closure.append(elf)
            for dep in elf.needed:
                if dep in virtual:virtual_scope.add(dep)
                if dep in aliases:visit(aliases[dep])
                elif dep not in virtual:gaps.append(dict(kind='native-library',owner=elf.name,member=dep,descriptor='',weak=False,root=root.name))
        visit(root)
        for elf in closure:
            for sym in elf.symbols:
                if sym['defined'] or sym['binding'] not in (1,2) or not sym['name']:continue
                found=not sym['required_version'] and any(sym['name'] in virtual[v] for v in virtual_scope)
                for provider in closure:
                    if sym['required_library'] and provider.soname!=sym['required_library']:continue
                    for candidate in export_index[id(provider)].get(sym['name'],[]):
                        if not candidate['defined'] or candidate['binding'] not in (1,2) or candidate['visibility'] not in (0,3) or candidate['name']!=sym['name']:continue
                        if sym['required_version']:
                            if candidate['version']!=sym['required_version']:continue
                        elif candidate['hidden_version']:continue
                        found=True;break
                    if found:break
                if not found:gaps.append(dict(kind='native-symbol',owner=elf.name,member=sym['name'],descriptor='',weak=sym['binding']==2,root=root.name,version=sym['required_version']))
    unique={tuple(g.get(k) for k in ['kind','owner','member','version','weak']):g for g in gaps}
    return list(unique.values())


DESCRIPTOR=re.compile(r'\((?:\[*(?:[ZBSCIJFD]|L[^;\s]+;))*\)(?:V|\[*(?:[ZBSCIJFD]|L[^;\s]+;))\Z')
TYPE=re.compile(r'\[*(?:[ZBSCIJFD]|L[^;\s]+;)\Z')
CLASS=re.compile(r'(?:L)?(?:android|java|javax|dalvik|org/xml|org/json|org/apache/http)/[A-Za-z0-9_/$]+;?\Z')


def jni_references(elf: Elf, reference: dict[str,dict]) -> tuple[list[dict],list[dict]]:
    strings=[(m.start(),m.group()[:-1].decode('ascii')) for m in re.finditer(rb'[ -~]{3,}\x00|\[*[ZBSCIJFD]\x00',elf.data)]
    names={};positions={}
    for pos,s in strings:positions.setdefault(s,[]).append(pos)
    for pos,s in strings:
        if CLASS.fullmatch(s):
            desc=s if s.startswith('L') and s.endswith(';') else 'L'+s.rstrip(';')+';'
            names.setdefault(desc,[]).append(pos)
    refs=[]
    offsets=[p for p,_ in strings]
    class_positions=sorted((p,name) for name,locs in names.items() for p in locs)
    class_offsets=[p for p,_ in class_positions]
    for desc,locs in names.items():
        refs.append(dict(kind='class',owner=desc,member='',descriptor='',confidence='medium',evidence=f'{elf.name}: ASCII class string at {locs[0]:#x}'))
        # Pair class/name/signature against API19 member identities. Far pairs remain explicit low-confidence hints.
        seen=set();pending=[desc]
        while pending:
            owner=pending.pop()
            if owner in seen:continue
            seen.add(owner);cls=reference.get(owner,{})
            pending += [x for x in [cls.get('superclass'),*cls.get('interfaces',[])] if x]
            for typ in ['methods','fields']:
                for member in cls.get(typ,[]):
                    name,signature=member['name'],member['descriptor']
                    if name not in positions or signature not in positions:continue
                    distance=min(abs(a-b) for a in locs for b in positions[name])
                    refs.append(dict(kind='method' if typ=='methods' else 'field',owner=desc,member=name,descriptor=signature,
                                     confidence='medium' if distance<=512 and min(abs(a-b) for a in positions[name] for b in positions[signature])<=128 else 'low',
                                     evidence=f'{elf.name}: class/name/descriptor strings; nearest class/name distance={distance}; heuristic, no call-site proof'))
    # Not-in-API19 probes: require a nearby class and method/field signature.
    for pos,signature in strings:
        is_method=bool(DESCRIPTOR.fullmatch(signature))
        if not is_method and not TYPE.fullmatch(signature):continue
        nearby=[(abs(p-pos),name) for p,name in strings[bisect.bisect_left(offsets,pos-96):bisect.bisect_right(offsets,pos+96)] if re.fullmatch(r'[A-Za-z_$][\w$]{2,}',name) and name not in ['true','false','null']]
        nearest=bisect.bisect_left(class_offsets,pos)
        owners=[(abs(p-pos),name) for p,name in class_positions[max(0,nearest-1):nearest+1]]
        if not nearby or not owners:continue
        distance,owner=min(owners)
        if distance>512:continue
        _,member=min(nearby)
        refs.append(dict(kind='method' if is_method else 'field',owner=owner,member=member,descriptor=signature,confidence='low',
                         evidence=f'{elf.name}: nearby name/signature and class at distance={distance}; heuristic'))
    # RegisterNatives tables have no owner pointer: identify exact name/signature/function triples, report owner unknown.
    tables=[]
    addresses={}
    for pos,text in strings:
        for va,off,size in elf.loads:
            if off<=pos<off+size:addresses[va+pos-off]=text;break
    member_addresses={p:t for p,t in addresses.items() if re.fullmatch(r'[A-Za-z_$][\w$]*',t)}
    signature_addresses={p:t for p,t in addresses.items() if DESCRIPTOR.fullmatch(t)}
    for va,off,size in elf.loads:
        step=elf.word
        for i,(pointer,) in enumerate(struct.iter_unpack('<Q' if step==8 else '<I',elf.data[off:off+size-size%step])):
            addr=va+i*step;where=off+i*step
            pointer=elf.relocations.get(addr,pointer)
            if pointer not in member_addresses or where+3*step>off+size:continue
            sig=elf.relocations.get(addr+step,int.from_bytes(elf.data[where+step:where+2*step],'little'))
            if sig not in signature_addresses:continue
            target=elf.relocations.get(addr+2*step,int.from_bytes(elf.data[where+2*step:where+3*step],'little'))
            try:elf.offset(target&~1)
            except ValueError:continue
            tables.append(dict(name=member_addresses[pointer],descriptor=signature_addresses[sig],table_address=hex(addr),confidence='high',owner=None))
    return refs,tables
