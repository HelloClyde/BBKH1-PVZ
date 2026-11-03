"""Exercise actual syscalls pool integration and shutdown with a native-span mock.

The host's pointers enlarge Allocation (its size is reported by the probe);
the MIPS build separately enforces the original 24-byte header. Ownership and
accounting code is unmodified.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = ROOT / 'runtime/syscalls.c'
    text = source.read_text(encoding='utf8')
    first = text[text.index('typedef union Allocation'):text.index('static int stat_native(')]
    first = re.sub(r'^_Static_assert\([^\n]+\n', '', first, flags=re.M)
    split = first.index('static void first_allocation_failure(')
    globals_body, failure = first[:split], first[split:]
    allocator = text[text.index('static void update_small_pool_diagnostic('):text.index('int _open(')]
    shutdown = text[text.index('void pvz_runtime_shutdown('):]
    template = Path(__file__).with_name('runtime_pool_probe.c.in')
    probe_text = template.read_text(encoding='utf8')
    for key, body in [('GLOBALS', globals_body), ('FAILURE', failure), ('ALLOCATOR', allocator), ('SHUTDOWN', shutdown)]:
        marker = f'/* @{key}@ */'
        assert probe_text.count(marker) == 1
        probe_text = probe_text.replace(marker, body)
    output = ROOT / 'build/runtime-pool-integration'
    output.mkdir(parents=True, exist_ok=True)
    probe = output / 'probe.c'
    probe.write_text(probe_text, encoding='utf8')
    exe = output / 'probe.exe'
    sources = [probe, ROOT / 'runtime/small_pool.c', ROOT / 'runtime/pressure_retry.c']
    command = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
               '-I', str(ROOT / 'runtime'), *map(str, sources), '-o', str(exe)]
    subprocess.run(command, check=True)
    run = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
    pressure_exe = output / 'pressure.exe'
    pressure_command = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'runtime/pressure_retry.c'), str(ROOT / 'tools/test_pressure_retry.c'),
                        '-o', str(pressure_exe)]
    subprocess.run(pressure_command, check=True)
    pressure = subprocess.run([str(pressure_exe)], check=True, capture_output=True, text=True)
    evidence = [source, ROOT / 'runtime/small_pool.c', ROOT / 'runtime/small_pool.h', ROOT / 'runtime/pressure_retry.c',
                ROOT / 'tools/test_pressure_retry.c', Path(__file__), template, probe]
    report = dict(passed=True, scope=__doc__, output=run.stdout.strip(), command=command,
                  pressure_command=pressure_command, pressure_output=pressure.stdout.strip(),
                  source_sha256={p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in evidence},
                  device_access_performed=False, native_abi_enforced_by_production_static_assert=True)
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(run.stdout.strip())
    print(pressure.stdout.strip())


if __name__ == '__main__':
    main()
