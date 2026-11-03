"""Captured glyph operations against the original on changing backgrounds."""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
ROOT=Path(__file__).resolve().parents[2]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--host',type=Path,default=ROOT/'build/host-offline-menu')
    ap.add_argument('--output',type=Path,default=ROOT/'build/text-replay-engine-probe')
    ap.add_argument('--resources',type=Path,default=ROOT/'private/resources-h1-wide')
    ap.add_argument('--bundle',type=Path,default=ROOT/'private/resources-h1-wide/h1menu/selector-wide.h1mb')
    ap.add_argument('--localization',action='store_true',help='Check all CJK glyphs, saved labels and the original almanac dialog instead')
    ap.add_argument('--glyph-report',type=Path,help='Defaults to the selected resource directory / zh-cn-manifest.json')
    ap.add_argument('--cxx',default='C:/msys64/ucrt64/bin/g++.exe');args=ap.parse_args()
    host=args.host.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    report=json.loads((host/'compile-report.json').read_text());assert report['linked'] and report['failed']==0
    for sub in ('run','save','tmp'): (out/sub).mkdir(exist_ok=True)
    env=os.environ.copy();env['PATH']=str(Path(args.cxx).parent)+os.pathsep+env.get('PATH','');env['TEMP']=env['TMP']=str(out/'tmp')
    objects=[];identities={}
    for row in report['records']:
        name=row['source'];assert sha(ROOT/name)==row['sha256'],name
        if name=='platform/host/host_io.cpp':continue
        obj=host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o');objects.append(obj);identities[name]={'source':row['sha256'],'object':sha(obj)}
    io=(ROOT/'platform/host/host_io.cpp').read_text(encoding='utf8')
    needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}'
    assert io.count(needle)==1
    symbol='pvzh1_localization_probe' if args.localization else 'pvzh1_text_replay_probe'
    probe=ROOT/'tools/tests'/('localization_engine_probe.cpp' if args.localization else 'text_replay_engine_probe.cpp')
    io=f'extern "C" void {symbol}();\n'+io.replace(needle,f'if(std::strcmp(checkpoint,"loading-complete")==0){{{symbol}();std::exit(0);}}')
    (out/'observer.cpp').write_text(io,encoding='utf8')
    for source in (out/'observer.cpp',probe):
        obj=out/(source.stem+'.o');cmd=[args.cxx,*report['flags'],'-std=c++20','-c',str(source),'-o',str(obj)]
        result=subprocess.run(cmd,capture_output=True,text=True,env=env)
        (out/(source.stem+'.compile.log')).write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        objects.append(obj)
    exe=out/'probe.exe';cmd=[args.cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',str(exe),*map(str,objects),'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi']
    result=subprocess.run(cmd,capture_output=True,text=True,env=env);(out/'link.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    if args.localization:
        glyphs=json.loads((args.glyph_report or args.resources/'zh-cn-manifest.json').read_text(encoding='utf8'))['fonts']['characters']
        (out/'characters.txt').write_text(glyphs,encoding='utf8')
        env['PVZ_ZH_CHARACTERS']=(out/'characters.txt').as_posix()
    env.update(PVZ_RESOURCE_DIR=args.resources.resolve().as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',
               PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100',PVZ_HOST_SELECTOR_BUNDLE=args.bundle.resolve().as_posix())
    for name in list(env):
        if name.startswith('PVZ_HOST_') and name not in ('PVZ_HOST_OUTPUT','PVZ_HOST_MAX_FRAMES','PVZ_HOST_SPEED','PVZ_HOST_SELECTOR_BUNDLE'):del env[name]
    with (out/'console.log').open('w') as log:result=subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=log,stderr=log)
    if result.returncode:raise RuntimeError(f'fixture exit {result.returncode}; {out}/console.log')
    identity={'objects':identities,'compile_report':sha(host/'compile-report.json'),'executable':sha(exe),
              'bundle':sha(args.bundle.resolve()),'probe':sha(probe)}
    (out/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
    verification=json.loads((out/'run'/('localization-engine-verification.json' if args.localization else 'text-replay-verification.json')).read_text());assert verification['passed'];print(json.dumps(verification))
if __name__=='__main__':main()
