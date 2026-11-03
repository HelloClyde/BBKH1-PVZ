"""Build and run the original-order/libxmp and actual H1 Mix API host checks."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'private/libxmp-host'
MUSIC=ROOT/'private/music'


def main():
    lib=BUILD/'libxmp-lite.a'
    if not lib.exists():
        subprocess.run(['cmake','-S',str(ROOT/'vendor/libxmp'),'-B',str(BUILD),'-G','Ninja',
                        '-DBUILD_LITE=ON','-DBUILD_SHARED=OFF','-DLIBXMP_DISABLE_DEPACKERS=ON',
                        '-DLIBXMP_DISABLE_PROWIZARD=ON','-DLIBXMP_DISABLE_IT=ON','-DCMAKE_BUILD_TYPE=Release'],check=True)
        subprocess.run(['cmake','--build',str(BUILD),'--target','xmp_lite_static','-j','6'],check=True)
    gcc=shutil.which('gcc');gxx=shutil.which('g++')
    native=MUSIC/'test_module_playback.exe'
    subprocess.run([gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-DLIBXMP_STATIC',
                    '-I'+str(ROOT/'vendor/libxmp/include'),str(ROOT/'assets/test_module_playback.c'),
                    str(lib),'-lm','-o',str(native)],check=True)
    a=subprocess.run([str(native),str(MUSIC/'mainmusic.original.xm'),
                      str(ROOT/'private/resources/sounds/mainmusic.xm')],capture_output=True,text=True,check=True)
    api=MUSIC/'test_music_api.exe'
    shared=MUSIC/'xmp_shared.o'
    subprocess.run([gcc,'-std=c99','-O2','-DLIBXMP_CORE_PLAYER','-DLIBXMP_CORE_DISABLE_IT','-DLIBXMP_STATIC',
                    '-I'+str(ROOT/'vendor/libxmp/src'),'-I'+str(ROOT/'vendor/libxmp/include'),
                    '-I'+str(ROOT/'platform'),'-c',str(ROOT/'platform/xmp_shared.c'),'-o',str(shared)],check=True)
    subprocess.run([gxx,'-std=c++20','-O2','-static-libstdc++','-static-libgcc','-DLIBXMP_STATIC',
                    *('-I'+str(ROOT/p) for p in ['platform/include','platform',
                                               'upstream/src/SexyAppFramework','vendor/libxmp/include']),
                    str(ROOT/'assets/test_music_api.cpp'),str(ROOT/'platform/h1_music.cpp'),
                    str(shared),str(lib),'-lm','-o',str(api)],check=True)
    b=subprocess.run([str(api),str(ROOT/'private/resources')],capture_output=True,text=True)
    if b.returncode:
        print(b.stdout,end='');print(b.stderr,end='');b.check_returncode()
    report=dict(result='passed',native_order_frames=14*300,native_order_and_controls=a.stdout,
                h1_mix_api=b.stdout,
                music_source_sha256=hashlib.sha256((ROOT/'platform/h1_music.cpp').read_bytes()).hexdigest(),
                shared_helper_source_sha256=hashlib.sha256((ROOT/'platform/xmp_shared.c').read_bytes()).hexdigest(),
                api_fixture_source_sha256=hashlib.sha256((ROOT/'assets/test_music_api.cpp').read_bytes()).hexdigest(),
                module_sha256=hashlib.sha256((ROOT/'private/resources/sounds/mainmusic.xm').read_bytes()).hexdigest(),
                scope='Host libxmp-lite original-vs-H1 XM order sequencing and actual H1 Mix API; no physical H1 audio claim')
    (ROOT/'assets/music-playback-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(a.stdout,end='');print(b.stdout,end='')


if __name__=='__main__':main()
