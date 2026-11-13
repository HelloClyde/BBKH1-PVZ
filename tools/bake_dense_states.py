"""Pack the measured uncovered LCD states, retaining original shader colors."""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys
from pack_dense_actor_poses import pack
from pack_actor_pages import encode, decode
from transpose_sunflower_glow import transpose
from index_sunflower_glow import compact

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--export', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    proof = json.loads((a.export/'dense-state-export-verification.json').read_text())
    assert proof['passed']
    intermediate = a.output/'plant-blinks.unindexed'
    subprocess.run([sys.executable, str(ROOT/'tools/pack_sunflower_glow.py'),
        str(a.export/'plant-blinks.raw'), str(intermediate), '--report',
        str(a.output/'plant-blinks-packing.json')], check=True)
    transposed, _ = transpose(intermediate.read_bytes())
    blob, report = compact(transposed)
    (a.output/'plant-blinks.h1ap').write_bytes(blob)
    (a.output/'plant-blinks-indexed.json').write_text(json.dumps(report, indent=2)+'\n')
    sun = a.output/'sun-poses.h1ap'
    pack(a.export/'scaled-suns.raw', sun, a.output/'scaled-suns-packing.json', share_normal=True, segment_offsets=True)
    raw = sun.read_bytes()
    blob = encode(raw)
    assert decode(blob) == raw
    sun.write_bytes(blob)
    proof['files'] = {name: dict(bytes=(a.output/name).stat().st_size,
        sha256=hashlib.sha256((a.output/name).read_bytes()).hexdigest())
        for name in ('plant-blinks.h1ap', 'sun-poses.h1ap')}
    proof['runtime_scale_sampling_only'] = True
    proof['exporter_sha256'] = hashlib.sha256((ROOT/'tools/tests/dense_state_export.cpp').read_bytes()).hexdigest()
    (a.output/'dense-states.json').write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps(proof))


if __name__ == '__main__':
    main()
