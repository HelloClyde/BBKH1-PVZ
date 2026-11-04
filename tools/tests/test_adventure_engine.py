"""Check offline hand poses against the complete original physical renderer."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);(out/'run').mkdir();(out/'save').mkdir()
 report=json.loads((a.host/'compile-report.json').read_text());objects=[]
 for row in report['records']:
  name=row['source'];assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==row['sha256'],name
  if name=='platform/host/host_io.cpp':continue
  objects.append(a.host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o'))
 io=(ROOT/'platform/host/host_io.cpp').read_text();needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}';assert io.count(needle)==1
 io='extern "C" void pvzh1_adventure_probe();\n'+io.replace(needle,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_adventure_probe();std::exit(0);}')
 (out/'observer.cpp').write_text(io,encoding='utf8');env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');cxx='C:/msys64/ucrt64/bin/g++.exe'
 for src in (out/'observer.cpp',ROOT/'tools/tests/adventure_engine_probe.cpp'):
  obj=out/(src.stem+'.o');subprocess.run([cxx,*report['flags'],'-std=c++20','-c',str(src),'-o',str(obj)],env=env,cwd=ROOT,check=True);objects.append(obj)
 exe=out/'probe.exe';subprocess.run([cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',str(exe),*map(str,objects),'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],env=env,cwd=ROOT,check=True)
 env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-wide').as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_SPEED='100',PVZ_HOST_MAX_FRAMES='5')
 with (out/'console.log').open('w') as log:subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=log,stderr=log,check=True)
 result=json.loads((out/'run/adventure-verification.json').read_text());assert result['passed'] and result['whole_frame_pixel_cases']==342;print(json.dumps(result))
if __name__=='__main__':main()
