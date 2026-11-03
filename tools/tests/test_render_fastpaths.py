"""Isolated exact presenter + real engine SWTri row fast-path pixel oracles."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--host-build',type=Path,default=Path('build/host-physical'));ap.add_argument('--cxx',type=Path,default=Path('C:/msys64/ucrt64/bin/g++.exe'));args=ap.parse_args()
 host=(ROOT/args.host_build).resolve();out=ROOT/'build/render-fastpaths-probe';out.mkdir(exist_ok=True)
 for folder in ('objects','tmp','run','save'):(out/folder).mkdir(exist_ok=True)
 report=json.loads((host/'compile-report.json').read_text());assert report['linked'] and not report['failed']
 shutil.copytree(host/'source',out/'source',dirs_exist_ok=True);shutil.copytree(ROOT/'platform',out/'platform',dirs_exist_ok=True)
 spec=importlib.util.spec_from_file_location('patch',ROOT/'platform/graphics/reduced_texture_patches.py');patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch);patch.apply(out/'source')
 flags=[]
 for flag in report['flags']:
  for original,snapshot in ((host/'source',out/'source'),(ROOT/'platform',out/'platform')):flag=flag.replace(str(original),str(snapshot)).replace(original.as_posix(),snapshot.as_posix())
  flags.append(flag)
 env=dict(os.environ);env['PATH']=str(args.cxx.parent)+os.pathsep+env.get('PATH','');env['TMP']=env['TEMP']=str(out/'tmp')
 def run(command,log):
  r=subprocess.run(list(map(str,command)),capture_output=True,text=True,env=env);(out/log).write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(r.stderr)
 def compile(src,obj):run([args.cxx,*flags,'-std=c++20','-c',src,'-o',obj],obj.name+'.log')
 presentation=Path(__file__).with_name('presentation_probe.cpp');run([args.cxx,'-std=c++20','-O2','-I',out/'platform',presentation,'-o',out/'presentation.exe'],'presentation.compile.log');run([out/'presentation.exe'],'presentation-result.json');assert json.loads((out/'presentation-result.json').read_text())['all_passed']
 objects=[];identities={};replace={'platform/graphics/SoftwareRaster.cpp','platform/host/host_io.cpp'}
 for row in report['records']:
  name=row['source'];obj=host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o')
  if name in replace or name.endswith('/SexyAppFramework/graphics/SWTri.cpp'):continue
  target=out/'objects'/obj.name;shutil.copyfile(obj,target);objects.append(target);identities[name]=dict(source=row['sha256'],object=sha(target))
 observer=(out/'platform/host/host_io.cpp').read_text();observer=observer.replace('extern "C" void pvzh1_host_probe_fonts();','extern "C" void pvzh1_host_probe_fonts();\nextern "C" void pvzh1_render_fastpath_probe();');needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();}';assert observer.count(needle)==1;observer=observer.replace(needle,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_render_fastpath_probe();std::exit(0);}')
 (out/'observer.cpp').write_text(observer)
 for i,src in enumerate((out/'source/SexyAppFramework/graphics/SWTri.cpp',out/'platform/graphics/SoftwareRaster.cpp',out/'observer.cpp',Path(__file__).with_name('render_fastpaths_probe.cpp'))):
  obj=out/f'changed-{i}.o';compile(src,obj);objects.append(obj);identities[str(src.relative_to(ROOT))]=dict(source=sha(src),object=sha(obj))
 exe=out/'probe.exe';run([args.cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],'link.log')
 for key in list(env):
  if key.startswith('PVZ_HOST_'):del env[key]
 env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-full').as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100')
 with (out/'console.log').open('w') as log:subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=log,stderr=log,check=True)
 verification=json.loads((out/'run/render-fastpaths-verification.json').read_text());assert verification['all_passed']
 combined=dict(all_passed=True,scope='host pixel correctness only; no real H1 performance claim',presentation=json.loads((out/'presentation-result.json').read_text()),raster=verification,base_compile_report_sha256=sha(host/'compile-report.json'),objects=identities,executable_sha256=sha(exe),source_sha256={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'platform/h1_host.cpp',ROOT/'platform/graphics/PresentationMap.h',ROOT/'platform/graphics/SoftwareRaster.cpp',ROOT/'platform/graphics/SoftwareRaster.h',ROOT/'platform/graphics/reduced_texture_patches.py')})
 (out/'verification.json').write_text(json.dumps(combined,indent=2)+'\n');print(json.dumps(dict(all_passed=True,presentation=combined['presentation'],raster=verification,report=str(out/'verification.json'))))
if __name__=='__main__':main()
