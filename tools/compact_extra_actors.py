"""Repack verified actor captures with exact tiles; retain smaller legacy banks.

All heavy work is offline. The runtime accepts both baked formats. It never
chooses a software-rendering fallback when a bank cannot be allocated.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from bake_file_index import encode as encode_index

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--legacy', type=Path, default=ROOT/'build/extra-actor-packed/verification.json')
    p.add_argument('--tiles', type=Path, default=ROOT/'build/extra-actor-compact-tiles')
    p.add_argument('--resources', type=Path, default=ROOT/'private/resources-h1-wide')
    p.add_argument('--output', type=Path, default=ROOT/'build/extra-actor-compact-resources')
    p.add_argument('--cxx', default='C:/msys64/ucrt64/bin/g++.exe')
    p.add_argument('--minimum-saving', type=float, default=.10)
    p.add_argument('--type', type=int, action='append', dest='selected_types')
    p.add_argument('--experimental-all', action='store_true', help='Reproduce the oversized experiment; does not change runtime policy')
    a = p.parse_args()
    if not a.selected_types and not a.experimental_all:
        p.error('Select measured --type entries, or explicitly request --experimental-all')
    assert 0 <= a.minimum_saving < 1
    legacy = json.loads(a.legacy.read_text())
    assert legacy['passed'] and legacy['types'] == len(legacy['files'])
    if a.selected_types:
        assert set(a.selected_types)<={r['type'] for r in legacy['files']}
        legacy['files']=[r for r in legacy['files'] if r['type'] in a.selected_types]
        legacy['types']=len(legacy['files'])
        legacy['bytes']=sum(r['bytes'] for r in legacy['files'])
    tiles, resources, output = a.tiles.resolve(), a.resources.resolve(), a.output.resolve()
    assert output != resources and not output.is_relative_to(resources)
    requested={r['runtime_path'] for r in legacy['files']}
    existing={p.relative_to(output).as_posix() for p in (output/'extra-actors').glob('*.h1ap')}
    if existing-requested:p.error('Output contains unselected experimental banks; choose a fresh --output directory')
    tiles.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    encoder = ROOT/'tools/pack_extra_actor_tiles.cpp'
    encoder_sha = sha(encoder)
    executable = ROOT/'build/pack-extra-actor-tiles.exe'
    stamp = executable.with_suffix('.source-sha256')
    env = os.environ.copy()
    env['PATH'] = str(Path(a.cxx).parent)+os.pathsep+env['PATH']
    if not executable.exists() or not stamp.exists() or stamp.read_text() != encoder_sha:
        subprocess.run([a.cxx, '-std=c++20', '-O2', encoder, '-lz', '-o', executable], env=env, check=True)
        stamp.write_text(encoder_sha)
    selected = {}
    banks = {}
    records = []
    for old in sorted(legacy['files'], key=lambda r:r['type']):
        tid = old['type']
        target, proof = tiles/f'{tid:03d}.h1ap', tiles/f'{tid:03d}.json'
        raw = ROOT/old['raw_input']
        new = json.loads(proof.read_text()) if proof.exists() else {}
        reusable = (target.exists() and new.get('encoder_source_sha256') == encoder_sha and
                    new.get('source_proof') == old['source_proof'] and new.get('frames') == old['frames'] and
                    new.get('bytes') == target.stat().st_size and new.get('raw_input') == old['raw_input'])
        if not reusable:
            r = subprocess.run([executable, raw, target], env=env, capture_output=True, text=True)
            (tiles/f'{tid:03d}.log').write_text(r.stdout+r.stderr)
            if r.returncode:
                raise RuntimeError(f'Tile encoding failed for {tid}: {r.stderr[-1500:]}')
            new = json.loads(r.stdout)
            new.update(encoder_source_sha256=encoder_sha, source_proof=old['source_proof'],
                       raw_input=old['raw_input'], previous_bytes=old['bytes'])
        assert new['pixel_roundtrip'] and new['per_pixel_operation_order_preserved'] and new['source_color_precision_unchanged']
        new['sha256'] = sha(target)
        proof.write_text(json.dumps(new, indent=2)+'\n')
        use_tiles = new['bytes'] <= old['bytes']*(1-a.minimum_saving)
        entry = dict(old)
        if use_tiles:
            entry.update(new)
            entry['encoder_sha256'] = encoder_sha
            entry['page_roundtrip'] = True  # v3 outer pages are direct bytes; inner tiles round-trip.
            selected[old['runtime_path']] = target
        entry.update(codec='exact-tiles-v3' if use_tiles else 'exact-runs-v1', previous_bytes=old['bytes'])
        records.append(entry)
        banks[old['runtime_path']]=target if use_tiles else a.legacy.resolve().parent/f'{tid:03d}.h1ap'
    # Hardlink immutable old files. Never truncate a linked source on replacement.
    for source in resources.rglob('*'):
        if not source.is_file():
            continue
        rel = source.relative_to(resources).as_posix()
        if rel.startswith('extra-actors/') and rel not in banks:
            continue
        if rel == 'resource-files.h1fi':
            continue
        chosen = selected.get(rel, source)
        dest = output/rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            if os.path.samefile(chosen, dest):
                continue
            dest.unlink()
        os.link(chosen, dest)
    for rel, source in banks.items():
        dest=output/rel
        dest.parent.mkdir(parents=True,exist_ok=True)
        if dest.exists():
            if os.path.samefile(source,dest):continue
            dest.unlink()
        os.link(source,dest)
    index, _ = encode_index(output)
    index_path = output/'resource-files.h1fi'
    if index_path.exists():
        index_path.unlink()
    index_path.write_bytes(index)
    result = dict(passed=True, types=len(records), frames=sum(r['frames'] for r in records),
                  previous_bytes=legacy['bytes'], bytes=sum(r['bytes'] for r in records),
                  tile_banks=len(selected), legacy_banks=len(records)-len(selected),
                  whole_resource_bytes=sum(f.stat().st_size for f in output.rglob('*') if f.is_file()),
                  minimum_saving=a.minimum_saving, encoder_sha256=encoder_sha, files=records,
                  all_pixels_and_pages_roundtrip=True, source_color_precision_unchanged=True,
                  resource_index_sha256=sha(index_path))
    (tiles/'verification.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='files'}, indent=2))


if __name__ == '__main__':
    main()
