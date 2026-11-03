"""Compile the actual H1 logical-to-physical boundary helper (no game state)."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='C:/msys64/ucrt64/bin/g++.exe')
    args = parser.parse_args()
    output = ROOT / 'build/reduced-mapping-test'
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'tools/tests/reduced_mapping_probe.cpp'
    header = ROOT / 'platform/graphics/ReducedTextureMapping.h'
    executable = output / 'probe.exe'
    env = dict(os.environ)
    env['PATH'] = str(Path(args.cxx).parent) + os.pathsep + env.get('PATH', '')
    command = [args.cxx, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(header.parent), str(source), '-o', str(executable)]
    subprocess.run(command, check=True, env=env)
    run = subprocess.run([str(executable)], check=True, capture_output=True, text=True, env=env)
    report = {'passed': True, 'scope': 'Actual reduced source boundary mapping; no emulator claim',
              'output': run.stdout.strip(), 'command': command,
              'header_sha256': hashlib.sha256(header.read_bytes()).hexdigest(),
              'probe_sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(run.stdout.strip())


if __name__ == '__main__':
    main()
