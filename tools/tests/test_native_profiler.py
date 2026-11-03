"""Check stage bookkeeping, same-stage nesting, RAII and 32/64-bit wrap offline."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parents[2]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw-ticks',action='store_true')
    parser.add_argument('--timers-only',action='store_true',help='Skip historical renderer-wrapper comparison when independent renderer changes are expected')
    args=parser.parse_args()
    out=ROOT/'build/native-profiler-test';out.mkdir(parents=True,exist_ok=True)
    probe=out/'probe.cpp'
    probe.write_text(r'''
#include "profiler.h"
#include <cassert>
#include <cstdio>
static uint32_t count,raw;
#ifdef PVZ_NATIVE_PROFILE_RAW_TICKS
#define timer_value raw
#else
#define timer_value count
#endif
extern "C" uint32_t pvz_profile_test_count(void){return count;}
extern "C" uint32_t pvz_runtime_ticks(void){return raw;}
static int early(bool value){PVZProfileScope s(PVZ_PROFILE_UPDATE);timer_value+=7;if(value)return 3;return 4;}
static void throws(){PVZProfileScope s(PVZ_PROFILE_UPDATE);timer_value+=11;throw 5;}
int main(){
    uint32_t start;auto d=pvz_runtime_profile_diagnostic;
    timer_value=100;start=pvz_profile_enter(PVZ_PROFILE_UPDATE);timer_value=125;pvz_profile_leave(PVZ_PROFILE_UPDATE,start);
    assert(d[0]==1&&d[1]==25&&d[2]==0&&d[3]==25);
    timer_value=1000;assert(early(true)==3);assert(d[0]==2&&d[1]==32);
    try{throws();assert(false);}catch(int n){assert(n==5);}assert(d[0]==3&&d[1]==43);
    timer_value=200;{PVZProfileScope a(PVZ_PROFILE_UPDATE);timer_value=205;{PVZProfileScope b(PVZ_PROFILE_UPDATE);timer_value=220;}timer_value=240;}
    assert(d[0]==4&&d[1]==83&&d[3]==40);
    timer_value=300;{PVZProfileScope a(PVZ_PROFILE_AUDIO);timer_value=320;{PVZProfileScope b(PVZ_PROFILE_MUSIC);timer_value=350;}timer_value=400;}
    assert(d[PVZ_PROFILE_AUDIO*4]==1&&d[PVZ_PROFILE_AUDIO*4+1]==100);
    assert(d[PVZ_PROFILE_MUSIC*4]==1&&d[PVZ_PROFILE_MUSIC*4+1]==30);
    timer_value=700;{PVZProfileScope a(PVZ_PROFILE_SW_DRAW_SHAPE);timer_value=730;}
    assert(d[PVZ_PROFILE_SW_DRAW_SHAPE*4]==1&&d[PVZ_PROFILE_SW_DRAW_SHAPE*4+1]==30);
    unsigned row=PVZ_PROFILE_PRESENT*4;d[row+1]=0xfffffff8u;d[row+2]=3;
    timer_value=0xfffffff0u;start=pvz_profile_enter(PVZ_PROFILE_PRESENT);timer_value=0;pvz_profile_leave(PVZ_PROFILE_PRESENT,start);
    assert(d[row]==1&&d[row+1]==8&&d[row+2]==4&&d[row+3]==16);
    start=pvz_profile_enter(99);pvz_profile_leave(99,start);pvz_profile_leave(PVZ_PROFILE_FRAME,0);
    assert(!d[PVZ_PROFILE_FRAME*4]);
    auto c=pvz_runtime_profile_clock;
#ifdef PVZ_NATIVE_PROFILE_RAW_TICKS
    raw=0xfffffff0u;pvz_profile_mark_frame();
    assert(c[0]==2&&c[0]==c[5]&&c[1]==raw&&c[2]==raw&&c[3]==0&&c[4]==40);
    raw=0x20;pvz_profile_mark_frame();
    assert(c[0]==4&&c[0]==c[5]&&c[1]==raw&&c[2]==raw&&c[3]==1);
#else
    count=0xfffffff0u;raw=100;pvz_profile_mark_frame();
    assert(c[0]==2&&c[0]==c[5]&&c[1]==100&&c[2]==0xfffffff0u&&c[3]==0&&c[4]==40);
    count=0x20;raw=140;pvz_profile_mark_frame();
    assert(c[0]==4&&c[0]==c[5]&&c[1]==140&&c[2]==0x20&&c[3]==1);
#endif
    puts("PASS profiler RAII/earlyreturn/exception/same-stage nesting/inclusive child/32bit delta/64bit total carry/clock sequence+wrap");
}
''',encoding='utf8')
    flags=['-O2','-Wall','-Wextra','-Werror','-DPVZ_NATIVE_PROFILE','-DPVZ_PROFILE_TEST',
           '-DPVZ_H1_RAW_TICK_HZ=40','-I',str(ROOT/'runtime')]
    if args.raw_ticks:flags.append('-DPVZ_NATIVE_PROFILE_RAW_TICKS')
    obj=out/'profiler.o';exe=out/'probe.exe'
    commands=[['gcc','-std=c11',*flags,'-c',str(ROOT/'runtime/profiler.c'),'-o',str(obj)],
              ['g++','-std=c++20',*flags,str(probe),str(obj),'-o',str(exe)]]
    for command in commands:subprocess.run(command,check=True)
    run=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
    proof={}
    for filename in ([] if args.timers_only else ['h1_host.cpp','h1_sound.cpp','h1_music.cpp','graphics/SoftwareRaster.cpp']):
        current=(ROOT/'platform'/filename).read_text(encoding='utf8')
        restored=current.replace('#ifdef PVZ_NATIVE_PROFILE\n#include "profiler.h"\n#endif\n','')
        for stage in ['AUDIO','MUSIC','REPEAT_TRIANGLE']:
            restored=restored.replace(f'#ifdef PVZ_NATIVE_PROFILE\n    PVZProfileScope profile(PVZ_PROFILE_{stage});\n#endif\n','')
        if filename=='h1_host.cpp':
            restored=restored.replace('    {\n#ifdef PVZ_NATIVE_PROFILE\n    PVZProfileScope profile(PVZ_PROFILE_PRESENT);\n#endif\n','',1)
            restored=restored.replace('    }\n    pvzp_diagnostic[2]++;pvzp_audio_pump();','    pvzp_diagnostic[2]++;pvzp_audio_pump();',1)
            restored=restored.replace('#ifdef PVZ_NATIVE_PROFILE\n    pvz_profile_mark_frame();\n#endif\n','')
        rel='platform/'+filename
        baseline=json.loads((ROOT/'dist/hardware-opt-pool/H1PVZPortable.build.json').read_text(encoding='utf8'))['source_inputs'][rel]
        digest=hashlib.sha256(restored.encode()).hexdigest()
        assert digest==baseline,(filename,digest,baseline)
        proof[rel]=dict(remove_only_diagnostic_wrappers_matches_pool_sha256=baseline,
                       current_sha256=hashlib.sha256((ROOT/rel).read_bytes()).hexdigest())
    report=dict(passed=True,commands=commands,output=run.stdout.strip(),wrapper_equivalence=proof,
                inputs={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in [ROOT/'runtime/profiler.c',ROOT/'runtime/profiler.h',Path(__file__)]},
                device_access_performed=False,real_counter_frequency_assumed=False)
    report['timer_backend']='native_raw_ticks' if args.raw_ticks else 'synthetic_CP0_model'
    report['historical_renderer_comparison_skipped']=args.timers_only
    (out/('raw-ticks-report.json' if args.raw_ticks else 'report.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(run.stdout.strip())
    if not args.timers_only:print('PASS four platform wrappers reverse to frozen pool source byte-for-byte')


if __name__=='__main__':main()
