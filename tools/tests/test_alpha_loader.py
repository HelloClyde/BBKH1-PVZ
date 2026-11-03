"""Exercise the production exact A8 loader against original ARGB blits.
Run tools/build_host.py first to create one matching complete host build.
All fixture binaries, copied objects and evidence stay under build/.
"""
from pathlib import Path
import json,hashlib,subprocess,shutil,os
TESTS=Path(__file__).resolve().parent;R=TESTS.parents[1];O=R/'build/alpha-loader-probe';O.mkdir(exist_ok=True);H=R/'build/host';C=Path(os.environ.get('CXX','C:/msys64/ucrt64/bin/g++.exe'))
shutil.copyfile(TESTS/'alpha_loader_probe.cpp',O/'probe.cpp')
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
report=json.loads((H/'compile-report.json').read_text());assert report['linked'] and not report['failed']
for n in ['source','objects','tmp','run','save']:(O/n).mkdir(exist_ok=True)
shutil.copytree(H/'source',O/'source',dirs_exist_ok=True)
objects=[];ids={}
for row in report['records']:
 name=row['source']
 if name=='platform/host/host_io.cpp':continue
 assert sha(R/name)==row['sha256'],name
 obj=H/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o');digest=sha(obj);target=O/'objects'/obj.name;shutil.copyfile(obj,target);assert sha(obj)==digest
 objects.append(target);ids[name]={'source':row['sha256'],'object':digest}
flags=[s.replace(str(H/'source'),str(O/'source')).replace((H/'source').as_posix(),(O/'source').as_posix()) for s in report['flags']]
io=(R/'platform/host/host_io.cpp').read_text(encoding='utf8').replace('extern "C" void pvzh1_host_probe_fonts();','extern "C" void pvzh1_host_probe_fonts();\nextern "C" void pvzh1_alpha_loader_probe();')
needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();}';assert io.count(needle)==1
io=io.replace(needle,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_alpha_loader_probe();std::exit(0);}')
(O/'host_io.cpp').write_text(io,encoding='utf8');env=os.environ.copy();env['PATH']=str(C.parent)+os.pathsep+env.get('PATH','');env['TMP']=env['TEMP']=str(O/'tmp')
for p in [O/'host_io.cpp',O/'probe.cpp']:
 obj=O/(p.stem+'.o');result=subprocess.run(list(map(str,[C,*flags,'-std=c++20','-c',p,'-o',obj])),capture_output=True,text=True,env=env);(O/(p.stem+'.compile.log')).write_text(result.stdout+result.stderr)
 if result.returncode:print(result.stderr);raise SystemExit(result.returncode)
 objects.append(obj)
exe=O/'alpha-loader-probe.exe';cmd=[C,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'];result=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,env=env);(O/'link.log').write_text(result.stdout+result.stderr)
if result.returncode:print(result.stderr);raise SystemExit(result.returncode)
identity={'test_only':True,'production_modified':False,'host_compile_report_sha256':sha(H/'compile-report.json'),'objects':ids,'probe':sha(O/'probe.cpp'),'H1Image_header':sha(R/'platform/resources/h1_resources.h'),'executable':sha(exe)};(O/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
env.update(PVZ_RESOURCE_DIR=(R/'private/resources').as_posix()+'/',PVZ_SAVE_DIR=(O/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(O/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100')
for n in list(env):
 if n.startswith('PVZ_HOST_') and n not in ['PVZ_HOST_OUTPUT','PVZ_HOST_MAX_FRAMES','PVZ_HOST_SPEED']:del env[n]
with (O/'console.log').open('w') as log:result=subprocess.run([str(exe)],cwd=R,env=env,stdout=log,stderr=log)
print('Alpha loader fixture exit',result.returncode)
if result.returncode:raise SystemExit(result.returncode)
verification=json.loads((O/'run/alpha-loader-verification.json').read_text());assert verification['all_passed'];print('PASS cases',verification['case_count'])
