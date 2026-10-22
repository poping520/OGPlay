#!/usr/bin/env python3
"""Pinned API19 own-member shapes, indexed class JSON records; no behavior/stubs."""
from __future__ import annotations
import argparse, hashlib, json, struct, zipfile
from pathlib import Path
import dex_survey_lib as dex
from dexdis import load_catalog
ROOT=Path(__file__).resolve().parent.parent
PINNED={
 'core.jar':'996557954e45f7192b187b2394259bc8aca6e3946652e03b99d837432f83d1ef',
 'framework.jar':'90fc9fd36b45db1810ecf51e40119222da081b552f9a64a6d0c09cac09f98a48',
 'framework2.jar':'b6aba8bec45e2a2faa7a4e1e8ae3d8e450259f7e28c8dae6ca2c31dc34e06bea',
 'ext.jar':'618663a2f417f7eb4ae903d25809ffdd90fe5b5236184c530228dd66bfcdb65c'}
MAGIC=b'OGSK19\x01\n'
def pack(records):
 index=bytearray(MAGIC+struct.pack('<I',len(records)));payload=bytearray()
 for owner,record in sorted(records.items()):
  name=owner.encode();body=json.dumps(record,ensure_ascii=True,separators=(',',':'),allow_nan=False).encode()
  index+=struct.pack('<H',len(name))+name+struct.pack('<II',len(payload),len(body));payload+=body
 return bytes(index+payload)
def unpack(data):
 """Read the OGSK19-v1 index and own-member records (skeleton_surface.cpp)."""
 if len(data)<12 or data[:8]!=MAGIC:raise ValueError('invalid API19 skeleton header')
 count=struct.unpack_from('<I',data,8)[0]
 if count>20000:raise ValueError('invalid API19 skeleton count')
 cursor=12;index={};previous=''
 def integer(width):
  nonlocal cursor
  if cursor+width>len(data):raise ValueError('truncated API19 skeleton index')
  value=int.from_bytes(data[cursor:cursor+width],'little');cursor+=width
  return value
 for _ in range(count):
  length=integer(2)
  if not length or cursor+length>len(data):raise ValueError('invalid skeleton name')
  name=data[cursor:cursor+length].decode('utf-8');cursor+=length
  offset,size=integer(4),integer(4)
  if name<=previous or not name.startswith('L') or not name.endswith(';'):raise ValueError('invalid skeleton ordering/name')
  previous=name;index[name]=(offset,size)
 records={};end=cursor
 for name,(offset,size) in index.items():
  start=cursor+offset;stop=start+size
  if stop>len(data):raise ValueError('invalid skeleton record range')
  record=json.loads(data[start:stop])
  if not isinstance(record,list) or len(record)!=5:raise ValueError('invalid skeleton record')
  records[name]=record;end=max(end,stop)
 if end!=len(data):raise ValueError('trailing skeleton bytes')
 return records

def clinit_writes(data,code,catalog):
 units=struct.unpack_from('<%dH'%code.insns_size,data,code.insns_offset);pc=0;writes=set()
 while pc<len(units):
  op=units[pc]&255
  if op==0 and units[pc]>>8:
   ident=units[pc]
   width=4+2*units[pc+1] if ident==0x100 else 2+4*units[pc+1] if ident==0x200 else 4+(units[pc+1]*(units[pc+2]|units[pc+3]<<16)+1)//2
  else:
   if 0x67<=op<=0x6d:writes.add(units[pc+1])
   width=int(catalog[op]['format'][0])
  pc+=width
 if pc!=len(units):raise ValueError('invalid clinit instruction width')
 return writes

