"""Compare the actual original and compiled reverse-resource lookup bodies."""
from pathlib import Path
import json,os,subprocess,hashlib
ROOT=Path(__file__).resolve().parents[2]
def body(path):
 text=path.read_text(encoding='utf8');start=text.index('Sexy::ResourceId Sexy::GetIdByVariable(');at=text.index('{',start);i=at+1;depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]
def main():
 out=ROOT/'build/resource-id-test';out.mkdir(parents=True,exist_ok=True)
 original=body(ROOT/'upstream/src/Resources.cpp').replace('GetIdByVariable(','Reference(').replace('gNeedRecalcVariableToIdMap','needReference')
 current=body(ROOT/'build/host-offline2/source/Resources.cpp').replace('GetIdByVariable(','Candidate(').replace('gNeedRecalcVariableToIdMap','needCandidate')
 pre=r'''
#include <map>
#include <vector>
#include <utility>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <cstdio>
namespace Sexy {enum class ResourceId:int {RESOURCE_ID_MAX=1000};ResourceId Reference(void*);ResourceId Candidate(void*);}
using namespace Sexy;
static bool needReference=true,needCandidate=true;
static uintptr_t values[1000];static void* gResources[1000];static unsigned services;
extern "C" void pvzp_audio_service(){++services;}
'''
 run=r'''
int main(){unsigned checks=0;
 for(unsigned generation=0;generation<20;generation++){
  for(unsigned i=0;i<1000;i++){values[i]=(i%7==0)?0:((i%4==0)?(i%170):0x82000000u+((i+generation*31)%317)*16);gResources[i]=&values[i];}
  needReference=needCandidate=true;
  for(auto value:values){assert(Reference((void*)value)==Candidate((void*)value));++checks;}
  uint32_t random=0x71453+generation;for(unsigned i=0;i<10000;i++){random=random*1664525u+1013904223u;void* value=(void*)(uintptr_t)(random&0xfffffff0u);assert(Reference(value)==Candidate(value));++checks;}
  assert(Reference(nullptr)==Candidate(nullptr));++checks;
 }
 assert(services==20000);printf("{\"passed\":true,\"comparisons\":%u,\"rebuilds\":20,\"last_alias_ID_preserved\":true,\"null_and_unknown_pointers\":true}\n",checks);
}
'''
 source=out/'probe.cpp';source.write_text(pre+original+'\n'+current+run,encoding='utf8');exe=out/'probe.exe';env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 subprocess.run(['C:/msys64/ucrt64/bin/g++.exe','-std=c++20','-O2',str(source),'-o',str(exe)],env=env,check=True)
 r=subprocess.run([str(exe)],env=env,capture_output=True,text=True,check=True);d=json.loads(r.stdout);d['production_patch_sha256']=hashlib.sha256((ROOT/'platform/source_patches.py').read_bytes()).hexdigest();(out/'verification.json').write_text(json.dumps(d,indent=2)+'\n');print(r.stdout.strip())
if __name__=='__main__':main()
