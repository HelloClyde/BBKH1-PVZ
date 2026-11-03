"""Verify RGB paging against original image bytes and real C stdio failures."""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import subprocess
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'private/raw-rgb-verification'
RESOURCES=ROOT/'private/resources'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    BUILD.mkdir(parents=True,exist_ok=True)
    fixture=BUILD/'fixtures';fixture.mkdir(exist_ok=True)
    def h1(name,w,h,c,flags,data,magic=b'H1I1',declared=None):
        (fixture/(name+'.h1i')).write_bytes(struct.pack('<4s5I',magic,w,h,c,flags,w*h*c if declared is None else declared)+data)
    high=bytes([255,128,0,0,255,255,1,254,129,255,255,255,0,0,0,128,0,255])
    h1('autoalias',3,2,3,0,high)
    (fixture/'autoalias.h1a').write_bytes(struct.pack('<4s5I',b'H1I1',3,2,4,0,24)+bytes(range(24)))
    h1('rgba',3,2,4,0,bytes(range(24)))
    h1('mask',3,2,1,1,bytes(range(6)))
    h1('rgbmask',3,2,3,1,high)
    h1('badmagic',3,2,3,0,high,magic=b'PNG!')
    h1('badsize',3,2,3,0,high,declared=19)
    h1('badwidth',8193,1,3,0,b'')
    h1('badflags',3,2,3,4,high)
    h1('empty',0,2,3,0,b'')
    h1('truncated',4,2,3,0,high[:10])
    (fixture/'shortheader.h1i').write_bytes(b'H1I1\0')
    # Correctly checksummed but stale metadata may hint RGB; actual open wins.
    record=struct.pack('<6I',0,3,2,3,0,18);strings=b'rgba.h1i\0';body=record+strings
    fnv=2166136261
    for byte in body:fnv=((fnv^byte)*16777619)&0xffffffff
    (fixture/'image-metadata.h1x').write_bytes(struct.pack('<4s6I',b'H1X1',1,1,24,len(strings),len(body),fnv)+body)
    original=ROOT/'private/source/images/background3.jpg'
    converted=RESOURCES/'images/background3.h1i'
    data=converted.read_bytes();magic,w,h,c,flags,n=struct.unpack('<4s5I',data[:24])
    assert (magic,w,h,c,flags,n)==(b'H1I1',1400,600,3,0,2520000)
    expected=Image.open(original).convert('RGB').tobytes()
    assert data[24:]==expected
    expected_file=BUILD/'expected-rgb.bin';expected_file.write_bytes(expected)
    gcc=shutil.which('gcc');include=ROOT/'platform/resources';obj=BUILD/'h1_resources.o';exe=BUILD/'test_raw_rgb.exe'
    subprocess.run([gcc,'-std=c99','-Wall','-Wextra','-Werror','-Dfseek=pvz_test_fseek','-Dfread=pvz_test_fread',
                    '-I'+str(include),'-c',str(include/'h1_resources.c'),'-o',str(obj)],check=True)
    subprocess.run([gcc,'-std=c99','-Wall','-Wextra','-Werror','-static-libgcc','-I'+str(include),
                    str(ROOT/'assets/test_raw_rgb.c'),str(obj),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe),str(RESOURCES),str(fixture),str(expected_file),'images/background3'],
                          capture_output=True,text=True,check=True)
    print(result.stdout,end='');print(result.stderr,end='')
    report=json.loads(result.stdout)
    report.update(scope='Host C resource layer; original RGB bytes, no renderer/native claim',
                  source_rgb_sha256=hashlib.sha256(expected).hexdigest(),source_file_sha256=sha(original),
                  converted_file_sha256=sha(converted),resource_tree_modified=False,
                  actual_header_validation=True,stale_index_rejected_for_rgb=True,
                  rejection_cases=['RGBA','mask','RGB-mask-flags','optional-transparent','bad-magic','bad-size','bad-width',
                                   'unknown-flags','empty-dimensions','short-header','truncated-payload','unaligned-byte-range',
                                   'out-of-range','integer-overflow','null-buffer','closed-image'],
                  mixed_argb_read_resynchronization=True,short_read_retry=True,seek_failure_retry=True,
                  tested_files={p.relative_to(ROOT).as_posix():sha(p) for p in [include/'h1_resources.c',include/'h1_resources.h',
                                  ROOT/'assets/test_raw_rgb.c',ROOT/'assets/verify_raw_rgb.py']})
    legacy_cases=ROOT/'private/loader-verification/cases.txt'
    if legacy_cases.is_file():
        legacy_obj=BUILD/'legacy-h1-resources.o';legacy_exe=BUILD/'legacy-resource-loader.exe'
        framework=ROOT/'upstream/src/SexyAppFramework'
        subprocess.run([gcc,'-std=c99','-Wall','-Wextra','-Werror','-c',str(include/'h1_resources.c'),'-o',str(legacy_obj)],check=True)
        subprocess.run([shutil.which('g++'),'-std=c++20','-Wall','-Wextra','-Werror','-static-libstdc++','-static-libgcc',
                        '-I'+str(include),'-I'+str(framework),str(include/'ImageLib.cpp'),str(ROOT/'assets/test_resource_loader.cpp'),
                        str(legacy_obj),'-o',str(legacy_exe)],check=True)
        legacy=subprocess.run([str(legacy_exe),str(RESOURCES),str(legacy_cases)],capture_output=True,text=True,check=True)
        print(legacy.stdout,end='');print(legacy.stderr,end='')
        report.update(legacy_argb_fixture_result=legacy.stdout.strip(),legacy_argb_casefile_sha256=sha(legacy_cases),
                      legacy_argb_fixture_scope='C/C++ metadata/full ARGB/span/alpha comparisons against existing original-image expectations; no full tree readback')
        report['tested_files']['assets/test_resource_loader.cpp']=sha(ROOT/'assets/test_resource_loader.cpp')
        report['tested_files']['platform/resources/ImageLib.cpp']=sha(include/'ImageLib.cpp')
    else:
        report['legacy_argb_fixture_scope']='Not run: generate original expectations with assets/verify_resources.py first'
    (ROOT/'assets/raw-rgb-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
