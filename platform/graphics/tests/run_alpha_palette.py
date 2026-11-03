"""Compare the original ARGB font renderer with the production exact A8 loader.

First build the complete host with tools/build_host.py. This fixture copies its
matching objects, uses both original Font32 descriptors, and writes evidence to
build/alpha-palette-probe. It never changes production or upstream sources.
"""
from pathlib import Path
import json,hashlib,shutil,os,subprocess
TESTS=Path(__file__).resolve().parent;ROOT=TESTS.parents[2];OUT=ROOT/'build/alpha-palette-probe';OUT.mkdir(parents=True,exist_ok=True);HOST=ROOT/'build/host';report=json.loads((HOST/'compile-report.json').read_text());CXX=os.environ.get('CXX','C:/msys64/ucrt64/bin/g++.exe');env=os.environ.copy();env['PATH']=str(Path(CXX).parent)+';'+env.get('PATH','')
(OUT/'probe.cpp').write_text((TESTS/'alpha_palette_probe.cpp').read_text(encoding='utf8'),encoding='utf8')
assert report['linked'] and not report['failed']
io=(ROOT/'platform/host/host_io.cpp').read_text(encoding='utf8').replace('extern "C" void pvzh1_host_probe_fonts();','extern "C" void pvzh1_host_probe_fonts();\nextern "C" void pvzh1_alpha_palette_probe();')
needle='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();}'
assert io.count(needle)==1;io=io.replace(needle,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_alpha_palette_probe();std::exit(0);}')
(OUT/'host_io.cpp').write_text(io,encoding='utf8');(OUT/'objects').mkdir(exist_ok=True);objects=[];identity={}
for r in report['records']:
 name=r['source']
 assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==r['sha256'],f'Stale host object source: {name}; rebuild host first'
 if name=='platform/host/host_io.cpp':continue
 obj=HOST/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o');target=OUT/'objects'/obj.name;shutil.copyfile(obj,target);objects.append(target);identity[name]={'source':r['sha256'],'object':hashlib.sha256(target.read_bytes()).hexdigest()}
for p in [OUT/'host_io.cpp',OUT/'probe.cpp']:
 obj=OUT/(p.stem+'.o');r=subprocess.run(list(map(str,[CXX,*report['flags'],'-std=c++20','-c',p,'-o',obj])),env=env,capture_output=True,text=True);(OUT/(p.stem+'.compile.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr;objects.append(obj)
exe=OUT/'probe.exe';r=subprocess.run([CXX,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',str(exe),*map(str,objects),'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],env=env,capture_output=True,text=True);(OUT/'link.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
(OUT/'run').mkdir(exist_ok=True);(OUT/'save').mkdir(exist_ok=True);env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources').as_posix()+'/',PVZ_SAVE_DIR=(OUT/'save').as_posix()+'/',PVZ_HOST_OUTPUT=(OUT/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100')
for n in list(env):
 if n.startswith('PVZ_HOST_') and n not in ('PVZ_HOST_OUTPUT','PVZ_HOST_MAX_FRAMES','PVZ_HOST_SPEED'):del env[n]
(OUT/'identity.json').write_text(json.dumps({'test_only':True,'production_modified':False,'objects':identity,'probe_source':hashlib.sha256((OUT/'probe.cpp').read_bytes()).hexdigest(),'executable':hashlib.sha256(exe.read_bytes()).hexdigest()},indent=2))
with (OUT/'console.log').open('w') as log:r=subprocess.run([str(exe)],env=env,cwd=ROOT,stdout=log,stderr=log)
print('palette probe exit',r.returncode)
assert r.returncode==0,'See build/alpha-palette-probe/console.log and run/crash.txt'
result=json.loads((OUT/'run/alpha-palette-verification.json').read_text())
assert result['production_loader'] and result['all_passed'] and result['all_1x_no_expansion']
