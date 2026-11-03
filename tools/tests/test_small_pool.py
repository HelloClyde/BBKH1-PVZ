"""Exercise actual standalone C small slabs without syscalls/device integration."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/small-pool-test'
OUT.mkdir(parents=True, exist_ok=True)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='C:/msys64/ucrt64/bin/gcc.exe')
args = parser.parse_args()
env = os.environ.copy()
env['PATH'] = str(Path(args.cc).parent) + os.pathsep + env.get('PATH', '')
exe = OUT / 'probe.exe'
command = [args.cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
           '-I', str(ROOT / 'runtime'), str(ROOT / 'runtime/small_pool.c'),
           str(ROOT / 'tools/tests/small_pool_probe.c'), '-o', str(exe)]
subprocess.run(command, check=True, env=env, timeout=60)
completed = subprocess.run([str(exe)], check=True, capture_output=True, text=True, env=env, timeout=90)
report = json.loads(completed.stdout)
report.update(scope='Actual C small_pool implementation; SDK callbacks replaced by guarded host backing memory. No syscalls integration or BDA build.',
              sources={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in [ROOT / 'runtime/small_pool.c', ROOT / 'runtime/small_pool.h',
                                 ROOT / 'tools/tests/small_pool_probe.c']}, command=command)
(OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