def generate(directory):
 records={};catalog=load_catalog();methods=fields=constants=0
 for jar,digest in PINNED.items():
  raw=(directory/jar).read_bytes()
  if hashlib.sha256(raw).hexdigest()!=digest:raise ValueError('pinned SHA256 mismatch: '+jar)
  import io
  data=zipfile.ZipFile(io.BytesIO(raw)).read('classes.dex');image=dex.parse_dex(data)
  for cls in image.classes:
   owner=image.type_name(cls.type_index)
   if owner in records:raise ValueError('duplicate ROM class: '+owner)
   mm=[];ff=[];writes=set()
   for m in cls.direct_methods+cls.virtual_methods:
    _,name,desc=image.method_signature(m.method_index);mm.append([name,desc,m.access_flags])
    if name=='<clinit>' and m.code:writes|=clinit_writes(data,m.code,catalog)
   values=dex.static_values(data,image,cls)
   for i,f in enumerate(cls.static_fields+cls.instance_fields):
    _,typ,ni=image.field_ids[f.field_index];desc=image.type_name(typ);kind=0;value=None
    if i<len(cls.static_fields) and f.access_flags&0x18==0x18 and f.field_index not in writes and i<len(values):
     v=values[i]
     if desc=='Ljava/lang/String;' and isinstance(v,str):kind=2;value=v
     elif desc in 'ZBSCIJFD' and len(desc)==1 and isinstance(v,(int,float)):
      kind=1
      if desc in 'FD':value=str(int.from_bytes(struct.pack('<f' if desc=='F' else '<d',v),'little',signed=True))
      else:value=str(int(v))
    ff.append([image.strings[ni],desc,f.access_flags,kind,value]);constants+=bool(kind)
   methods+=len(mm);fields+=len(ff)
   records[owner]=[image.type_name(cls.superclass_index) if cls.superclass_index is not None else None,[image.type_name(t) for t in cls.interface_type_indices],cls.access_flags,sorted(mm),sorted(ff)]
 payload=pack(records)
 manifest=dict(schema=1,format='OGSK19-v1: u32 count; sorted (u16 name bytes,u32 offset,u32 size); compact JSON [super,interfaces,flags,own methods,own fields]; offsets relative to payload',inputs=PINNED,classes=len(records),methods=methods,fields=fields,constants=constants,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest(),constant_policy='static final primitive/String encoded values excluding any clinit sput target; numeric raw bits as signed decimal strings')
 return payload,(json.dumps(manifest,indent=2)+'\n').encode()
def self_test():
 r={'Lx/C;':[None,[],1,[['m','()V',1]],[['X','I',25,1,'123']]]}
 b=pack(r);assert b[:8]==MAGIC
 many={**r,'Lx/Z;':[None,[],1,[],[]]}
 assert pack(many)==pack(dict(reversed(list(many.items()))))
 code=dex.DexCode(1,0,0,0,4,0)
 assert clinit_writes(struct.pack('<4H',0x12,0x67,3,0x0e),code,load_catalog())=={3}
 n=struct.unpack_from('<I',b,8)[0];assert n==1
 length=struct.unpack_from('<H',b,12)[0];off,size=struct.unpack_from('<II',b,14+length)
 assert json.loads(b[22+length+off:22+length+off+size])==r['Lx/C;']
 import tempfile
 with tempfile.TemporaryDirectory() as d:
  (Path(d)/'core.jar').write_bytes(b'wrong')
  try:generate(Path(d));raise AssertionError('untrusted input accepted')
  except ValueError as e:assert 'SHA256 mismatch' in str(e)
 print('api19_skeleton self-test passed (indexed format, determinism, pinned rejection)')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--aosp',type=Path,default=ROOT/'.local/aosp');p.add_argument('--output-dir',type=Path,default=ROOT/'data/dexvm');p.add_argument('--check',action='store_true');p.add_argument('--self-test',action='store_true');a=p.parse_args()
 if a.self_test:self_test();return
 binary,manifest=generate(a.aosp)
 for name,data in [('api19-skeleton.bin',binary),('api19-skeleton.json',manifest)]:
  path=a.output_dir/name
  if a.check:
   if path.read_bytes()!=data:raise ValueError('stale skeleton: '+str(path))
  else:path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
 print(manifest.decode())
if __name__=='__main__':main()
