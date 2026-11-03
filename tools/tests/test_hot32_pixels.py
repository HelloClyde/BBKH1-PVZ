"""Paired original -Os vs actual checked hot32-prepared kernel pixel digests.

Every changed TU is rebuilt from its own prepared tree. -Os and retained frame
pointers are explicit on both; only the production patch selects local O2/omit.
The same full engine, resource pack and deterministic fixtures run in both.
"""
from pathlib import Path
import hashlib,importlib.util,json,os,shutil,subprocess
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 base=ROOT/'build/render-plan-probe';host=ROOT/'build/host-physical';out=ROOT/'build/hot32-render-probe';out.mkdir(exist_ok=True);cxx=Path('C:/msys64/ucrt64/bin/g++.exe');report=json.loads((host/'compile-report.json').read_text());env=dict(os.environ);env['PATH']=str(cxx.parent)+os.pathsep+env.get('PATH','')
 for directory in ('tmp','save'):(out/directory).mkdir(exist_ok=True)
 env['TMP']=env['TEMP']=str(out/'tmp')
 def run(cmd,log):
  r=subprocess.run(list(map(str,cmd)),env=env,capture_output=True,text=True);log.write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
 template=ROOT/'tools/tests/fast3_probe.cpp';anim=template.read_text().replace('pvzh1_fast3_probe','pvzh1_hot32_animation_probe').replace('fast3-verification.json','hot32-animation.json')
 anim=anim.replace('uint64_t digest=0;','uint64_t digest=0;std::vector<uint64_t> caseHashes;').replace('++animationChecks;','caseHashes.push_back(Hash(Pixels(target)));++animationChecks;').replace('++matrixChecks;','caseHashes.push_back(Hash(Pixels(target)));++matrixChecks;')
 anim=anim.replace('std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"hot32-animation.json");','std::ofstream cases(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"animation-case-hashes.txt");for(auto h:caseHashes)cases<<h<<"\\n";\n std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"hot32-animation.json");')
 (out/'animation.cpp').write_text(anim)
 (out/'driver.cpp').write_text('#include "graphics/RenderPlanCache.h"\nextern "C" void pvzh1_hot32_pixel_probe();\nextern "C" void pvzh1_hot32_animation_probe();\nextern "C" void pvzh1_render_plan_probe(){pvzh1_hot32_pixel_probe();pvzh1_hot32_animation_probe();pvzh1_render_plan_shutdown();}\n')
 spec=importlib.util.spec_from_file_location('hot',ROOT/'platform/graphics/raster_codegen_patches.py');patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
 commonObjects=[p for p in (base/'objects').glob('*.o') if not p.name.endswith('-PvzpCommon.o') and not p.name.endswith('-EffectSystem.o')]+[base/f'changed-{i}.o' for i in range(1,7)]
 results={};prepared={}
 for variant in ('reference','hot32'):
  folder=out/variant;folder.mkdir(exist_ok=True);(folder/'run').mkdir(exist_ok=True);shutil.copytree(base/'source',folder/'source',dirs_exist_ok=True)
  assert 'PVZH1_HOT_0888_TEXTURED_OPTIONS' not in (folder/'source/SexyAppFramework/graphics/SWTri/SWTri_DrawTriangle.inc').read_text()
  if variant=='hot32':patch.apply(folder/'source');before={str(p.relative_to(folder/'source')):sha(p) for p in (folder/'source').rglob('*') if p.is_file()};patch.apply(folder/'source');assert before=={str(p.relative_to(folder/'source')):sha(p) for p in (folder/'source').rglob('*') if p.is_file()}
  flags=[]
  for flag in report['flags']:
   for old,new in ((host/'source',folder/'source'),(ROOT/'platform',base/'platform')):flag=flag.replace(str(old),str(new)).replace(old.as_posix(),new.as_posix())
   flags.append(flag)
  flags+=['-Os','-fno-omit-frame-pointer','-I',str(ROOT/'runtime')]
  objects=list(commonObjects)
  for name,source in (('SWTri',folder/'source/SexyAppFramework/graphics/SWTri.cpp'),('PvzpCommon',folder/'source/PvzpLib/PvzpCommon.cpp'),('EffectSystem',folder/'source/PvzpLib/EffectSystem.cpp'),('pixels',ROOT/'tools/tests/hot32_pixel_probe.cpp'),('animation',out/'animation.cpp'),('driver',out/'driver.cpp')):
   obj=folder/(name+'.o');run([cxx,*flags,'-std=c++20','-c',source,'-o',obj],folder/(name+'.compile.log'));objects.append(obj)
  exe=folder/'probe.exe';run([cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],folder/'link.log')
  for key in list(env):
   if key.startswith('PVZ_HOST_'):del env[key]
  env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-full').as_posix()+'/',PVZ_SAVE_DIR=(out/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(folder/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100')
  with (folder/'console.log').open('w') as log:subprocess.run([str(exe)],cwd=ROOT,env=env,stdout=log,stderr=log,check=True)
  results[variant]={'pixels':json.loads((folder/'run/hot32-pixels.json').read_text()),'animation':json.loads((folder/'run/hot32-animation.json').read_text()),'animation_case_hashes':(folder/'run/animation-case-hashes.txt').read_text().splitlines()}
  prepared[variant]={str(p.relative_to(folder/'source')):sha(p) for p in (folder/'source/SexyAppFramework/graphics/SWTri.cpp',folder/'source/PvzpLib/PvzpCommon.cpp',folder/'source/SexyAppFramework/graphics/SWTri/SWTri_DrawTriangle.inc')}
 a,b=results['reference'],results['hot32'];assert a['pixels']==b['pixels'],'Actual old/new prepared kernel pixels differ';assert a['animation_case_hashes']==b['animation_case_hashes'],'Actual full animation/BG pixels differ';assert a['animation']==b['animation']
 result=dict(passed=True,scope='Paired original -Os/FP vs actual checked hot32 prepared -Os with local O2/omit; full framebuffer FNV64 digests compared per deterministic case. No device speed claim.',kernel_cases=b['pixels']['kernel_cases'],forced_mode_linear_draw_cases=b['pixels']['forced_mode_linear_draw_cases'],real_font_alpha_cases=b['pixels']['real_font_alpha_cases'],animation_BG_case_hashes=len(b['animation_case_hashes']),all_case_digests_identical=True,normal_and_custom_effect_32bit=True,formats=[0x888,0x8888],modes=['normal','additive'],prepared_inputs=prepared,patch_sha256=sha(ROOT/'platform/graphics/raster_codegen_patches.py'),fixture_sha256=sha(ROOT/'tools/tests/hot32_pixel_probe.cpp'))
 (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
