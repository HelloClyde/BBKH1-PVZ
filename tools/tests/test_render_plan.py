"""Render-plan lifecycle unit checks and the current complete-engine oracle."""
from pathlib import Path
import argparse,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--host',type=Path,default=ROOT/'build/host-physical')
 p.add_argument('--output',type=Path,default=ROOT/'build/render-plan-probe')
 p.add_argument('--cxx',type=Path,default=Path('C:/msys64/ucrt64/bin/g++.exe'))
 a=p.parse_args();host=a.host.resolve();out=a.output.resolve()
 assert out.is_relative_to(ROOT/'build') and not out.exists(),'Choose a fresh output below build/'
 assert (host/'compile-report.json').is_file(),'Build the complete host engine first'
 out.mkdir(parents=True);env=dict(os.environ);env['PATH']=str(a.cxx.parent)+os.pathsep+env.get('PATH','')
 def run(command,log):
  r=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,capture_output=True,text=True)
  (out/log).write_text(r.stdout+r.stderr,encoding='utf8');assert not r.returncode,r.stderr
 run([a.cxx,'-DPVZH1_PLAN_STANDALONE','-std=c++20','-Os','-static-libstdc++','-static-libgcc',
      '-I',ROOT/'platform/graphics','-I',host/'source/SexyAppFramework',
      ROOT/'tools/tests/render_plan_unit.cpp',ROOT/'platform/graphics/RenderPlanCache.cpp','-o',out/'unit.exe'],'unit.compile.log')
 run([out/'unit.exe'],'unit-result.json');unit=json.loads((out/'unit-result.json').read_text());assert unit['passed']
 run([sys.executable,ROOT/'tools/tests/run_engine_probe.py','--host',host,
      '--probe',ROOT/'tools/tests/render_plan_probe.cpp','--entry','pvzh1_render_plan_probe','--output',out/'engine'],'engine.log')
 engine=json.loads((out/'engine/run/render-plan-verification.json').read_text());assert engine['passed']
 result=dict(passed=True,scope='Host correctness and lifecycle; no H1 FPS claim',unit=unit,engine=engine)
 (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
