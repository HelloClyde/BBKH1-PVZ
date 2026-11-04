"""Compile and execute the actual offline-selector loader/replay in isolation."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cxx',default='C:/msys64/ucrt64/bin/g++.exe')
    ap.add_argument('--source',type=Path,default=ROOT/'build/host-offline-menu/source')
    ap.add_argument('--output',type=Path,default=ROOT/'build/selector-offline-probe');args=ap.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['PATH']=str(Path(args.cxx).parent)+os.pathsep+env.get('PATH','')
    exe=out/'probe.exe';source=ROOT/'tools/tests/selector_offline_probe.cpp'
    includes=[ROOT/'platform/host/include',ROOT/'platform/include',ROOT/'platform',ROOT/'platform/graphics',
              ROOT/'platform/resources',args.source,args.source/'SexyAppFramework']
    command=[args.cxx,'-std=c++20','-O1','-ffunction-sections','-fdata-sections','-DH1_PVZ_PORTABLE','-DLOW_MEMORY','-DPVZH1_SELECTOR_OFFLINE_STANDALONE',
             '-Wl,--gc-sections','-static-libstdc++','-static-libgcc',str(source),'-lz','-o',str(exe)]
    for path in includes:command+=['-I',str(path)]
    built=subprocess.run(command,capture_output=True,text=True,env=env)
    if built.returncode:raise RuntimeError(built.stdout+built.stderr)
    ran=subprocess.run([str(exe),str(out/'fixture.h1mb')],capture_output=True,text=True,env=env)
    if ran.returncode:raise RuntimeError(ran.stdout+ran.stderr)
    report=json.loads(ran.stdout);report['production_sha256']={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (ROOT/'platform/selector/SelectorOffline.cpp',ROOT/'platform/selector/SelectorOffline.h',ROOT/'platform/selector/selector_offline_patches.py')}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
