"""Count work in the real host engine without changing production sources.

The controlled PCM sink drains on demand. Counts describe backend operations,
not H1 timing or a dense-battle FPS test.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',type=Path,default=ROOT/'build/host-offline2');p.add_argument('--output',type=Path,default=ROOT/'build/runtime-work-audit');a=p.parse_args()
 host=a.host.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'build');out.mkdir(parents=True,exist_ok=False)
 for n in ('run','save'):(out/n).mkdir()
 report=json.loads((host/'compile-report.json').read_text());assert report['failed']==0 and report['linked']
 replaced={'platform/host/host_io.cpp','platform/h1_sound.cpp','platform/graphics/MemoryImageCache.cpp'};objects=[]
 for row in report['records']:
  name=row['source'];assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==row['sha256'],name
  if name not in replaced:objects.append(host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o'))
 io=(ROOT/'platform/host/host_io.cpp').read_text();needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}';assert io.count(needle)==1
 io='extern "C" void pvzh1_work_audit();\nextern "C" int pvzh1_audit_drain;\n'+io.replace(needle,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_work_audit();std::exit(0);}')
 needle='extern "C" unsigned pvzp_pcm_writable(){';assert io.count(needle)==1;io=io.replace(needle,needle+'\n if(pvzh1_audit_drain)return 2048;')
 (out/'observer.cpp').write_text(io,encoding='utf8')
 sound=(ROOT/'platform/h1_sound.cpp').read_text();needle='using namespace Sexy;';assert sound.count(needle)==1
 hooks='''extern "C" FILE* pvzh1_audit_fopen(const char*,const char*);
extern "C" int pvzh1_audit_fseek(FILE*,long,int);
extern "C" size_t pvzh1_audit_fread(void*,size_t,size_t,FILE*);
extern "C" int pvzh1_audit_fclose(FILE*);
#define fopen pvzh1_audit_fopen
#define fseek pvzh1_audit_fseek
#define fread pvzh1_audit_fread
#define fclose pvzh1_audit_fclose
'''
 (out/'sound.cpp').write_text(sound.replace(needle,hooks+needle),encoding='utf8')
 cache=(ROOT/'platform/graphics/MemoryImageCache.cpp').read_text();needle='std::size_t Recount()\n{\n    Cache& cache=State();';assert cache.count(needle)==1
 cache='#include <cstddef>\nextern "C" void pvzh1_audit_recount(std::size_t);\n'+cache.replace(needle,needle+'pvzh1_audit_recount(cache.sources.size());')
 (out/'cache.cpp').write_text(cache,encoding='utf8')
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');cxx='C:/msys64/ucrt64/bin/g++.exe'
 for src in [out/'observer.cpp',out/'sound.cpp',out/'cache.cpp',ROOT/'tools/audit/runtime_work_probe.cpp']:
  obj=out/(src.stem+'.o');r=subprocess.run([cxx,*report['flags'],'-std=c++20','-c',str(src),'-o',str(obj)],env=env,capture_output=True,text=True)
  (out/(src.stem+'.compile.log')).write_text(r.stdout+r.stderr,encoding='utf8');assert r.returncode==0,r.stderr;objects.append(obj)
 exe=out/'audit.exe';r=subprocess.run([cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',str(exe),*map(str,objects),'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],env=env,capture_output=True,text=True);assert r.returncode==0,r.stderr
 env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-wide').as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_SPEED='100',PVZ_HOST_MAX_FRAMES='5')
 with (out/'console.log').open('w') as f:subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=f,stderr=f,check=True)
 result=json.loads((out/'run/runtime-work.json').read_text());result['identity']={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in sorted(replaced)}
 result['scope']='Real host backend operations with controlled PCM draining; no H1 latency/FPS or subjective audio claim. Production sources unchanged.'
 (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
