"""Compile the pinned libxmp core and compare shared players' PCM exactly."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def execute(command):
    p=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    if p.returncode:raise RuntimeError(p.stdout+p.stderr)
    return p.stdout
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default=shutil.which('gcc') or 'gcc')
    ap.add_argument('modules',nargs='*',type=Path)
    args=ap.parse_args()
    config=json.loads((ROOT/'platform/music_sources.json').read_text())
    out=ROOT/'build/xmp-shared-test';out.mkdir(parents=True,exist_ok=True)
    sources=[ROOT/p for p in config['sources']]+[ROOT/'platform/xmp_shared.c',ROOT/'tools/test_xmp_shared.c']
    flags=['-std=gnu99','-O2','-g','-Wall','-Wextra','-Wno-unused-parameter']
    flags+=['-D'+d for d in config['defines']]
    flags+=['-I'+str(ROOT/p) for p in config['includes']]+['-I'+str(ROOT/'platform')]
    def compile_source(source):
        obj=out/(hashlib.sha1(str(source).encode()).hexdigest()[:10]+'.o')
        strict=['-Werror'] if 'vendor' not in source.parts else []
        execute([args.cc,*flags,*strict,'-c',source,'-o',obj]);return obj
    with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_source,sources))
    exe=out/'test-xmp-shared.exe'
    execute([args.cc,*objects,'-lm','-o',exe])
    modules=args.modules or [ROOT/'private/resources/sounds/mainmusic.xm',ROOT/'private/resources/sounds/mainmusic_hihats.xm']
    reports=[]
    for module in modules:
        report=json.loads(execute([exe,module]));report.update(path=str(module),sha256=hashlib.sha256(module.read_bytes()).hexdigest())
        reports.append(report);print(json.dumps(report),flush=True)
    (out/'report.json').write_text(json.dumps({'implementation_sha256':hashlib.sha256((ROOT/'platform/xmp_shared.c').read_bytes()).hexdigest(),'modules':reports},indent=2)+'\n')
if __name__=='__main__':main()
