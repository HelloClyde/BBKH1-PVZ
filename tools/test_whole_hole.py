"""Test guarded V1.41 descriptor headroom and whole-hole allocation logic."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
KERNEL_SHA256='d05786e442f9aad62a8d0a0cb4f6d786bdc7c2fa353a7a2b152c9ed9f01b40ef'

def expected_words():
    source=(ROOT/'runtime/whole_hole.c').read_text()
    words={int(a,16):int(b,16) for a,b in re.findall(r'\{(0x[0-9a-f]+)u,(0x[0-9a-f]+)u\}',source)}
    words.update({0x802aac54:0x800dc998,0x802aac58:0x800dbb54,
                  0x800dc710:0x3c0481c0,0x800dc764:0x3c0583c0})
    return words

def verify_kernel(path):
    payload=Path(path).read_bytes()
    digest=hashlib.sha256(payload).hexdigest()
    if digest!=KERNEL_SHA256:raise ValueError('Only the pinned V1.41 firmware ABI is supported')
    for address,expected in expected_words().items():
        actual=struct.unpack_from('<I',payload,address-0x80004000)[0]
        if actual!=expected:raise ValueError(f'Heap ABI word changed at {address:08x}')
    return {'evidence':'verified-pinned-kernel','sha256':digest,'checked_words':len(expected_words())}

def execute(command):
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return result.stdout

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default=shutil.which('gcc') or 'gcc')
    ap.add_argument('--kernel',type=Path)
    args=ap.parse_args()
    out=ROOT/'build/whole-hole-test';out.mkdir(parents=True,exist_ok=True)
    sources=[ROOT/'runtime/whole_hole.c',ROOT/'tools/test_whole_hole.c']
    flags=['-std=c11','-Wall','-Wextra','-Werror','-O2','-DPVZ_WHOLE_HOLE_TEST']
    exe=out/'test-whole-hole.exe';execute([args.cc,*flags,*sources,'-o',exe])
    output=execute([exe]).strip()
    report={'passed':True,'stdout':output,'flags':flags,'payload_tail_reserve_bytes':65536,
            'descriptor_floor_bytes':8192,'layout_trials':2000,'small_split_regression_allocations':383,
            'sources':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
            'firmware':verify_kernel(args.kernel) if args.kernel else {'evidence':'pinned-contract-not-rechecked','expected_sha256':KERNEL_SHA256},
            'checks':['whole-hole-best-fit','rounding-and-actual-span-accounting','original-free-neighbour-and-tail-coalescing',
                      'ordinary-SDK-first-fit-prediction','safe-bump-default-path','unsafe-bump-denied',
                      'no-fit-pressure-path','independent-fopen-522-byte-native-allocation',
                      'ABI/table/semaphore-rejection','full-layout-validation-before-write',
                      'invalid-request/header/record-and-changed-metadata-no-write']}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(output)

if __name__=='__main__':main()
