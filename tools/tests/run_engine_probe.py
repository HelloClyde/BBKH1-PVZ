"""Link a checkpoint probe against an identified complete host game build."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,shutil
ROOT=Path(__file__).resolve().parents[2]

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--host',type=Path,required=True);p.add_argument('--probe',type=Path,required=True)
 p.add_argument('--entry',required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--resources',type=Path,default=ROOT/'private/resources-h1-wide')
 p.add_argument('--wrap',action='append',default=[],help='Observe a production C reader through a test-only linker wrapper')
 p.add_argument('--timeout',type=float,default=180,help='Timeout for a complete offline asset compilation or component probe')
 a=p.parse_args();host=a.host.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'build');out.mkdir(parents=True,exist_ok=False)
 assert a.entry.replace('_','').isalnum()
 for n in ('run','save'):(out/n).mkdir()
 sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
 report=json.loads((host/'compile-report.json').read_text());assert report['linked'] and not report['failed']
 shutil.copyfile(host/'compile-report.json',out/'host-compile-report.json')
 objects=[]
 for row in report['records']:
  name=row['source'];assert sha(ROOT/name)==row['sha256'],name
  if name!='platform/host/host_io.cpp':objects.append(host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o'))
 io=(ROOT/'platform/host/host_io.cpp').read_text();needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}';assert io.count(needle)==1
 io='extern "C" void '+a.entry+'();\n'+io.replace(needle,'if(std::strcmp(checkpoint,"loading-complete")==0){'+a.entry+'();std::exit(0);}')
 (out/'observer.cpp').write_text(io,encoding='utf8');env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');cxx='C:/msys64/ucrt64/bin/g++.exe'
 for src in (out/'observer.cpp',a.probe.resolve()):
  obj=out/(src.stem+'.o');r=subprocess.run([cxx,*report['flags'],'-std=c++20','-c',str(src),'-o',str(obj)],env=env,capture_output=True,text=True,encoding="utf8",errors="replace")
  (out/(src.stem+'.compile.log')).write_text(r.stdout+r.stderr,encoding='utf8');assert not r.returncode,r.stderr;objects.append(obj)
 assert all(s.replace('_','').isalnum() for s in a.wrap)
 exe=out/'probe.exe';r=subprocess.run([cxx,'-Wl,--gc-sections',*('-Wl,--wrap='+s for s in a.wrap),'-static-libstdc++','-static-libgcc','-o',str(exe),*map(str,objects),'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],env=env,capture_output=True,text=True,encoding="utf8",errors="replace")
 (out/'link.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 env.update(PVZ_RESOURCE_DIR=a.resources.resolve().as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_MAX_FRAMES='5',PVZ_HOST_SPEED='100')
 with (out/'console.log').open('w') as f:r=subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=f,stderr=f,timeout=a.timeout)
 assert not r.returncode,f'Probe exit {r.returncode}: {out}/console.log'
 (out/'identity.json').write_text(json.dumps(dict(host_report=sha(host/'compile-report.json'),probe=sha(a.probe),executable=sha(exe),test_linker_wrappers=a.wrap),indent=2)+'\n')
 for f in (out/'run').glob('*verification.json'):print(f.read_text())

if __name__=='__main__':main()
