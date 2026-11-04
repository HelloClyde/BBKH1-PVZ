"""Export H1 menu pixels using an isolated copy of the original software renderer.

The sole raster modification is an offline final-fragment observation hook.
Original frames, per-track ordering, image sampling and blend math execute normally.
Production source, original resources and save data are not modified.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, struct, subprocess, zlib

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def patch_capture(source):
    graphics = source/'SexyAppFramework/graphics'
    path = graphics/'SWTri.cpp'
    text = path.read_text(encoding='utf8')
    declaration = 'extern "C" void pvzh1_menu_bake_fragment(uint32_t*,uint32_t,uint32_t,int);\n'
    anchor = '#include "RenderPlanCache.h"'
    assert text.count(anchor) == 1
    path.write_text(text.replace(anchor, anchor+'\n'+declaration), encoding='utf8')
    path = graphics/'SWTri/SWTri_Pixel888.inc'
    text = path.read_text(encoding='utf8')
    partial = '*pix = 0xFF000000 | ((trb|tg) + (prb|pg));'
    assert text.count(partial) == 1
    text = text.replace(partial, 'pvzh1_menu_bake_fragment(pix,trb|tg,alpha,0);\n\t\t\t\t\t'+partial)
    opaque = '*pix = 0xFF000000 | tex;'
    assert text.count(opaque) == 2
    text = text.replace(opaque, 'pvzh1_menu_bake_fragment(pix,0xFF000000u|tex,0,1);\n\t\t\t\t\t'+opaque)
    path.write_text(text, encoding='utf8')

def encode_segments(manifest_path, output, compressed=False):
    manifest = json.loads(manifest_path.read_text())
    width,height=manifest['physical_width'],manifest['physical_height']
    logical_width,logical_height=manifest.get('logical_width',800),manifest.get('logical_height',600)
    encoded = []
    codec_checks = 0
    for segment in manifest['segments']:
        key = segment['key']
        if key.startswith('hand-'):
            kind,identifier,variant,color=3,int(key.split('-')[1]),0,0xffffffff
        elif key.startswith('track-'):
            _, identifier, variant = key.split('-')
            kind, identifier, color = 1, int(identifier), segment['color']
            variant = dict(normal=0, press=1, almanac=2, key=3)[variant]
        else:
            _, identifier, tint, state = key.split('-')
            kind, identifier, variant = 2, int(identifier), int(state)
            if tint in ('start','start80'):
                variant |= 256
                tint = 80 if tint=='start80' else 255
            tint = int(tint)
            color = 0xff000000 | tint << 16 | tint << 8 | tint
        raw = (manifest_path.parent/'segments'/segment['file']).read_bytes()
        assert len(raw) == segment['fragments'] * 16
        operations = list(struct.iter_unpack('<4I', raw))
        # Earlier fragments covered by a later opaque fragment cannot affect
        # any background. Keep the remaining fragments in their original order.
        if kind==3:
            covered=set();keep=[]
            for op in reversed(operations):
                if op[0] in covered:continue
                keep.append(op)
                if op[3]:covered.add(op[0])
            reduced=list(reversed(keep))
            def replay(ops,seed):
                values={}
                for dst,premul,inv,opaque in ops:
                    old=values.get(dst,0xff000000|((dst*0x164b35+seed*0x213141)&0xffffff))
                    rb=(((old&0xff00ff)*inv)>>8)&0xff00ff
                    green=(((old&0xff00)*inv)>>8)&0xff00
                    values[dst]=premul if opaque else 0xff000000|(premul+(rb|green))
                return values
            for seed in (1,17,231):assert replay(operations,seed)==replay(reduced,seed)
            operations=reduced
        runs, payload = [], []
        previous = -2
        for destination, premul, inverse, opaque in operations:
            assert destination < width*height and inverse < 256 and opaque <= 1
            op_kind = 0 if opaque else 1
            value = premul if opaque else premul | inverse << 24
            assert opaque or premul < 1 << 24
            if runs and destination == previous + 1 and destination//width == previous//width and runs[-1][2] == op_kind:
                runs[-1][1] += 1
            else:
                runs.append([destination, 1, op_kind, len(payload)])
            previous = destination
            payload.append(value)
        data = b''.join(struct.pack('<IHHI', *r) for r in runs) + struct.pack('<'+'I'*len(payload), *payload)
        # Decode all merged runs back to the original ordered fragments.
        # This checks row boundaries, payload offsets, alpha packing and order.
        rebuilt = []
        for destination, count, run_kind, word in runs:
            assert count and destination//width == (destination+count-1)//width
            for i, value in enumerate(payload[word:word+count]):
                rebuilt.append((destination+i, value if run_kind==0 else value&0xffffff,
                                0 if run_kind==0 else value>>24, 1 if run_kind==0 else 0))
        assert rebuilt == operations
        codec_checks += 1
        encoded.append(dict(kind=kind, id=identifier, variant=variant, color=color,
                            run_count=len(runs), pixel_count=len(payload), data=data, source=segment))
    directory = bytearray()
    offset = 64 + len(encoded)*40
    for entry in encoded:
        data = entry['data']
        meta = entry['source']
        geometry = ((meta['x']&65535)|((meta['y']&65535)<<16), (meta['width']&65535)|((meta['height']&65535)<<16)) if entry['kind']==2 else (0,0)
        if compressed:data=zlib.compress(data,9)
        entry['packed_data']=data
        directory += struct.pack('<10I', entry['kind'], entry['id'], entry['variant'], entry['color'], entry['run_count'], entry['pixel_count'], offset, len(data), *geometry)
        offset += len(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    header = struct.pack('<16I', 0x424d3148, 1, 64, offset, width, height, logical_width, logical_height, len(encoded), 64, 40, 40, 48, int(compressed), 0, 0)
    output.write_bytes(header + directory + b''.join(e['packed_data'] for e in encoded))
    return dict(file=str(output), bytes=offset, sha256=sha(output), entry_count=len(encoded),
                runs=sum(e['run_count'] for e in encoded), pixels=sum(e['pixel_count'] for e in encoded),
                original_vs_replay_checks=manifest['original_vs_replay_checks'],
                codec_roundtrip_checks=codec_checks,
                entries=[{k:v for k,v in e.items() if k not in ('data','packed_data')} for e in encoded])

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host-build', type=Path, default=ROOT/'build/host-physical')
    ap.add_argument('--prepared-source', type=Path, default=ROOT/'build/native-hardware-opt-memo/source')
    ap.add_argument('--build-dir', type=Path, default=ROOT/'build/selector-bake')
    ap.add_argument('--output', type=Path, default=ROOT/'private/menu-bake/selector.h1mb')
    ap.add_argument('--cxx', type=Path, default=Path('C:/msys64/ucrt64/bin/g++.exe'))
    ap.add_argument('--encode-only', action='store_true')
    ap.add_argument('--adventure-only', action='store_true')
    ap.add_argument('--resources',type=Path,default=ROOT/'private/resources-h1-full')
    args = ap.parse_args()
    out, host = args.build_dir.resolve(), args.host_build.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if not args.encode_only:
        report = json.loads((host/'compile-report.json').read_text())
        assert report['linked'] and not report['failed']
        shutil.copytree(args.prepared_source, out/'source', dirs_exist_ok=True)
        patch_capture(out/'source')
        (out/'run').mkdir(exist_ok=True)
        (out/'save').mkdir(exist_ok=True)
        (out/'tmp').mkdir(exist_ok=True)
        flags = []
        for flag in report['flags']:
            flag = flag.replace(str(host/'source'), str(out/'source')).replace((host/'source').as_posix(), (out/'source').as_posix())
            flags.append(flag)
        flags += ['-std=c++20', '-I', str(ROOT/'runtime')]
        env = dict(os.environ)
        env['PATH'] = str(args.cxx.parent)+os.pathsep+env.get('PATH','')
        env['TEMP'] = env['TMP'] = str(out/'tmp')
        changed = ['source/SexyAppFramework/graphics/SWTri.cpp', 'source/PvzpLib/Reanimator.cpp', 'source/Lawn/Widget/GameSelector.cpp',
                   'platform/graphics/SoftwareRaster.cpp', 'platform/graphics/RenderPlanCache.cpp', 'platform/graphics/MemoryImageCache.cpp', 'platform/graphics/PhysicalScreen.cpp']
        observer = (ROOT/'platform/host/host_io.cpp').read_text()
        anchor = 'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}'
        assert observer.count(anchor) == 1
        observer = observer.replace(anchor, 'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_export_selector_bake();std::exit(0);}')
        observer = 'extern "C" void pvzh1_export_selector_bake();\n' + observer
        (out/'observer.cpp').write_text(observer)
        objects, identities = [], {}
        for row in report['records']:
            name = row['source']
            if name == 'platform/host/host_io.cpp' or any(name.endswith(n) or name == n for n in changed):
                continue
            obj = host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o')
            assert obj.exists(), obj
            objects.append(obj)
        def run(command, log):
            result = subprocess.run(list(map(str,command)), capture_output=True, text=True, env=env)
            (out/log).write_text(result.stdout+result.stderr)
            if result.returncode:
                raise RuntimeError(result.stderr[-6000:])
        for i, name in enumerate(changed):
            source = out/name if name.startswith('source/') else ROOT/name
            obj = out/f'changed-{i}.o'
            run([args.cxx,*flags,'-c',source,'-o',obj], f'compile-{i}.log')
            objects.append(obj)
            identities[name] = sha(source)
        for i, source in enumerate((out/'observer.cpp', ROOT/'tools/selector_bake_export.cpp')):
            obj = out/f'extra-{i}.o'
            run([args.cxx,*flags,'-c',source,'-o',obj], f'extra-{i}.log')
            objects.append(obj)
        exe = out/'export.exe'
        run([args.cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'], 'link.log')
        env.update(PVZ_RESOURCE_DIR=args.resources.resolve().as_posix()+'/', PVZ_SAVE_DIR=(out/'save').as_posix()+'/',
                   PVZ_HOST_OUTPUT=(out/'run').as_posix(), PVZ_HOST_SPEED='100', PVZ_HOST_MAX_FRAMES='600')
        for k in list(env):
            if k.startswith('PVZ_HOST_') and k not in ('PVZ_HOST_OUTPUT','PVZ_HOST_SPEED','PVZ_HOST_MAX_FRAMES'):
                del env[k]
        if args.adventure_only:env['PVZ_BAKE_ADVENTURE']='1'
        with (out/'console.log').open('w') as log:
            subprocess.run([str(exe)], cwd=ROOT, env=env, stdout=log, stderr=log, check=True)
        (out/'build-identity.json').write_text(json.dumps(dict(host_report=sha(host/'compile-report.json'), prepared_source=str(args.prepared_source), changed_sources=identities, executable=sha(exe)),indent=2)+'\n')
    result = encode_segments(out/'run/segments.json', args.output, args.adventure_only)
    result['passed'] = True
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='entries'}))

if __name__ == '__main__':
    main()
