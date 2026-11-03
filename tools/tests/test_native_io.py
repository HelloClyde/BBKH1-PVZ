"""Compile actual native I/O instrumentation with deterministic SDK mocks."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    output = ROOT / 'build/native-io-tests'
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'runtime/syscalls.c'
    text = source.read_text(encoding='utf8')
    bodies = [text[text.index('char pvz_runtime_last_path['):text.index('static void detect_resource_drive')],
              text[text.index('ssize_t _read('):text.index('ssize_t _write(')],
              text[text.index('off_t _lseek('):text.index('static int stat_native(const char *native,')],
              text[text.index('int _fstat('):text.index('int _isatty(')]]
    template = Path(__file__).with_name('native_io_probe.c.in')
    value = template.read_text()
    assert value.count('/* @IO_IMPLEMENTATION@ */') == 1
    probe = output / 'probe.c'
    probe.write_text(value.replace('/* @IO_IMPLEMENTATION@ */', '\n'.join(bodies)))
    executable = output / 'probe.exe'
    command = [args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-Wno-misleading-indentation', str(probe), '-o', str(executable)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    report = dict(passed=True, source_sha256=sha(source), probe_sha256=sha(probe),
                  template_sha256=sha(template), command=command, output=result.stdout.strip(),
                  device_access_performed=False, timebase_frequency_assumed=False)
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(result.stdout.strip())


if __name__ == '__main__':
    main()
