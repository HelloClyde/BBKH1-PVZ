"""Compile the actual native path and stdout functions with bounded SDK mocks.

This checks fixed GBK path bytes, A-first/B-fallback selection, shared resource/
save/log roots, sentinel failures and output bounds. It never contacts a device.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    args = parser.parse_args()
    output = ROOT / 'build/path-map-tests'
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'runtime/syscalls.c'
    text = source.read_text(encoding='utf8')
    # Use the production function bodies verbatim, not a mirrored path policy.
    bodies = [text[text.index('static char virtual_cwd['):text.index('static uint32_t last_tick')],
              text[text.index('char pvz_runtime_last_path['):text.index('static int native_failure')],
              text[text.index('static int pvz_map_native_path('):text.index('uint32_t pvz_runtime_ticks(')],
              text[text.index('ssize_t _write('):text.index('off_t _lseek(')]]
    template = Path(__file__).with_name('native_paths_probe.c.in')
    value = template.read_text()
    assert value.count('/* @PATH_IMPLEMENTATION@ */') == 1
    probe = output / 'probe.c'
    probe.write_text(value.replace('/* @PATH_IMPLEMENTATION@ */', '\n'.join(bodies)))
    executable = output / 'probe.exe'
    command = [args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-Wno-misleading-indentation', str(probe), '-o', str(executable)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
    bucket_tree = ROOT / 'private/resources-h1-hardware-patch/H1DATA'
    bucket_cases = 0
    if bucket_tree.is_dir():
        paths = sorted(p.relative_to(bucket_tree).as_posix() for p in bucket_tree.rglob('*') if p.is_file())
        logical = [path.split('/', 1)[1] for path in paths]
        mapped = subprocess.run([str(executable), '--map'], input='\n'.join(logical) + '\n',
                                check=True, capture_output=True, text=True)
        actual = [line.replace('\\', '/') for line in mapped.stdout.splitlines()]
        assert actual == ['H1DATA/' + path for path in paths], 'Native mapping differs from the offline bucket tree'
        bucket_cases = len(paths)
    report = dict(passed=True, scope=__doc__, source_sha256=sha(source), probe_sha256=sha(probe),
                  template_sha256=sha(template), command=command, output=result.stdout.strip(),
                  native_root_a_gbk_hex='A:\\应用\\数据\\游戏\\PVZ'.encode('gbk').hex(),
                  offline_bucket_paths_compared=bucket_cases,
                  physical_device_used=False, firmware_emulator_used=False)
    (output / 'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf8')
    print(result.stdout.strip())


if __name__ == '__main__':
    main()
