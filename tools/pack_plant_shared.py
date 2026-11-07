"""Separate shared plant pixels from independently loadable offline palettes."""
from pathlib import Path
import argparse, hashlib, json, struct, zlib
from pack_actor_pages import encode, decode

ROOT = Path(__file__).resolve().parents[1]

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('state_keys', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    assert not a.output.exists()
    source = a.input.read_bytes()
    old = struct.unpack_from('<8I', source)
    assert old[:2] == (0x4c503148, 1) and old[4] == len(source)
    proof = json.loads(a.state_keys.read_text(encoding='utf8'))
    assert proof['passed']
    keys = {r['type']: {int(k, 16): f for k, f in r['state_keys']} for r in proof['types']}
    count = old[2]
    table = bytearray(count * 12)
    body = bytearray()
    components, records = {}, []
    base = 32 + len(table)
    def append(name, kind, data, raw=None):
        row = dict(kind=kind, offset=base + len(body), bytes=len(data), sha256=sha(data), crc32=zlib.crc32(data))
        if raw is not None:
            row.update(decoded_bytes=len(raw), decoded_sha256=sha(raw), decoded_crc32=zlib.crc32(raw))
        components[name] = row
        body.extend(data)
        return row
    shared = 0
    for type_id in sorted(keys):
        slots = [struct.unpack_from('<3I', source, 32 + (type_id * 4 + f) * 12) for f in range(4)]
        raw = decode(source[slots[0][0]:slots[0][0] + slots[0][1]])
        h = struct.unpack_from('<16I', raw)
        if h[15] != 10:
            for f, (offset, size, crc) in enumerate(slots):
                blob = source[offset:offset + size]
                assert zlib.crc32(blob) == crc
                decoded = decode(blob)
                hh = struct.unpack_from('<16I', decoded)
                name = f'{type_id}/legacy-{f}'
                row = append(name, 'H1PZ', blob, decoded)
                struct.pack_into('<3I', table, (type_id * 4 + f) * 12, row['offset'], row['bytes'], row['crc32'])
                records.append(dict(type=type_id, filter=f, slot_index=type_id * 4 + f, schema='H1AP/1', component=name, frames=hh[4]))
            continue
        assert len(set(slots)) == 1 and h[13] == 4
        shared += 1
        packets = bytearray(64) + raw[h[8]:]
        entries = [[] for _ in range(4)]
        seen = set()
        for i in range(h[4]):
            key, frame, offset, size, kind = struct.unpack_from('<Q4I', raw, 64 + i * 24)
            assert kind == type_id
            new_offset = 64 + offset - h[8]
            if new_offset not in seen:
                assert struct.unpack_from('<H', packets, new_offset + 6)[0] == 10
                struct.pack_into('<H', packets, new_offset + 6, 2)
                seen.add(new_offset)
            entries[keys[type_id][key]].append((key, frame, new_offset, size, kind))
        geometry = bytes(packets)
        encoded = encode(geometry)
        assert decode(encoded) == geometry
        geom_name = f'{type_id}/geometry'
        geom = append(geom_name, 'H1PZ-geometry', encoded, geometry)
        for f in range(4):
            entries[f].sort()
            index = b''.join(struct.pack('<Q4I', *e) for e in entries[f])
            palette = b''.join(raw[h[11] + (i * 4 + f) * 4:h[11] + (i * 4 + f + 1) * 4] for i in range(h[12]))
            palette_name, index_name = f'{type_id}/palette-{f}', f'{type_id}/index-{f}'
            pal = append(palette_name, 'zlib-palette', zlib.compress(palette, 9), palette)
            idx = append(index_name, 'zlib-index', zlib.compress(index, 9), index)
            descriptor = struct.pack('<16I', 0x53503148, 1, type_id, f, len(entries[f]), h[12],
                geom['offset'], geom['bytes'], len(geometry), zlib.crc32(geometry),
                idx['offset'], idx['bytes'], zlib.crc32(index), pal['offset'], pal['bytes'], zlib.crc32(palette))
            desc_name = f'{type_id}/descriptor-{f}'
            row = append(desc_name, 'H1PS', descriptor)
            struct.pack_into('<3I', table, (type_id * 4 + f) * 12, row['offset'], row['bytes'], row['crc32'])
            records.append(dict(type=type_id, filter=f, slot_index=type_id * 4 + f, schema='H1PS/1',
                component=desc_name, geometry=geom_name, palette=palette_name, index=index_name,
                frames=len(entries[f]), palette_colors=h[12]))
        print('Split palette', type_id, 'geometry', len(encoded), flush=True)
    output = struct.pack('<8I', 0x4c503148, 2, count, len(table), base + len(body), zlib.crc32(table), 0, 0) + table + body
    a.output.write_bytes(output)
    report = dict(passed=True, runtime_path='plant-poses.h1pp', schema='H1PL/2', bytes=len(output), sha256=sha(output),
        shared_geometry_types=shared, plant_types=len(keys), logical_banks=len(records),
        selected_palette_only=True, paged_runtime_palette_bytes=4096, lossy=False,
        source_pack_sha256=sha(source), source_state_keys_sha256=sha(a.state_keys.read_bytes()),
        encoder_sha256=sha(Path(__file__).read_bytes()), page_encoder_sha256=sha((ROOT/'tools/pack_actor_pages.py').read_bytes()),
        compiler_sha256=sha((ROOT/'tools/tests/plant_pose_export.cpp').read_bytes()), components=components, files=records)
    a.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(dict(bytes=len(output), shared_geometry_types=shared, logical_banks=len(records))))

if __name__ == '__main__':
    main()
