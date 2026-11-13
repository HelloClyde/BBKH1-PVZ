"""Real opening and profile files under a fragmented-page allocation model."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--reference-64k',action='store_true');args=ap.parse_args()
    host=ROOT/'build/host-first-level-fix';out=ROOT/('build/intro-delete-reference-64k' if args.reference_64k else 'build/intro-delete-host');out.mkdir(exist_ok=True)
    for n in ('run','save','tmp'):(out/n).mkdir(exist_ok=True)
    report=json.loads((host/'compile-report.json').read_text(encoding='utf8'));assert report['linked'] and not report['failed']
    env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');env['TEMP']=env['TMP']=str(out/'tmp');cxx='C:/msys64/ucrt64/bin/g++.exe';objects=[]
    def run(cmd,log):
        r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,capture_output=True,text=True,encoding='utf8',errors='replace');(out/log).write_text(r.stdout+r.stderr,encoding='utf8')
        if r.returncode:raise RuntimeError(log+'\n'+r.stderr[-1800:])
    for item in report['records']:
        n=item['source'];assert sha(ROOT/n)==item['sha256'],n
        if n=='platform/host/host_io.cpp' or (args.reference_64k and n=='platform/video/IntroMovie.cpp'):continue
        objects.append(host/(hashlib.sha1(n.encode()).hexdigest()[:10]+'-'+Path(n).stem+'.o'))
    io=(ROOT/'platform/host/host_io.cpp').read_text(encoding='utf8');anchor='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}';assert io.count(anchor)==1
    observer=out/'observer.cpp';observer.write_text('extern "C" void pvzh1_intro_delete_probe();\n'+io.replace(anchor,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_intro_delete_probe();std::exit(0);}'),encoding='utf8')
    sources=[observer,ROOT/'tools/tests/intro_delete_probe.cpp']
    if args.reference_64k:
        original=(ROOT/'platform/video/IntroMovie.cpp').read_text(encoding='utf8');assert 'PageBytes=16384' in original
        f=out/'IntroMovie64.cpp';f.write_text(original.replace('PageBytes=16384','PageBytes=65536'),encoding='utf8');sources.append(f)
    for s in sources:
        obj=out/(s.stem+'.o');run([cxx,*report['flags'],'-I',ROOT/'platform/video','-std=c++20','-c',s,'-o',obj],s.stem+'.compile.log');objects.append(obj)
    exe=out/'probe.exe';run([cxx,'-Wl,--gc-sections','-Wl,--wrap=malloc','-Wl,--wrap=_Z19H1IntroMoviePrepareP5Board','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],'link.log')
    env={k:v for k,v in env.items() if not k.startswith('PVZ_HOST_')};env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-wide').as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100')
    if args.reference_64k:env['PVZ_HOST_EXPECT_PAGE_OOM']='1'
    with (out/'console.log').open('w',encoding='utf8') as f:r=subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=f,stderr=f)
    assert r.returncode==0,(r.returncode,str(out/'console.log'))
    proof=json.loads((out/'run/intro-delete-verification.json').read_text(encoding='utf8'));assert proof['passed'];proof.update(executable_sha256=sha(exe),probe_sha256=sha(ROOT/'tools/tests/intro_delete_probe.cpp'),compile_report_sha256=sha(host/'compile-report.json'))
    (out/'verification.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(json.dumps(proof))
if __name__=='__main__':main()
