"""Compile the real calibrated runtime delay/monotonic bodies at 40 and 80 Hz."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = ROOT / 'runtime/syscalls.c'
    text = source.read_text(encoding='utf8')
    delay = text[text.index('void pvz_runtime_delay_ms('):text.index('static void *native_allocate(')]
    clock = text[text.index('int clock_gettime('):text.index('int nanosleep(')]
    output = ROOT / 'build/native-time-tests'
    output.mkdir(parents=True, exist_ok=True)
    probe = output / 'probe.c'
    probe.write_text(r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "runtime.h"
typedef int clockid_t;
struct timespec { long long tv_sec; long tv_nsec; };
struct timeval { long long tv_sec; long tv_usec; };
#define CLOCK_MONOTONIC 4
static uint32_t raw_tick,last_tick,tick_high;
uint32_t pvz_runtime_ticks(void) { return raw_tick; }
int _gettimeofday(struct timeval *tv,void *zone) {(void)zone;tv->tv_sec=12;tv->tv_usec=345;return 0;}
''' + delay + clock + r'''
void pvz_runtime_pump(void) { raw_tick++; }
#define CHECK(x) do {if(!(x)){fprintf(stderr,"check line %d\n",__LINE__);return 1;}}while(0)
int main(void) {
    const unsigned requests[]={0,1,12,13,24,25,26,999,1000,1001,2500};
    struct timespec t;
    for(unsigned i=0;i<sizeof(requests)/sizeof(requests[0]);i++){
        raw_tick=0xfffffff0u;
        uint32_t begin=raw_tick;
        unsigned expected=(requests[i]*PVZ_H1_RAW_TICK_HZ+999u)/1000u;
        pvz_runtime_delay_ms(requests[i]);
        CHECK((uint32_t)(raw_tick-begin)==(expected?expected:1));
    }
    last_tick=tick_high=raw_tick=0;
    CHECK(clock_gettime(CLOCK_MONOTONIC,&t)==0&&t.tv_sec==0&&t.tv_nsec==0);
    raw_tick=PVZ_H1_RAW_TICK_HZ+1;
    CHECK(clock_gettime(CLOCK_MONOTONIC,&t)==0&&t.tv_sec==1&&t.tv_nsec==1000000000L/PVZ_H1_RAW_TICK_HZ);
    last_tick=0xfffffff0u;tick_high=0;raw_tick=16;
    CHECK(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    CHECK(t.tv_sec==(long long)((0x100000000ull+16)/PVZ_H1_RAW_TICK_HZ));
    CHECK(t.tv_nsec==(long)(((0x100000000ull+16)%PVZ_H1_RAW_TICK_HZ)*1000000000ull/PVZ_H1_RAW_TICK_HZ));
    CHECK(clock_gettime(CLOCK_MONOTONIC,0)==-1&&errno==EINVAL);
    CHECK(clock_gettime(0,&t)==0&&t.tv_sec==12&&t.tv_nsec==345000);
    printf("PASS runtime delay/monotonic/wrap/null/realtime branch at %d Hz\n",PVZ_H1_RAW_TICK_HZ);
    return 0;
}
''', encoding='utf8')
    cases = []
    for hz in (40, 80):
        exe = output / f'probe-{hz}.exe'
        command = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   f'-DPVZ_H1_RAW_TICK_HZ={hz}', '-I', str(ROOT / 'runtime'), str(probe), '-o', str(exe)]
        subprocess.run(command, check=True)
        run = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
        cases.append({'hz': hz, 'command': command, 'output': run.stdout.strip()})
        print(run.stdout.strip())
    report = dict(passed=True, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  header_sha256=hashlib.sha256((ROOT / 'runtime/runtime.h').read_bytes()).hexdigest(),
                  probe_sha256=hashlib.sha256(probe.read_bytes()).hexdigest(), cases=cases,
                  device_access_performed=False)
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
