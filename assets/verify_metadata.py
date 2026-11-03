"""Exercise optional metadata lookup and fallback against every image header."""
from pathlib import Path
import ctypes
import hashlib
import json
import shutil
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'private/resources'
BUILD=ROOT/'private/metadata-verification'


def checksum(data):
    value=2166136261
    for byte in data:value=((value^byte)*16777619)&0xffffffff
    return value


def main():
    BUILD.mkdir(parents=True,exist_ok=True)
    library=BUILD/'metadata-loader.dll'
    subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror','-shared','-static-libgcc',
                    str(ROOT/'platform/resources/h1_resources.c'),'-o',str(library)],check=True)
    api=ctypes.CDLL(str(library))
    class Info(ctypes.Structure):
        _fields_=[(name,ctypes.c_uint32) for name in ['width','height','channels','flags','pixel_bytes']]
    api.h1_resources_set_root.argtypes=[ctypes.c_char_p]
    api.h1_image_probe.argtypes=[ctypes.c_char_p,ctypes.c_int,ctypes.POINTER(Info)]
    api.h1_image_probe.restype=ctypes.c_int
    images=sorted(p for p in SOURCE.rglob('*') if p.is_file() and p.suffix in ['.h1i','.h1a'])
    cases=[]
    for path in images:
        with path.open('rb') as stream:header=stream.read(24)
        magic,*expected=struct.unpack('<4s5I',header)
        assert magic==b'H1I1'
        logical=path.relative_to(SOURCE).with_suffix('').as_posix()
        cases.append((logical,path.suffix=='.h1a',tuple(expected)))
    data=(SOURCE/'image-metadata.h1x').read_bytes()
    index_only=BUILD/'index-only';index_only.mkdir(exist_ok=True)
    (index_only/'image-metadata.h1x').write_bytes(data)
    fallback=BUILD/'fallback';fallback.mkdir(exist_ok=True)
    for path in images:
        target=fallback/path.relative_to(SOURCE);target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists():target.hardlink_to(path)
    def check(folder,items=cases):
        api.h1_resources_set_root(str(folder.resolve()).encode())
        for logical,auto,expected in items:
            info=Info()
            # Normalization is tested for every image, independently of index
            # names. Fonts also pass an already-rooted original filename.
            query=logical.upper().replace('/','\\')
            if not api.h1_image_probe(query.encode(),int(auto),ctypes.byref(info)):
                raise AssertionError('Missing probe: '+query)
            actual=tuple(getattr(info,name) for name,_ in Info._fields_)
            if actual!=expected:raise AssertionError((query,actual,expected))
        for query in ['../../outside','images/absent_metadata_fixture']:
            if api.h1_image_probe(query.encode(),1,ctypes.byref(Info())):raise AssertionError(query)
        logo=Info()
        if not api.h1_image_probe(b'properties/partner_logo',1,ctypes.byref(logo)) or logo.flags!=2:
            raise AssertionError('Optional logo semantics changed')
        return len(items)
    results={}
    results['index_only_without_image_files']=check(index_only)
    # Every expected value was read directly from the original H1I1 header.
    index=fallback/'image-metadata.h1x'
    if index.exists():index.unlink()
    results['missing_index_falls_back']=check(fallback)
    corrupt=bytearray(data);corrupt[-2]^=0x40;index.write_bytes(corrupt)
    results['bad_checksum_falls_back']=check(fallback)
    index.write_bytes(data[:55]);results['truncated_index_falls_back']=check(fallback)
    payload=b'abcdef'
    index.write_bytes(struct.pack('<4s6I',b'H1X1',1,1,24,0xffffffee,6,checksum(payload))+payload)
    results['overflowing_header_lengths_fall_back']=check(fallback)
    invalid=bytearray(data);struct.pack_into('<I',invalid,28+4,0)
    struct.pack_into('<I',invalid,24,checksum(invalid[28:]));index.write_bytes(invalid)
    results['invalid_dimensions_with_valid_checksum_falls_back']=check(fallback)
    # A valid, incomplete index may occur during installation: a lookup miss
    # must still find its actual stream rather than report a missing image.
    magic,version,count,size,strings,body,crc=struct.unpack_from('<4s6I',data)
    names=data[28+count*size:]
    records=[data[28+i*size:28+(i+1)*size] for i in range(count)]
    omitted='images/background1.h1i'
    records=[r for r in records if names[struct.unpack_from('<I',r)[0]:].split(b'\0',1)[0].decode()!=omitted]
    payload=b''.join(records)+names
    index.write_bytes(struct.pack('<4s6I',magic,version,len(records),size,strings,len(payload),checksum(payload))+payload)
    results['valid_index_missing_entry_finds_real_file']=check(fallback)
    # Root changes release the old table, including a previous failure state.
    results['root_switch_reloads_valid_index']=check(index_only)
    report=dict(index_files=len(images),index_bytes=len(data),index_sha256=hashlib.sha256(data).hexdigest(),
                results=results,all_metadata_fields_match_original_headers=True,
                original_image_files_modified=False,optional_logo_and_missing_paths_verified=True,
                actual_image_decoder_fixture='verify_resources.py: 14 full ARGB/span/alpha comparisons')
    (ROOT/'assets/image-metadata-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
