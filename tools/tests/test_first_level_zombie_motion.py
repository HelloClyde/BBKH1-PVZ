"""Exercise the real opening, first-level zombie movement, and options pause."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT=Path(__file__).resolve().parents[2]

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host',type=Path,default=ROOT/'build/host-first-level-fix')
    ap.add_argument('--output',type=Path,default=ROOT/'build/first-level-zombie-motion')
    args=ap.parse_args();host=args.host.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    for name in ('tmp','save','run'):(out/name).mkdir(exist_ok=True)
    report=json.loads((host/'compile-report.json').read_text(encoding='utf8'))
    assert report['linked'] and not report['failed']
    env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');env['TEMP']=env['TMP']=str(out/'tmp')
    objects=[]
    for item in report['records']:
        name=item['source'];assert sha(ROOT/name)==item['sha256'],name
        if name!='platform/host/host_io.cpp':objects.append(host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o'))
    io=(ROOT/'platform/host/host_io.cpp').read_text(encoding='utf8')
    anchor='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}'
    assert io.count(anchor)==1
    io='extern "C" void pvzh1_first_level_zombie_motion_probe();\n'+io.replace(anchor,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_first_level_zombie_motion_probe();std::exit(0);}')
    observer=out/'observer.cpp';observer.write_text(io,encoding='utf8')
    probe=ROOT/'tools/tests/first_level_zombie_motion_probe.cpp'
    def run(command,log):
        r=subprocess.run(list(map(str,command)),env=env,cwd=ROOT,capture_output=True,text=True,encoding='utf8',errors='replace')
        (out/log).write_text(r.stdout+r.stderr,encoding='utf8')
        if r.returncode:raise RuntimeError(f'{log}: '+r.stderr[-2000:])
    cxx='C:/msys64/ucrt64/bin/g++.exe'
    for source in (observer,probe):
        obj=out/(source.stem+'.o');run([cxx,*report['flags'],'-std=c++20','-c',source,'-o',obj],source.stem+'.compile.log');objects.append(obj)
    exe=out/'probe.exe'
    run([cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],'link.log')
    env={k:v for k,v in env.items() if not k.startswith('PVZ_HOST_')}
    env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-wide').as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100',PVZ_HOST_SELECTOR_BUNDLE=(ROOT/'private/resources-h1-wide/h1menu/selector-wide.h1mb').as_posix())
    with (out/'console.log').open('w',encoding='utf8') as log:r=subprocess.run([str(exe)],env=env,cwd=ROOT,stdout=log,stderr=log)
    if r.returncode:raise RuntimeError(f'Probe failed ({r.returncode}); see {out}/console.log')
    proof=json.loads((out/'run/zombie-motion-verification.json').read_text(encoding='utf8'))
    proof.update(executable_sha256=sha(exe),compile_report_sha256=sha(host/'compile-report.json'),probe_sha256=sha(probe))
    (out/'verification.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(json.dumps(proof))

if __name__=='__main__':main()
