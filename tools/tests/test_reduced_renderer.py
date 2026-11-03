"""Run the real reduced-texture rasterizer against nine contrasting cells.

Build the complete host first. The fixture isolates its current source/header/
object identities before compiling; all test artifacts stay below build/.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host-build', type=Path, default=Path('build/host-scaled-full'))
    parser.add_argument('--resources', type=Path, default=Path('private/resources-h1'))
    parser.add_argument('--cxx', type=Path, default=Path('C:/msys64/ucrt64/bin/g++.exe'))
    args = parser.parse_args()
    host = (ROOT / args.host_build).resolve()
    output = ROOT / 'build/reduced-render-probe'
    output.mkdir(parents=True, exist_ok=True)
    for name in ('objects', 'tmp', 'run', 'save'):
        (output / name).mkdir(exist_ok=True)
    report_path = host / 'compile-report.json'
    report = json.loads(report_path.read_text())
    assert report['linked'] and not report['failed']
    shutil.copytree(host / 'source', output / 'source', dirs_exist_ok=True)
    shutil.copytree(ROOT / 'platform', output / 'platform', dirs_exist_ok=True)
    objects, identities = [], {}
    for row in report['records']:
        name = row['source']
        if name == 'platform/host/host_io.cpp':
            continue  # Observer is recompiled below with the fixture checkpoint.
        assert sha(ROOT / name) == row['sha256'], name
        obj = host / (hashlib.sha1(name.encode()).hexdigest()[:10] + '-' + Path(name).stem + '.o')
        target = output / 'objects' / obj.name
        digest = sha(obj)
        shutil.copyfile(obj, target)
        assert sha(target) == digest
        objects.append(target)
        identities[name] = dict(source=row['sha256'], object=digest)
    flags = []
    for flag in report['flags']:
        for original, snapshot in ((host / 'source', output / 'source'), (ROOT / 'platform', output / 'platform')):
            flag = flag.replace(str(original), str(snapshot)).replace(original.as_posix(), snapshot.as_posix())
        flags.append(flag)
    source = Path(__file__).with_name('reduced_render_probe.cpp')
    shutil.copyfile(source, output / 'probe.cpp')
    observer = (output / 'platform/host/host_io.cpp').read_text()
    observer = observer.replace('extern "C" void pvzh1_host_probe_fonts();',
                                'extern "C" void pvzh1_host_probe_fonts();\nextern "C" void pvzh1_reduced_render_probe();')
    needle = 'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();}'
    assert observer.count(needle) == 1
    observer = observer.replace(needle, 'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_reduced_render_probe();std::exit(0);}')
    (output / 'host_io.cpp').write_text(observer)
    env = dict(os.environ)
    env['PATH'] = str(args.cxx.parent) + os.pathsep + env.get('PATH', '')
    env['TMP'] = env['TEMP'] = str(output / 'tmp')

    def run(command, log):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, env=env)
        (output / log).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stderr)

    for src in (output / 'host_io.cpp', output / 'probe.cpp'):
        obj = src.with_suffix('.o')
        run([args.cxx, *flags, '-std=c++20', '-c', src, '-o', obj], src.stem + '.compile.log')
        objects.append(obj)
    exe = output / 'probe.exe'
    run([args.cxx, '-Wl,--gc-sections', '-static-libstdc++', '-static-libgcc', '-o', exe, *objects,
         '-lws2_32', '-luser32', '-lgdi32', '-lwinmm', '-limm32', '-lshlwapi', '-lshell32', '-lpsapi'], 'link.log')
    env.update(PVZ_RESOURCE_DIR=(ROOT / args.resources).as_posix() + '/', PVZ_SAVE_DIR=(output / 'save').as_posix() + '/',
               PVZ_HOST_OUTPUT=(output / 'run').as_posix(), PVZ_HOST_MAX_FRAMES='600', PVZ_HOST_SPEED='100')
    for key in list(env):
        if key.startswith('PVZ_HOST_') and key not in ('PVZ_HOST_OUTPUT', 'PVZ_HOST_MAX_FRAMES', 'PVZ_HOST_SPEED'):
            del env[key]
    with (output / 'console.log').open('w') as log:
        subprocess.run([str(exe)], cwd=ROOT, env=env, stdout=log, stderr=log, check=True)
    verification = json.loads((output / 'run/reduced-render-verification.json').read_text())
    assert verification['all_passed']
    identity = dict(test_only=True, compile_report_sha256=sha(report_path), objects=identities,
                    probe_sha256=sha(source), executable_sha256=sha(exe))
    (output / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n')
    print('PASS', verification['cases'], 'real raster cases')


if __name__ == '__main__':
    main()
