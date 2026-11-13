"""Build actual XM helper and verify buffered FILE ownership/reuse/PCM."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default='C:/msys64/ucrt64/bin/gcc.exe');args=ap.parse_args()
    out=ROOT/'build/xmp-buffered-io-test';out.mkdir(parents=True,exist_ok=True)
    config=json.loads((ROOT/'platform/music_sources.json').read_text())
    sources=[ROOT/p for p in config['sources']]+[ROOT/'platform/xmp_shared.c',ROOT/'tools/tests/xmp_buffered_io_probe.c']
    flags=['-std=gnu99','-O2','-g','-Wall','-Wextra','-Wno-unused-parameter']
    flags+=['-D'+d for d in config['defines']]
    flags+=['-I'+str(ROOT/p) for p in config['includes']]+['-I'+str(ROOT/'platform')]
    env=dict(os.environ);env['PATH']=str(Path(args.cc).parent)+os.pathsep+env.get('PATH','')
    def run(command):
        p=subprocess.run(list(map(str,command)),capture_output=True,text=True,encoding='utf8',env=env)
        if p.returncode:raise RuntimeError(p.stdout+p.stderr)
        return p.stdout
    def compile(source):
        obj=out/(hashlib.sha1(str(source).encode()).hexdigest()[:10]+'.o')
        run([args.cc,*flags,*(['-Werror'] if 'vendor' not in source.parts else []),'-c',source,'-o',obj]);return obj
    with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile,sources))
    wrapped=['malloc','free','setvbuf','fopen','fclose','fread','fseek','fgetc','libxmp_scan_sequences']
    exe=out/'probe.exe';run([args.cc,*objects,*['-Wl,--wrap='+n for n in wrapped],'-lm','-o',exe])
    module=ROOT/'private/resources-h1-wide/sounds/mainmusic.xm'
    result=json.loads(run([exe,module,ROOT/'tools/test_xmp_shared.py']))
    result['scope']='Host-only actual loader/FILE lifecycle; call counts are stdio API calls, not native SDK reads or a native speed claim'
    result['implementation_sha256']=hashlib.sha256((ROOT/'platform/xmp_shared.c').read_bytes()).hexdigest()
    result['module_sha256']=hashlib.sha256(module.read_bytes()).hexdigest()
    result['probe_sha256']=hashlib.sha256((ROOT/'tools/tests/xmp_buffered_io_probe.c').read_bytes()).hexdigest()
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
