"""Exercise H1I1/H1I2 storage, logical expansion and mixed metadata indexes."""
from pathlib import Path
import ctypes as C
import hashlib
import json
import os
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/scaled-reader-test'
OUT.mkdir(parents=True,exist_ok=True)
FIX=OUT/'fixtures';FIX.mkdir(exist_ok=True)
cc=os.environ.get('CC','C:/msys64/ucrt64/bin/gcc.exe')
dll=OUT/'resources.dll'
env=os.environ.copy();env['PATH']=str(Path(cc).parent)+';'+env.get('PATH','')
subprocess.run([cc,'-shared','-O2','-Wall','-Wextra',str(ROOT/'platform/resources/h1_resources.c'),'-o',str(dll)],check=True,env=env,timeout=45)
lib=C.CDLL(str(dll))
class Info(C.Structure):
    _fields_=[(n,C.c_uint32) for n in ('width','height','channels','flags','pixel_bytes','storage_width','storage_height')]
class Image(C.Structure):
    _fields_=[('file',C.c_void_p),('info',Info),('alpha_color',C.c_uint32),
              ('rgb_source_bytes',C.c_uint32),('rgb_next_byte',C.c_uint32),('rgb_stream_valid',C.c_uint32),('payload_offset',C.c_uint32)]
lib.h1_resources_set_root.argtypes=[C.c_char_p]
lib.h1_image_open.argtypes=[C.POINTER(Image),C.c_char_p,C.c_int,C.c_uint32]
lib.h1_image_close.argtypes=[C.POINTER(Image)]
lib.h1_image_probe.argtypes=[C.c_char_p,C.c_int,C.POINTER(Info)]
for n in ('h1_image_read_span','h1_image_read_storage_span'):
    getattr(lib,n).argtypes=[C.POINTER(Image),C.c_uint32,C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
for n in ('h1_image_load_all','h1_image_load_storage'):
    getattr(lib,n).argtypes=[C.POINTER(Image),C.POINTER(C.c_uint32),C.c_size_t]
lib.h1_image_read_rgb_bytes.argtypes=[C.POINTER(Image),C.c_uint32,C.c_uint32,C.POINTER(C.c_ubyte)]
checks=0;records={}
def check(value):
    global checks
    assert value
    checks+=1
def root(path=FIX):
    lib.h1_resources_set_root((path.as_posix()+'/').encode())
def fixture(name,channels,scaled=True,flags=None):
    w,h=7,5;sw,sh=(3,2) if scaled else (w,h)
    flags=(1 if channels==1 else 0) if flags is None else flags
    raw=bytes((i*37+11)%256 for i in range(sw*sh*channels))
    header=struct.pack('<4s7I',b'H1I2',w,h,channels,flags,len(raw),sw,sh) if scaled else struct.pack('<4s5I',b'H1I1',w,h,channels,flags,len(raw))
    (FIX/name).write_bytes(header+raw)
    records[name]=(w,h,channels,flags,len(raw),sw,sh)
    color=0x13579b
    pixels=[]
    for i in range(sw*sh):
        p=raw[i*channels:(i+1)*channels]
        pixels.append((p[0]<<24)|color if channels==1 else ((p[3] if channels==4 else 255)<<24)|(p[0]<<16)|(p[1]<<8)|p[2])
    return w,h,sw,sh,raw,pixels
root()
for scaled in (False,True):
    for channels in (1,3,4):
        name=f'image-{int(scaled)}-{channels}.h1i'
        w,h,sw,sh,raw,pixels=fixture(name,channels,scaled)
        image=Image();check(lib.h1_image_open(C.byref(image),name[:-4].encode(),0,0x13579b)==1)
        check((image.info.width,image.info.height,image.info.storage_width,image.info.storage_height)==(w,h,sw,sh))
        check(image.payload_offset==(32 if scaled else 24))
        physical=(C.c_uint32*(sw*sh))();check(lib.h1_image_load_storage(C.byref(image),physical,len(physical))==1)
        check(list(physical)==pixels)
        logical=(C.c_uint32*(w*h))();check(lib.h1_image_load_all(C.byref(image),logical,len(logical))==1)
        expected=[pixels[(y*sh//h)*sw+x*sw//w] for y in range(h) for x in range(w)]
        check(list(logical)==expected)
        for y in range(h):
            for x in range(w):
                span=(C.c_uint32*(w-x))();check(lib.h1_image_read_span(C.byref(image),x,y,w-x,span)==1)
                check(list(span)==expected[y*w+x:(y+1)*w])
        small=(C.c_uint32*2)();check(lib.h1_image_read_storage_span(C.byref(image),sw-1,0,2,small)==0)
        check(lib.h1_image_load_storage(C.byref(image),small,1)==0)
        if channels==3:
            rgb=(C.c_ubyte*len(raw))();check(lib.h1_image_read_rgb_bytes(C.byref(image),0,len(raw),rgb)==1)
            check(bytes(rgb)==raw)
        lib.h1_image_close(C.byref(image))
# Auto-alpha variant and immutable logical geometry.
fixture('pair.h1i',3,True);fixture('pair.h1a',4,True)
image=Image();check(lib.h1_image_open(C.byref(image),b'pair',1,0xffffff)==1)
check(image.info.channels==4);lib.h1_image_close(C.byref(image))
# H1X2 describes both old and new payloads; invalid indexes must fall back.
names=bytearray();entries=[]
for name,values in sorted(records.items()):
    entries.append(struct.pack('<8I',len(names),*values));names+=name.encode()+b'\0'
body=b''.join(entries)+names
fnv=2166136261
for b in body:fnv=((fnv^b)*16777619)&0xffffffff
index=struct.pack('<4s6I',b'H1X2',2,len(entries),32,len(names),len(body),fnv)+body
(FIX/'image-metadata.h1x').write_bytes(index);root()
for name,values in sorted(records.items()):
    info=Info();check(lib.h1_image_probe(name[:-4].encode(),name.endswith('.h1a'),C.byref(info))==1)
    check(tuple(getattr(info,n) for n,_ in Info._fields_)==values)
(FIX/'image-metadata.h1x').write_bytes(index[:-1]+bytes([index[-1]^1]));root()
info=Info();check(lib.h1_image_probe(b'image-1-4',0,C.byref(info))==1)
check((info.storage_width,info.storage_height)==(3,2))
# Old 24-byte records remain independently usable.
old_names=bytearray();old_entries=[]
for name,values in sorted(records.items()):
    if not name.startswith('image-0-'):continue
    old_entries.append(struct.pack('<6I',len(old_names),*values[:5]));old_names+=name.encode()+b'\0'
old_body=b''.join(old_entries)+old_names;old_hash=2166136261
for b in old_body:old_hash=((old_hash^b)*16777619)&0xffffffff
(FIX/'image-metadata.h1x').write_bytes(struct.pack('<4s6I',b'H1X1',1,len(old_entries),24,len(old_names),len(old_body),old_hash)+old_body);root()
for channels in (1,3,4):
    info=Info();check(lib.h1_image_probe(f'image-0-{channels}'.encode(),0,C.byref(info))==1)
    check((info.width,info.height,info.storage_width,info.storage_height)==(7,5,7,5))
for case,header in {
    'truncated':b'H1I2'+bytes(23),
    'zero-storage':struct.pack('<4s7I',b'H1I2',7,5,4,0,0,0,2),
    'oversize-storage':struct.pack('<4s7I',b'H1I2',7,5,4,0,64,8,2),
    'bad-size':struct.pack('<4s7I',b'H1I2',7,5,4,0,23,3,2),
}.items():
    (FIX/(case+'.h1i')).write_bytes(header);image=Image()
    check(lib.h1_image_open(C.byref(image),case.encode(),0,0xffffff)==0)
root()
report={'all_passed':True,'checks':checks,'formats':['H1I1','H1I2','H1X2 mixed'],
        'logical_geometry_unchanged':True,'per_buffer_stack_scratch_bytes':3072,'nested_decoder_scratch_bytes':6144,
        'reader_sha256':hashlib.sha256((ROOT/'platform/resources/h1_resources.c').read_bytes()).hexdigest()}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
