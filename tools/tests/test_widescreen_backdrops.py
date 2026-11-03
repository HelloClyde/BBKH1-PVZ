"""Isolated actual-engine fixture; no device access or gameplay state edits."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--host',type=Path,default=ROOT/'build/host-widescreen');ap.add_argument('--output',type=Path,default=ROOT/'build/wide-backdrops-engine');args=ap.parse_args()
    host=args.host.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    report=json.loads((host/'compile-report.json').read_text());assert report['linked'] and report['failed']==0
    cxx='C:/msys64/ucrt64/bin/g++.exe';env=os.environ.copy();env['PATH']=str(Path(cxx).parent)+os.pathsep+env.get('PATH','')
    for n in ('run','save','tmp'):(out/n).mkdir(exist_ok=True)
    env['TEMP']=env['TMP']=str(out/'tmp')
    source=(ROOT/'platform/host/host_io.cpp').read_text(encoding='utf8')
    anchor='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();}';assert source.count(anchor)==1
    source='extern "C" void pvzh1_widescreen_backdrops_probe();\n'+source.replace(anchor,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_widescreen_backdrops_probe();std::exit(0);}')
    (out/'observer.cpp').write_text(source,encoding='utf8')
    objects=[]
    for row in report['records']:
        name=row['source'];assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==row['sha256'],name
        if name=='platform/host/host_io.cpp':continue
        objects.append(host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o'))
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),env=env,capture_output=True,text=True,encoding='utf8',errors='replace');(out/name).write_text(r.stdout+r.stderr,encoding='utf8')
        if r.returncode:raise RuntimeError(str(r.returncode)+': '+str(out/name))
    for p in (out/'observer.cpp',ROOT/'tools/tests/widescreen_backdrops_probe.cpp'):
        obj=out/(p.stem+'.o');run([cxx,*report['flags'],'-std=c++20','-c',p,'-o',obj],p.stem+'.log');objects.append(obj)
    exe=out/'probe.exe';run([cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],'link.log')
    env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-wide').as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100')
    for n in list(env):
        if n.startswith('PVZ_HOST_') and n not in ('PVZ_HOST_OUTPUT','PVZ_HOST_MAX_FRAMES','PVZ_HOST_SPEED'):del env[n]
    run([exe],'console.log');result=json.loads((out/'run/widescreen-backdrops-verification.json').read_text());assert result['passed'];print(json.dumps(result))
if __name__=='__main__':main()
