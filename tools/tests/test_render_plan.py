"""Actual prepared engine render-plan replay and standalone lifecycle oracles."""
from pathlib import Path
import hashlib,importlib.util,json,os,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 host=ROOT/'build/host-physical';out=ROOT/'build/render-plan-probe';out.mkdir(exist_ok=True)
 for folder in ('objects','source','platform','tmp','run','save'):(out/folder).mkdir(exist_ok=True)
 cxx=Path('C:/msys64/ucrt64/bin/g++.exe');env=dict(os.environ);env['PATH']=str(cxx.parent)+os.pathsep+env.get('PATH','');env['TMP']=env['TEMP']=str(out/'tmp')
 def run(command,log):
  r=subprocess.run(list(map(str,command)),capture_output=True,text=True,env=env);(out/log).write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError(r.stderr)
 report=json.loads((host/'compile-report.json').read_text());assert report['linked'] and not report['failed'];shutil.copytree(host/'source',out/'source',dirs_exist_ok=True);shutil.copytree(ROOT/'platform',out/'platform',dirs_exist_ok=True)
 for name in ('reduced_texture_patches','reanim_fast_patches'):
  spec=importlib.util.spec_from_file_location(name,ROOT/f'platform/graphics/{name}.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.apply(out/'source')
 flags=[]
 for flag in report['flags']:
  for old,new in ((host/'source',out/'source'),(ROOT/'platform',out/'platform')):flag=flag.replace(str(old),str(new)).replace(old.as_posix(),new.as_posix())
  flags.append(flag)
 flags+=['-I',str(ROOT/'runtime')]
 run([cxx,'-DPVZH1_PLAN_STANDALONE','-std=c++20','-Os','-I',out/'platform/graphics',ROOT/'tools/tests/render_plan_unit.cpp',out/'platform/graphics/RenderPlanCache.cpp','-o',out/'unit.exe'],'unit.compile.log');run([out/'unit.exe'],'unit-result.json');assert json.loads((out/'unit-result.json').read_text())['passed']
 objects=[];identities={};replace={'platform/graphics/SoftwareRaster.cpp','platform/graphics/MemoryImageCache.cpp','platform/graphics/PhysicalScreen.cpp','platform/host/host_io.cpp'}
 for row in report['records']:
  name=row['source'];obj=host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o')
  if name in replace or name.endswith('/SexyAppFramework/graphics/SWTri.cpp') or name.endswith('/PvzpLib/Reanimator.cpp'):continue
  dest=out/'objects'/obj.name;shutil.copyfile(obj,dest);objects.append(dest);identities[name]=dict(source=row['sha256'],object=sha(dest))
 observer=(out/'platform/host/host_io.cpp').read_text().replace('extern "C" void pvzh1_host_probe_fonts();','extern "C" void pvzh1_host_probe_fonts();\nextern "C" void pvzh1_render_plan_probe();');needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();}';assert observer.count(needle)==1;(out/'observer.cpp').write_text(observer.replace(needle,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_render_plan_probe();std::exit(0);}'))
 sources=[out/'source/SexyAppFramework/graphics/SWTri.cpp',out/'source/PvzpLib/Reanimator.cpp',*[out/f'platform/graphics/{name}.cpp' for name in ('SoftwareRaster','MemoryImageCache','PhysicalScreen','RenderPlanCache')],out/'observer.cpp',ROOT/'tools/tests/render_plan_probe.cpp']
 for i,src in enumerate(sources):
  obj=out/f'changed-{i}.o';run([cxx,*flags,'-std=c++20','-c',src,'-o',obj],obj.name+'.log');objects.append(obj);identities[str(src.relative_to(ROOT))]=dict(source=sha(src),object=sha(obj))
 exe=out/'probe.exe';run([cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],'link.log')
 for key in list(env):
  if key.startswith('PVZ_HOST_'):del env[key]
 env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-full').as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100')
 with (out/'console.log').open('w') as log:subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=log,stderr=log,check=True)
 result=dict(passed=True,scope='Host correctness/lifecycle only; no real H1 FPS claim',unit=json.loads((out/'unit-result.json').read_text()),engine=json.loads((out/'run/render-plan-verification.json').read_text()),objects=identities,inputs={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'platform/graphics/RenderPlanCache.cpp',ROOT/'platform/graphics/RenderPlanCache.h',ROOT/'platform/graphics/SoftwareRaster.cpp',ROOT/'platform/graphics/reduced_texture_patches.py')});assert result['engine']['passed'];(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ('objects','inputs')}))
if __name__=='__main__':main()
