"""Exercise production save replacement against real files and injected I/O faults."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=ROOT/'build/save-file-host-v1');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'build');out.mkdir(exist_ok=False)
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 sources=[ROOT/'platform/SaveFile.cpp',ROOT/'tools/tests/save_file_probe.cpp']
 command=['C:/msys64/ucrt64/bin/g++.exe','-std=c++20','-O2','-I'+str(ROOT/'platform'),*map(str,sources),'-Wl,--wrap=write','-Wl,--wrap=read','-Wl,--wrap=close','-Wl,--wrap=rename','-o',str(out/'probe.exe')]
 r=subprocess.run(command,env=env,capture_output=True,text=True);(out/'compile.log').write_text(r.stdout+r.stderr,encoding='utf8');assert not r.returncode,r.stderr
 r=subprocess.run([str(out/'probe.exe'),str(out)],env=env,capture_output=True,text=True);(out/'run.log').write_text(r.stdout+r.stderr,encoding='utf8');assert not r.returncode,r.stderr
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 (out/'identity.json').write_text(json.dumps({str(p.relative_to(ROOT)):sha(p) for p in sources},indent=2)+'\n')
 print((out/'verification.json').read_text('utf8'))
if __name__=='__main__':main()
