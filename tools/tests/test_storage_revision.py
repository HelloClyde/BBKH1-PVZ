"""Targeted real-cache cookie and pressure checks with a frozen host ABI.

Only MemoryImageCache and this observer are rebuilt. Renderer/game objects are
the unmodified host-physical snapshot. Artifacts stay below build/.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/storage-revision-probe'
HOST=ROOT/'build/host-physical'
FROZEN=ROOT/'build/physical-screen-probe/platform'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    for folder in ('source','platform','tmp','run','save'):(OUT/folder).mkdir(parents=True,exist_ok=True)
    report=json.loads((HOST/'compile-report.json').read_text())
    assert report['linked'] and not report['failed']
    shutil.copytree(HOST/'source',OUT/'source',dirs_exist_ok=True)
    shutil.copytree(FROZEN,OUT/'platform',dirs_exist_ok=True)
    for name in ('MemoryImageCache.cpp','MemoryImageCache.h'):
        shutil.copyfile(ROOT/'platform/graphics'/name,OUT/'platform/graphics'/name)
    objects=[];identities={}
    for row in report['records']:
        name=row['source']
        obj=HOST/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o')
        identities[name]=dict(frozen_source_sha256=row['sha256'],object_sha256=sha(obj))
        if name not in ('platform/graphics/MemoryImageCache.cpp','platform/host/host_io.cpp'):objects.append(obj)
    flags=[]
    for flag in report['flags']:
        for old,new in ((HOST/'source',OUT/'source'),(ROOT/'platform',OUT/'platform')):
            flag=flag.replace(str(old),str(new)).replace(old.as_posix(),new.as_posix())
        flags.append(flag)
    observer=OUT/'platform/host/host_io.cpp';text=observer.read_text()
    anchor='extern "C" void pvzh1_host_probe_fonts();'
    assert text.count(anchor)==1;text=text.replace(anchor,anchor+'\nextern "C" void pvzh1_storage_revision_probe();')
    needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();}'
    assert text.count(needle)==1;text=text.replace(needle,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_storage_revision_probe();std::exit(0);}')
    observer.write_text(text)
    fixture=Path(__file__).with_name('storage_revision_probe.cpp');shutil.copyfile(fixture,OUT/'probe.cpp')
    env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');env['TMP']=env['TEMP']=str(OUT/'tmp')
    cxx='C:/msys64/ucrt64/bin/g++.exe'
    def run(command,log):
        r=subprocess.run(list(map(str,command)),capture_output=True,text=True,env=env)
        (OUT/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(log+': '+r.stderr[-2500:])
    for src in (OUT/'platform/graphics/MemoryImageCache.cpp',observer,OUT/'probe.cpp'):
        obj=OUT/(src.stem+'.o');run([cxx,*flags,'-std=c++20','-c',src,'-o',obj],src.stem+'.compile.log');objects.append(obj)
    exe=OUT/'probe.exe'
    run([cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,
        '-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],'link.log')
    for key in list(env):
        if key.startswith('PVZ_HOST_'):del env[key]
    env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-full').as_posix()+'/',PVZ_SAVE_DIR=(OUT/'save').as_posix()+'/',
        PVZ_HOST_OUTPUT=(OUT/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100')
    with (OUT/'console.log').open('w') as log:
        subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=log,stderr=log,timeout=60,check=True)
    result=json.loads((OUT/'run/storage-revision-verification.json').read_text());assert result['all_passed']
    identity=dict(test_only=True,scope='Cache-only implementation override; frozen MemoryImage/game/renderer ABI, no device.',
        compile_report_sha256=sha(HOST/'compile-report.json'),frozen_objects=identities,
        cache_source_sha256=sha(ROOT/'platform/graphics/MemoryImageCache.cpp'),cache_header_sha256=sha(ROOT/'platform/graphics/MemoryImageCache.h'),
        fixture_sha256=sha(fixture),executable_sha256=sha(exe))
    (OUT/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
    print(json.dumps(result))

if __name__=='__main__':main()
