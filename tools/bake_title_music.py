"""Render the original title tracker loop, with a sample-for-sample replay check."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host-build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--resources',type=Path,default=ROOT/'private/resources-h1-wide');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 report=json.loads((a.host_build/'compile-report.json').read_text());objects=[]
 for record in report['records']:
  name=record['source']
  if name.startswith('vendor/libxmp/'):
   objects.append(a.host_build/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o'))
 exe=out/'bake-title.exe';env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 subprocess.run(['C:/msys64/ucrt64/bin/g++.exe','-O2','-std=c++20','-DLIBXMP_STATIC','-I',str(ROOT/'vendor/libxmp/include'),str(ROOT/'tools/audio/bake_title.cpp'),*map(str,objects),'-static-libgcc','-static-libstdc++','-o',str(exe)],cwd=ROOT,env=env,check=True)
 output=out/'mainmusic-title.h1pcm';source=a.resources/'sounds/mainmusic.xm';r=subprocess.run([str(exe),str(source.resolve()),str(output)],cwd=ROOT,env=env,capture_output=True)
 (out/'export.log').write_bytes(r.stdout+r.stderr)
 if r.returncode:raise RuntimeError('Title export failed: '+str(r.returncode)+' '+r.stdout.decode(errors='replace')+r.stderr.decode(errors='replace'))
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 manifest={'source_sha256':sha(source),'output_sha256':sha(output),'bytes':output.stat().st_size,'rate':16000,'channels':1,'bits':16,'start_order':152,'whole_loop_sample_exact':True,'exporter_sha256':sha(ROOT/'tools/audio/bake_title.cpp')}
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(r.stdout.decode());print(json.dumps(manifest))
if __name__=='__main__':main()
