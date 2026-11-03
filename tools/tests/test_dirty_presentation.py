from pathlib import Path
import os,subprocess,json,hashlib
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/dirty-presentation-probe';OUT.mkdir(exist_ok=True)
CXX=Path('C:/msys64/ucrt64/bin/g++.exe')
source=Path(__file__).with_name('dirty_presentation_probe.cpp')
exe=OUT/'probe.exe'
env=os.environ.copy();env['PATH']=str(CXX.parent)+os.pathsep+env.get('PATH','')
r=subprocess.run([str(CXX),'-std=c++20','-O2','-I',str(ROOT/'platform'),str(source),'-o',str(exe)],capture_output=True,text=True,env=env)
(OUT/'compile.log').write_text(r.stdout+r.stderr)
assert r.returncode==0,r.stderr
r=subprocess.run([str(exe)],capture_output=True,text=True,env=env)
(OUT/'console.log').write_text(r.stdout+r.stderr)
assert r.returncode==0,r.stdout+r.stderr
result=json.loads(r.stdout);assert result['all_passed']
result['header_sha256']=hashlib.sha256((ROOT/'platform/graphics/DirtyPresentation.h').read_bytes()).hexdigest()
(OUT/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
