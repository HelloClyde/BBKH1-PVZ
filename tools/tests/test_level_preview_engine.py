"""Export day-camera and zombie poses through the original host shaders."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT/'build/host-first-level-fix')
    parser.add_argument('--output', type=Path, default=ROOT/'build/first-level-lifetime-regression')
    parser.add_argument('--cxx', default='C:/msys64/ucrt64/bin/g++.exe')
    parser.add_argument('--replay', action='store_true')
    args = parser.parse_args()
    host, output = args.host.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name in ('tmp', 'save', 'run'):
        (output/name).mkdir(exist_ok=True)
    env = os.environ.copy()
    env['PATH'] = str(Path(args.cxx).parent)+os.pathsep+env.get('PATH', '')
    env['TEMP'] = env['TMP'] = str(output/'tmp')
    report = json.loads((host/'compile-report.json').read_text())
    assert report['linked'] and not report['failed']
    objects = []
    for item in report['records']:
        name = item['source']
        assert sha(ROOT/name) == item['sha256'], name
        if name == 'platform/host/host_io.cpp':
            continue
        objects.append(host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o'))
    io = (ROOT/'platform/host/host_io.cpp').read_text(encoding='utf-8')
    anchor = 'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}'
    assert io.count(anchor) == 1
    io = 'extern "C" void pvzh1_level_preview_export();\n'+io.replace(anchor, 'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_level_preview_export();std::exit(0);}')
    if args.replay:
        io=io.replace('pvzh1_level_preview_export','pvzh1_level_preview_probe')
    observer = output/'observer.cpp'
    observer.write_text(io, encoding='utf-8')
    probe = ROOT/'tools/tests'/('level_preview_engine_probe.cpp' if args.replay else 'level_preview_export.cpp')

    def run(command, log):
        result = subprocess.run(list(map(str, command)), env=env, cwd=ROOT, capture_output=True, text=True)
        (output/log).write_text(result.stdout+result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(str(output/log)+'\n'+result.stderr[-3000:])

    for source in (observer, probe):
        obj = output/(source.stem+'.o')
        run([args.cxx, *report['flags'], '-std=c++20', '-c', source, '-o', obj], source.stem+'.compile.log')
        objects.append(obj)
    executable = output/'probe.exe'
    run([args.cxx, '-Wl,--gc-sections', '-static-libstdc++', '-static-libgcc', '-o', executable,
         *objects, '-lws2_32', '-luser32', '-lgdi32', '-lwinmm', '-limm32', '-lshlwapi', '-lshell32', '-lpsapi'], 'link.log')
    for name in list(env):
        if name.startswith('PVZ_HOST_'):
            del env[name]
    env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-wide').as_posix()+'/',
               PVZ_SAVE_DIR=(output/'save').as_posix()+'/', PVZ_HOST_OUTPUT=(output/'run').as_posix(),
               PVZ_HOST_MAX_FRAMES='600', PVZ_HOST_SPEED='100',
               PVZ_HOST_SELECTOR_BUNDLE=(ROOT/'private/resources-h1-wide/h1menu/selector-wide.h1mb').as_posix())
    with (output/'console.log').open('w', encoding='utf-8') as log:
        result = subprocess.run([str(executable)], cwd=ROOT, env=env, stdout=log, stderr=log)
    assert result.returncode == 0, f'Probe exit {result.returncode}; see {output}/console.log'
    verification = json.loads((output/'run'/('level-preview-replay-verification.json' if args.replay else 'zombie-compiler-verification.json')).read_text())
    assert verification['passed']
    verification.update(probe_sha256=sha(probe), executable_sha256=sha(executable),
                        compile_report_sha256=sha(host/'compile-report.json'),

                        scope='Original host game callbacks and real offline resources; not physical H1 timing')
    (output/'verification.json').write_text(json.dumps(verification, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(verification))


if __name__ == '__main__':
    main()
