"""Link against the existing pinned libxmp build and verify the award slice."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host-build',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
report=json.loads((a.host_build/'compile-report.json').read_text())
objects=[a.host_build/(hashlib.sha1(r['source'].encode()).hexdigest()[:10]+'-'+Path(r['source']).stem+'.o') for r in report['records'] if r['source'].startswith('vendor/libxmp/')]
env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
exe=a.output.resolve()/'verify-award.exe'
subprocess.run(['C:/msys64/ucrt64/bin/g++.exe','-O2','-std=c++20','-DLIBXMP_STATIC','-I',str(ROOT/'vendor/libxmp/include'),str(ROOT/'tools/audio/verify_award.cpp'),*map(str,objects),'-static-libgcc','-static-libstdc++','-o',str(exe)],env=env,check=True)
inputs=[ROOT/'private/resources-h1-wide/sounds'/n for n in ('mainmusic.xm','mainmusic-award.xm')]
r=subprocess.run([str(exe),*map(str,inputs)],env=env,capture_output=True)
(a.output/'verify.log').write_bytes(r.stdout+r.stderr)
if r.returncode:raise RuntimeError(str(r.returncode)+' '+r.stderr.decode(errors='replace'))
proof=json.loads(r.stdout);proof['input_sha256']={n.name:hashlib.sha256(n.read_bytes()).hexdigest() for n in inputs}
proof['verifier_sha256']=hashlib.sha256((ROOT/'tools/audio/verify_award.cpp').read_bytes()).hexdigest()
(a.output/'verification.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
