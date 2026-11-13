"""Differential pixel oracle for the actual bulk-run kernel, including page edges."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'build/extended-runs-v55'
OUT.mkdir(exist_ok=True)
source = (ROOT/'platform/actors/ActorPose.cpp').read_text(encoding='utf8')
start = source.index('__attribute__((optimize("O3"))) static void DrawExtendedRuns(')
stop = source.index('\nstatic bool DrawFromCache(', start)
kernel = source[start:stop].replace('DrawExtendedRuns(', 'DrawExtendedCandidate(')
reference = (ROOT/'tools/tests/extended_runs_reference.cpp').read_text(encoding='utf8')
prefix = r'''
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <random>
static uint16_t LE16(const uint8_t* p){return p[0]|uint16_t(p[1])<<8;}
static uint32_t LE32(const uint8_t* p){return LE16(p)|uint32_t(LE16(p+2))<<16;}
static unsigned boundaries;
struct Reader {
 const std::vector<uint8_t>& data;unsigned offset;
 uint16_t U16(){assert(offset+2<=data.size());auto v=LE16(data.data()+offset);offset+=2;return v;}
 uint32_t U32(){assert(offset+4<=data.size());auto v=LE32(data.data()+offset);offset+=4;return v;}
 void Run(bool,int&x,int&y,unsigned&flags){x=int16_t(U16());y=int16_t(U16());flags=U16();}
 void Skip(unsigned n){assert(n<=data.size()-offset);offset+=n;}
 unsigned TakeSamples(unsigned n,unsigned width,const uint8_t*&out){
  unsigned count=std::min(n,(4096-(offset&4095))/width);out=data.data()+offset;
  assert(count*width<=data.size()-offset);offset+=count*width;if(!count)++boundaries;return count;
 }
};
struct {bool directLiterals;uint32_t*palette;} cache;
'''
driver = r'''
int main(){
 std::mt19937 rng(80319);uint32_t palette[512];for(auto&v:palette)v=rng();cache.palette=palette;
 std::vector<uint32_t> base(480*272),a(base.size()),b(base.size());for(auto&v:base)v=rng();
 constexpr unsigned edges[]={0,4085,4086,4087,4095,8191};
 unsigned comparisons=0;
 for(unsigned trial=0;trial<1536;++trial){
  bool direct=trial&1;cache.directLiterals=direct;unsigned offset=edges[(trial/16)%6];std::vector<uint8_t> data(offset);
  auto put16=[&](unsigned v){data.push_back(v);data.push_back(v>>8);};
  auto put32=[&](uint32_t v){put16(v);put16(v>>16);};
  unsigned nr=32;
  for(unsigned i=0;i<nr;++i){
   int x=i?int(rng()%620)-110:100,y=i?int(rng()%310)-20:100;
   unsigned n=1+rng()%100,kind=i?rng()%4:(trial/4)%4;bool constant=i?rng()%2:bool(trial&2);
   put16(x);put16(y);put16(n|(constant?4096:0)|(kind==1?8192:kind==2?16384:kind==3?32768:0));
   for(unsigned p=0;p<(constant?1:n);++p){if(direct)put32(rng());else put16(rng()%512);}
  }
  int dx=int(rng()%49)-24,dy=int(rng()%33)-16,cx=trial%3?0:80,cy=trial%3?0:64,cr=trial%3?480:355,cb=trial%3?272:203;
  a=base;b=base;Reader old{data,offset},fresh{data,offset};
  DrawExtendedReference(old,nr,a.data(),dx,dy,cx,cy,cr,cb);
  DrawExtendedCandidate(fresh,nr,b.data(),dx,dy,cx,cy,cr,cb);
  if(a!=b||old.offset!=data.size()||fresh.offset!=old.offset){std::fprintf(stderr,"extended-run mismatch trial=%u old=%u new=%u size=%zu\n",trial,old.offset,fresh.offset,data.size());return 1;}
  ++comparisons;
 }
 assert(boundaries);std::printf("PASS %u frame comparisons, all four exact blend modes, constants/literals, palette/direct, clip/translation, %u cross-page samples\n",comparisons,boundaries);
}
'''
cpp = OUT/'probe.cpp'
cpp.write_text(prefix+reference+kernel+driver, encoding='utf8')
env = os.environ.copy()
env['PATH'] = 'C:/msys64/ucrt64/bin;'+env.get('PATH', '')
exe = OUT/'probe.exe'
subprocess.run(['C:/msys64/ucrt64/bin/g++.exe', '-std=c++20', '-O2', '-Wall', '-Wextra',
                str(cpp), '-o', str(exe)], env=env, check=True, timeout=60)
run = subprocess.run([str(exe)], env=env, check=True, capture_output=True, text=True, timeout=60)
proof = dict(passed=True, scope='Actual production kernel versus frozen V54 oracle; host differential pixels',
             production_kernel_sha256=hashlib.sha256(kernel.encode()).hexdigest(),
             reference_sha256=hashlib.sha256(reference.encode()).hexdigest(), result=run.stdout.strip())
(OUT/'verification.json').write_text(json.dumps(proof, indent=2)+'\n', encoding='utf8')
print(run.stdout.strip())
