"""Pack original-shader exports at LCD density and record their provenance.

First run combat_sprite_export.cpp and pea_layered_export.cpp with
run_engine_probe.py. Pass their run directories here; no gameplay code is
used as an image decoder and no runtime resources are validated in-game.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess
from pack_actor_pages import encode, decode
from pack_actor_poses import pack
from bake_file_index import encode as file_index
ROOT = Path(__file__).resolve().parents[1]

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text(encoding='utf8'))
def write(p, d): p.write_text(json.dumps(d, ensure_ascii=False, indent=2)+'\n', encoding='utf8')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--combat-export', type=Path, required=True)
    ap.add_argument('--head-export', type=Path, required=True)
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--cxx', default='C:/msys64/ucrt64/bin/g++.exe')
    a = ap.parse_args(); a.work.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy(); env['PATH'] = str(Path(a.cxx).parent)+';'+env.get('PATH', '')
    exe = a.work/'pack-combat.exe'
    subprocess.run([a.cxx, '-std=c++20', '-O2', str(ROOT/'tools/pack_extra_actor.cpp'), '-lz', '-o', str(exe)], env=env, check=True)
    for source, name in [(a.combat_export/'combat-sprites.raw', 'combat-sprites.h1ap'), (a.head_export/'pea-layered.raw', 'pea-transitions.h1ap')]:
        result = subprocess.run([str(exe.resolve()), str(source.resolve()), str((a.work/name).resolve())], env=env, check=True, capture_output=True, text=True)
        write(a.work/(name+'.json'), json.loads(result.stdout))
    # Small direct sprites cost less resident RAM raw than a compressed copy
    # plus inflate arena, page table and page cache.
    direct = a.work/'combat-sprites.h1ap'; direct.write_bytes(decode(direct.read_bytes()))
    hats = a.work/'zombie-hats.h1ap'
    pack(a.combat_export/'zombie-hats.raw', hats, a.work/'zombie-hats.json')
    raw = hats.read_bytes(); hats.write_bytes(encode(raw)); assert decode(hats.read_bytes()) == raw
    runtime = ROOT/'private/resources-h1-wide'; archive = ROOT/'private/resource-build-inputs/retired'
    archive.mkdir(parents=True, exist_ok=True)
    for name in ('pea-transitions.h1ap', 'plant-shadows.h1ap'):
        old = runtime/name; saved = archive/name
        if old.exists() and not saved.exists(): shutil.copyfile(old, saved)
    (runtime/'plant-shadows.h1ap').unlink(missing_ok=True)
    proof = dict(schema=1, dimensions=[480,272], frames_are_full_original_shading=True,
                 visual_translation='Round LCD translation; simulation coordinates unchanged',
                 splat_variants=4, original_particle_physics_and_rng=True,
                 original_blend_updates=20, files={}, source_files={}, export_proofs={})
    for name in ('combat-sprites.h1ap','zombie-hats.h1ap','pea-transitions.h1ap'):
        shutil.copyfile(a.work/name, runtime/name)
        blob=(runtime/name).read_bytes(); raw=decode(blob) if blob[:4]==b'H1PZ' else blob
        proof['files'][name]=dict(bytes=len(blob),sha256=sha(runtime/name),decoded_bytes=len(raw),decoded_sha256=hashlib.sha256(raw).hexdigest())
    for name in ('combat_sprite_export.cpp','pea_layered_export.cpp'):
        proof['source_files']['tools/tests/'+name]=sha(ROOT/'tools/tests'/name)
    for name in ('tools/pack_extra_actor.cpp','tools/pack_actor_poses.py','tools/pack_actor_pages.py', 'tools/bake_combat_sprites.py'):
        proof['source_files'][name]=sha(ROOT/name)
    for name, directory in [('combat-sprite-export-verification.json',a.combat_export), ('pea-layered-export-verification.json',a.head_export)]:
        report=read(directory/name); assert report['passed']; proof['export_proofs'][name]=report
    write(ROOT/'provenance/offline-combat-sprites.json',proof)
    shadow=ROOT/'provenance/offline-plant-shadows.json'; d=read(shadow)
    d.update(retired=True,archive_path='private/resource-build-inputs/retired/plant-shadows.h1ap',replacement='combat-sprites.h1ap',reason='Reuse one bitmap per display scale at arbitrary position; do not retain 280 grid-position packets')
    write(shadow,d)
    phase=ROOT/'provenance/offline-actor-phases.json';d=read(phase)
    for r in d['files']:
        if r['runtime_path']=='pea-transitions.h1ap': r.update(original_archive_path='private/resource-build-inputs/retired/pea-transitions.h1ap',replacement_provenance='provenance/offline-combat-sprites.json')
    write(phase,d)
    inputs=ROOT/'provenance/source-assets.json';d=read(inputs);tree=next(t for t in d['trees'] if t['directory']=='private/resource-build-inputs')
    for name in ('pea-transitions.h1ap','plant-shadows.h1ap'):
        rel='retired/'+name;p=archive/name
        if rel not in tree['files']:tree['bytes']+=p.stat().st_size
        tree['files'][rel]=sha(p)
    write(inputs,d)
    # Update only changed inventory entries. Unchanged resource evidence is
    # reused; no repeated whole-tree content hashing is needed.
    inv=ROOT/'provenance/resources.json';d=read(inv);old_bytes=d['bytes']
    data, entries=file_index(runtime)
    (runtime/'resource-files.h1fi').write_bytes(data)
    for name in proof['files']:d['files'][name]=sha(runtime/name)
    d['files'].pop('plant-shadows.h1ap',None);d['files']['resource-files.h1fi']=sha(runtime/'resource-files.h1fi')
    d['bytes']=sum(p.stat().st_size for p in runtime.rglob('*') if p.is_file());d['count']=len(d['files']);write(inv,d)
    write(ROOT/'provenance/offline-file-index.json',dict(version=1,runtime_path='resource-files.h1fi',bytes=len(data),sha256=d['files']['resource-files.h1fi'],entries=len(entries),files=sum(not flag for size,flag in entries.values()),directories=sum(flag for size,flag in entries.values()),encoder_sha256=sha(ROOT/'tools/bake_file_index.py'),scope='Immutable resource tree; savedata and index excluded'))
    opt=ROOT/'provenance/runtime-resource-optimization.json';o=read(opt)
    for name, info in proof['files'].items():o['changed'][name]=dict(reason='LCD-density direct combat sprites / layer-preserving head transitions / damaged hats',**{k:info[k] for k in ('bytes','sha256')})
    write(opt,o)
    print(json.dumps(dict(passed=True,runtime_files=d['count'],runtime_bytes=d['bytes'],runtime_delta=d['bytes']-old_bytes)))

if __name__=='__main__':main()
