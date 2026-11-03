"""Exercise the exact native pressure retry loop with a fragmented mock heap."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def execute(command):
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    if result.returncode:
        raise RuntimeError(result.stdout+result.stderr)
    return result.stdout

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=shutil.which('gcc') or 'gcc')
    args=parser.parse_args()
    out=ROOT/'build/pressure-retry-test'
    out.mkdir(parents=True,exist_ok=True)
    sources=[ROOT/'runtime/pressure_retry.c',ROOT/'tools/test_pressure_retry.c']
    executable=out/'test-pressure-retry.exe'
    flags=['-std=c11','-Wall','-Wextra','-Werror','-O2']
    execute([args.cc,*flags,*sources,'-o',executable])
    output=execute([executable]).strip()
    report={
        'passed':True,'compiler':execute([args.cc,'--version']).splitlines()[0],
        'flags':flags,'retry_limit':65536,'stdout':output,
        'sources':{str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        'checks':['no-pressure-success','multiple-releases-and-coalescing',
                  'stop-after-native-success','fragmented-aggregate-not-a-fit',
                  'terminal-no-eligible-source','callback-reentry','native-retry-reentry',
                  'missing-optional-hook','native-failure-sentinel',
                  'saturating-byte-counter','bounded-nonprogress-callback']}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(output)

if __name__=='__main__':main()
