"""Compare original fast3 fetches to bounded inline/opaque production fetches.

Only this isolated executable defines PVZH1_SAMPLER_TEST. Real engine animation
and matrix fixtures are reused from fast3_probe.cpp, with its toggle redirected
to sampler selection; fast3 itself stays enabled on both sides.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, shutil, subprocess
ROOT = Path(__file__).resolve().parents[2]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host-build',type=Path,default=Path('build/host-physical'))
    ap.add_argument('--cxx',type=Path,default=Path('C:/msys64/ucrt64/bin/g++.exe'))
    args=ap.parse_args();host=(ROOT/args.host_build).resolve();out=ROOT/'build/sampler-fastpaths-probe';out.mkdir(exist_ok=True)
    for folder in ('objects','tmp','run','save'):(out/folder).mkdir(exist_ok=True)
    report=json.loads((host/'compile-report.json').read_text());assert report['linked'] and not report['failed']
    shutil.copytree(host/'source',out/'source',dirs_exist_ok=True);shutil.copytree(ROOT/'platform',out/'platform',dirs_exist_ok=True)
    for name in ('reduced_texture_patches','reanim_fast_patches'):
        spec=importlib.util.spec_from_file_location(name,ROOT/f'platform/graphics/{name}.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);p.apply(out/'source')
    flags=[]
    for flag in report['flags']:
        for old,new in ((host/'source',out/'source'),(ROOT/'platform',out/'platform')):flag=flag.replace(str(old),str(new)).replace(old.as_posix(),new.as_posix())
        flags.append(flag)
    flags+=['-DPVZH1_SAMPLER_TEST','-I',str(ROOT/'runtime')]
    env=dict(os.environ);env['PATH']=str(args.cxx.parent)+os.pathsep+env.get('PATH','');env['TMP']=env['TEMP']=str(out/'tmp')
    def run(command,log):
        r=subprocess.run(list(map(str,command)),capture_output=True,text=True,env=env);(out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr)
    objects=[];identities={}
    for row in report['records']:
        name=row['source'];obj=host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o')
        if name in ('platform/graphics/SoftwareRaster.cpp','platform/host/host_io.cpp') or name.endswith('/SexyAppFramework/graphics/SWTri.cpp') or name.endswith('/PvzpLib/Reanimator.cpp'):continue
        target=out/'objects'/obj.name;shutil.copyfile(obj,target);objects.append(target);identities[name]=dict(source=row['sha256'],object=sha(target))
    observer=(out/'platform/host/host_io.cpp').read_text().replace('extern "C" void pvzh1_host_probe_fonts();','extern "C" void pvzh1_host_probe_fonts();\nextern "C" void pvzh1_sampler_probe();')
    needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();}';assert observer.count(needle)==1
    (out/'observer.cpp').write_text(observer.replace(needle,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_sampler_probe();std::exit(0);}'))
    template=Path(__file__).with_name('fast3_probe.cpp');probe=template.read_text().replace('pvzh1_fast3_test_enabled','pvzh1_sampler_test_enabled').replace('pvzh1_fast3_probe','pvzh1_sampler_probe').replace('fast3-verification.json','sampler-verification.json')
    fragment=Path(__file__).with_name('sampler_cases.inc')
    probe=probe.replace('#include "graphics/GLImage.h"','#include "graphics/GLImage.h"\n#include "graphics/SWTri.h"')
    probe=probe.replace('extern "C" void pvzh1_sampler_probe(){',fragment.read_text()+'\nextern "C" void pvzh1_sampler_probe(){\n unsigned readerChecks=0,additionalPixelChecks=0;SamplerAdditionalCases(readerChecks,additionalPixelChecks);')
    probe=probe.replace('std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"sampler-verification.json");out<<', 'std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"sampler-verification.json");out<<')
    probe=probe.replace('{\\"passed\\":true,','{\\"passed\\":true,\\"direct_reader_checks\\":'+ '"<<readerChecks<<",\\"additional_nearest_linear_opaque_tint_pixel_checks\\":'+'"<<additionalPixelChecks<<",')
    (out/'probe.cpp').write_text(probe)
    for i,src in enumerate((out/'source/SexyAppFramework/graphics/SWTri.cpp',out/'source/PvzpLib/Reanimator.cpp',out/'platform/graphics/SoftwareRaster.cpp',out/'observer.cpp',out/'probe.cpp')):
        obj=out/f'changed-{i}.o';run([args.cxx,*flags,'-std=c++20','-c',src,'-o',obj],obj.name+'.log');objects.append(obj);identities[str(src.relative_to(ROOT))]=dict(source=sha(src),object=sha(obj))
    exe=out/'probe.exe';run([args.cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],'link.log')
    for key in list(env):
        if key.startswith('PVZ_HOST_'):del env[key]
    env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-full').as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100')
    with (out/'console.log').open('w') as log:subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=log,stderr=log,check=True)
    result=json.loads((out/'run/sampler-verification.json').read_text());assert result['passed']
    result.update(scope='Original fast3 sampler vs new production inline screen sampler/opaque dispatch; real full animation, attachment and BG matrix pixel oracle. No device access or FPS claim.',fixture_template_sha256=sha(template),additional_fixture_sha256=sha(fragment),base_compile_report_sha256=sha(host/'compile-report.json'),objects=identities,source_sha256={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'platform/graphics/reduced_texture_patches.py',ROOT/'platform/graphics/SoftwareRaster.cpp')})
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ('objects','source_sha256')}))
if __name__=='__main__':main()
