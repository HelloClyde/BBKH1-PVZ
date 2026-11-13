"""Select the measured walk-to-eat hotspot from the exhaustive blend export."""
from pathlib import Path
import argparse, hashlib, json
from pack_dense_actor_poses import pack
from pack_actor_pages import encode, decode
import struct

ROOT = Path(__file__).resolve().parents[1]


def color_references(body, hats, blend, output):
    tables = []
    for path in (body, hats, blend):
        raw = path.read_bytes()
        raw = decode(raw) if raw[:4] == b'H1PZ' else raw
        h = struct.unpack_from('<16I', raw)
        assert h[13] == 26 and not h[15] & (256 | 512)
        tables.append([struct.unpack_from('<26I', raw, h[11]+i*104) for i in range(h[12])])
    references = {}
    for kind, table in enumerate(tables[:2]):
        assert len(table) < 32768
        for i, colors in enumerate(table):
            references.setdefault(colors, i | (kind << 15))
    ids = [references[colors] for colors in tables[2]]
    assert [tables[i >> 15][i & 0x7fff] for i in ids] == tables[2]
    raw = struct.pack('<'+'H'*len(ids), *ids)
    output.write_bytes(raw)
    return dict(passed=True, tuples=len(ids), bytes=len(raw),
        sha256=hashlib.sha256(raw).hexdigest(), all_26_colors_byte_exact=True,
        net_heap_saving=len(ids)*102,
        provider_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (body, hats)})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--export', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--providers', type=Path, default=ROOT/'private/resources-h1-wide')
    a = p.parse_args()
    proof = json.loads((a.export/'zombie-blend-export-verification.json').read_text())
    assert proof['passed'] and not proof['prototype']
    a.output.mkdir(parents=True, exist_ok=True)
    target = a.output/'zombie-blends.h1ap'
    packing = pack(a.export/'zombie-blends.raw', target,
        a.output/'zombie-blends-packing.json', packet_mode=16,
        segment_offsets=True, keep_frame=lambda frame: frame & 0x0c000000 == 0 and ((frame & 31) % 2 == 0 or (frame & 31) == 19))
    assert packing['entries'] == 93*11
    raw = target.read_bytes()
    blob = encode(raw)
    assert decode(blob) == raw
    target.write_bytes(blob)
    proof['shared_colors'] = color_references(a.providers/'zombie-hit.h1ap',
        a.providers/'zombie-hats.h1ap', target, a.output/'zombie-blend-colors.h1cr')
    proof.update(runtime_path=target.name, bytes=len(blob),
        sha256=hashlib.sha256(blob).hexdigest(), decoded_bytes=len(raw),
        selected_poses=packing['entries'], selected_direction='walk_to_eat',
        excluded_unmeasured_direction='eat_to_walk',
        source_pose_step=0.5, display_sample_hz=50, original_blend_updates=20,
        runtime_rng_and_clocks_unchanged=True)
    (a.output/'zombie-blends.json').write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps(proof))


if __name__ == '__main__':
    main()
