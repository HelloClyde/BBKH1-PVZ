"""Run the production ring retirement code with a fixed-address SDK cursor."""
from pathlib import Path
import hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def function(text,name):
 start=text.index(name);at=text.index('{',start);depth=1;i=at+1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]
def main():
 source=ROOT/'platform/h1_pcm.cpp';text=source.read_text();out=ROOT/'build/pcm-tail-test';out.mkdir(parents=True,exist_ok=True)
 code=r'''
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cassert>
#define RING_SAMPLES 32768u
struct PcmDescriptor {int16_t* pcm;uint32_t bytes,private_words[6];};
static PcmDescriptor descriptor;
static int16_t* ring;
static uint32_t queueWords[4],*queue=queueWords;
static uint32_t now,last_retire_ms,pvzh1_audio_diagnostic[16],pvzp_diagnostic[32];
static unsigned used,read_position,queued,pending_nonzero;
static int active=1,playing;
static uint32_t pvzp_ticks_ms(){return now;}
static int submit(int,void*,int,int){return 1;}
'''
 code+='\n'+function(text,'static void reset_ring(')+'\n'+function(text,'static void retire(')+'\n'+function(text,'extern "C" void pvzp_pcm_idle(')
 code+=r'''
static void setup(unsigned consumed,unsigned queuedCount,unsigned elapsed){
 memset(ring,0,RING_SAMPLES*2);memset(pvzh1_audio_diagnostic,0,sizeof(pvzh1_audio_diagnostic));
 auto* node=(uint32_t*)uintptr_t(0x81000000);queue[0]=0x81000000;
 node[1]=(uint32_t)(uintptr_t)&descriptor;node[2]=0x82000000+consumed*2;
 used=queued=queuedCount;read_position=0;pending_nonzero=2;ring[0]=ring[1]=17;
 playing=1;last_retire_ms=1000;now=1000+elapsed;
}
int main(){
 assert(VirtualAlloc((void*)uintptr_t(0x81000000),65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 ring=(int16_t*)VirtualAlloc((void*)uintptr_t(0x82000000),65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(ring);
 for(unsigned elapsed: {25u,100u,350u}){
  setup(200,100,elapsed);retire();assert(pvzh1_audio_diagnostic[2]==1&&!playing); // An active source ran dry.
  setup(200,100,elapsed);pvzp_pcm_idle();assert(!pvzh1_audio_diagnostic[2]&&!playing); // A finite source finished.
  setup(100,100,elapsed);pvzp_pcm_idle();assert(!playing&&pvzh1_audio_diagnostic[5]==1); // Exact end also stops.
  setup(16,100,elapsed);pvzp_pcm_idle();assert(!playing&&pvzh1_audio_diagnostic[5]==1); // Trailing zero fill stops.
 }
 setup(200,100,1100);retire(true);assert(pvzh1_audio_diagnostic[1]==1&&!playing); // A missed complete ring is never hidden.
 setup(16,100,25);retire();assert(playing&&queued==84&&!pending_nonzero&&!pvzh1_audio_diagnostic[2]);
 VirtualFree(ring,0,MEM_RELEASE);VirtualFree((void*)uintptr_t(0x81000000),0,MEM_RELEASE);
}
'''
 code='#include <initializer_list>\n'+code;(out/'probe.cpp').write_text(code,encoding='utf8')
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 subprocess.run(['C:/msys64/ucrt64/bin/g++.exe','-std=c++20','-O2','-fpermissive',str(out/'probe.cpp'),'-o',str(out/'probe.exe')],env=env,check=True,capture_output=True)
 subprocess.run([str(out/'probe.exe')],env=env,check=True)
 proof=dict(passed=True,production_function_bodies=True,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),cases=14,active_underrun_detected=True,finite_end_stops=True,full_wrap_still_detected=True)
 (out/'verification.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
if __name__=='__main__':main()
