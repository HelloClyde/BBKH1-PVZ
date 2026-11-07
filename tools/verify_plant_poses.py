"""Validate every installed plant bank and the shared sunflower glow palette."""
from pathlib import Path
import hashlib
import json
import struct
import zlib

from pack_actor_pages import decode
from resource_inputs import input_path

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def actor(data, expected_type, expected_frames, modes):
    h = struct.unpack_from('<16I', data)
    assert h[:4] == (0x50413148, 1, 480, 272)
    assert h[4] == expected_frames and h[5] == 64
    assert h[7] == 64 + h[4] * 24 and h[9] == len(data)
    assert h[10] == zlib.crc32(data[64:]) and h[14] == 0x314d4352
    assert h[15] in (2, 6, 10)
    if h[12]:
        assert h[13] == (4 if h[15] == 10 else 89) and h[11] == h[7] + h[6] * 4
        assert h[8] == h[11] + h[12] * h[13] * 4
    else:
        assert h[11:14] == (0, 0, 0) and h[8] == h[7] + h[6] * 4
    previous = None
    for i in range(h[4]):
        key, frame, offset, size, kind = struct.unpack_from('<Q4I', data, 64 + i * 24)
        assert previous is None or previous < (key, frame)
        previous = key, frame
        assert kind == expected_type and h[8] <= offset and size >= 16
        assert offset + size <= len(data)
        _, _, segments, mode, left, top, right, bottom = struct.unpack_from('<hhHHhhhh', data, offset)
        assert 0 < segments <= 257 and mode in modes
        assert bool(mode & 4) == bool(h[15] & 4)
        assert left <= right and top <= bottom
    return h

def shared_pack(blob, proof):
    magic, version, count, table, total, checksum, a, b = struct.unpack_from('<8I', blob)
    assert (magic, version, table, total, a, b) == (0x4c503148, 2, count * 12, len(blob), 0, 0)
    assert zlib.crc32(blob[32:32 + table]) == checksum
    decoded, end = {}, 32 + table
    for name, c in sorted(proof['components'].items(), key=lambda item: item[1]['offset']):
        assert c['offset'] == end
        data = blob[end:end + c['bytes']]
        assert len(data) == c['bytes'] and sha(data) == c['sha256'] and zlib.crc32(data) == c['crc32']
        end += len(data)
        if c['kind'] == 'H1PS':
            decoded[name] = data
            continue
        raw = decode(data) if c['kind'].startswith('H1PZ') else zlib.decompress(data)
        assert len(raw) == c['decoded_bytes'] and sha(raw) == c['decoded_sha256'] and zlib.crc32(raw) == c['decoded_crc32']
        decoded[name] = raw
    assert end == len(blob)
    records = {r['slot_index']: r for r in proof['files']}
    assert len(records) == 200 and len({r['type'] for r in records.values()}) == 50
    assert {(r['type'], r['filter']) for r in records.values()} == {(t, f) for t in {r['type'] for r in records.values()} for f in range(4)}
    for slot in range(count):
        offset, size, crc = struct.unpack_from('<3I', blob, 32 + slot * 12)
        if slot not in records:
            assert (offset, size, crc) == (0, 0, 0)
            continue
        r = records[slot]
        c = proof['components'][r['component']]
        assert slot == r['type'] * 4 + r['filter'] and (offset, size, crc) == (c['offset'], c['bytes'], c['crc32'])
        if r['schema'] == 'H1AP/1':
            actor(decoded[r['component']], r['type'], r['frames'], (2, 6))
            continue
        h = struct.unpack('<16I', decoded[r['component']])
        g, i, p = (proof['components'][r[k]] for k in ('geometry', 'index', 'palette'))
        assert h == (0x53503148, 1, r['type'], r['filter'], r['frames'], r['palette_colors'],
            g['offset'], g['bytes'], g['decoded_bytes'], g['decoded_crc32'],
            i['offset'], i['bytes'], i['decoded_crc32'], p['offset'], p['bytes'], p['decoded_crc32'])
        assert len(decoded[r['index']]) == r['frames'] * 24 and len(decoded[r['palette']]) == r['palette_colors'] * 4
        geometry = decoded[r['geometry']]
        previous = None
        for key, frame, start, length, kind in struct.iter_unpack('<Q4I', decoded[r['index']]):
            assert kind == r['type'] and 64 <= start <= len(geometry) - length and length >= 16
            assert previous is None or previous < (key, frame)
            previous = key, frame
            assert struct.unpack_from('<H', geometry, start + 6)[0] == 2
    return records


def verify(resources):
    plant = json.loads((ROOT / 'provenance/offline-plant-poses.json').read_text())
    blob = (resources / plant['runtime_path']).read_bytes()
    assert len(blob) == plant['bytes'] and sha(blob) == plant['sha256']
    encoder = 'tools/pack_plant_shared.py' if plant.get('schema') == 'H1PL/2' else 'tools/pack_plant_variants.py' if plant.get('shared_color_variant_types') else 'tools/pack_plant_poses.py'
    assert sha((ROOT / encoder).read_bytes()) == plant['encoder_sha256']
    if plant.get('shared_color_variant_types'):
        assert sha((ROOT / 'tools/pack_plant_poses.py').read_bytes()) == plant['legacy_encoder_sha256']
    assert sha((ROOT / 'tools/tests/plant_pose_export.cpp').read_bytes()) == plant['compiler_sha256']
    assert sha((ROOT / 'tools/pack_actor_pages.py').read_bytes()) == plant['page_encoder_sha256']
    if plant.get('schema') == 'H1PL/2':
        compiler = ROOT / 'provenance/offline-plant-compiler.json'
        assert sha(compiler.read_bytes()) == plant['source_state_keys_sha256']
        compilation = json.loads(compiler.read_text())
        assert compilation['passed'] and len(compilation['types']) == 50
        records = shared_pack(blob, plant)
    else:
        magic, version, count, table_bytes, total, crc, reserved1, reserved2 = struct.unpack_from('<8I', blob)
        assert (magic, version, table_bytes, total, reserved1, reserved2) == (0x4c503148, 1, count * 12, len(blob), 0, 0)
        assert zlib.crc32(blob[32:32 + table_bytes]) == crc
        records = {r['slot_index']: r for r in plant['files']}
        assert len(records) == 200 and len({r['type'] for r in records.values()}) == 50
        assert {(r['type'], r['filter']) for r in records.values()} == {(t, f) for t in {r['type'] for r in records.values()} for f in range(4)}
        end = 32 + table_bytes
        seen = {}
        for slot in range(count):
            offset, size, checksum = struct.unpack_from('<3I', blob, 32 + slot * 12)
            if slot not in records:
                assert (offset, size, checksum) == (0, 0, 0)
                continue
            r = records[slot]
            assert slot == r['type'] * 4 + r['filter']
            assert offset == r['offset'] and size == r['bytes']
            identity = (size, r['sha256'], r['decoded_sha256'], r['frames'], r['type'])
            if offset in seen:
                assert r.get('shared_variants') and seen[offset] == identity
                continue
            assert offset == end
            part = blob[offset:offset + size]
            assert len(part) == size and zlib.crc32(part) == checksum and sha(part) == r['sha256']
            raw = decode(part)
            assert len(raw) == r['decoded_bytes'] and sha(raw) == r['decoded_sha256']
            actor(raw, r['type'], r['frames'], (2, 6, 10))
            seen[offset] = identity
            end += size
        assert end == len(blob)
    glow = json.loads((ROOT / 'provenance/offline-sunflower-glow.json').read_text())
    encoded = (resources / glow['runtime_path']).read_bytes()
    assert len(encoded) == glow['bytes'] and sha(encoded) == glow['sha256']
    assert sha((ROOT / 'tools/pack_sunflower_glow.py').read_bytes()) == glow['encoder_sha256']
    assert sha((ROOT / 'tools/tests/sunflower_glow_export.cpp').read_bytes()) == glow['exporter_sha256']
    raw = encoded if encoded[:4] == b'H1AP' else decode(encoded)
    assert len(raw) == glow['decoded_bytes'] and sha(raw) == glow['decoded_sha256']
    h = actor(raw, 7, glow['frames'], (3,))
    assert h[12] == glow['palette_tuples'] and h[13] == glow['intensities']
    shadow = json.loads((ROOT / 'provenance/offline-plant-shadows.json').read_text())
    data = (resources / shadow['runtime_path']).read_bytes()
    assert len(data) == shadow['bytes'] and sha(data) == shadow['sha256']
    assert sha((ROOT / 'tools/pack_plant_shadows.py').read_bytes()) == shadow['encoder_sha256']
    assert sha((ROOT / 'tools/pack_plant_poses.py').read_bytes()) == shadow['span_encoder_sha256']
    assert sha((ROOT / 'tools/tests/plant_shadow_export.cpp').read_bytes()) == shadow['exporter_sha256']
    actor(data, 0, shadow['poses'], (2,))
    normal = json.loads((ROOT / 'provenance/offline-actor-normal.json').read_text())
    data = (resources / normal['runtime_path']).read_bytes()
    linear = json.loads((ROOT / 'provenance/offline-actor-linear.json').read_text())
    assert len(data) == linear['bytes'] and sha(data) == linear['sha256']
    assert linear['source_sha256'] == normal['sha256'] and not linear['lossy']
    assert sha((ROOT / 'tools/linearize_actor_normal.py').read_bytes()) == linear['encoder_sha256']
    source_normal = ROOT / 'private/resource-build-inputs/optimized/actor-normal-mode20.h1ap'
    assert sha(source_normal.read_bytes()) == normal['sha256']
    assert sha((ROOT / 'tools/optimize_actor_normal.py').read_bytes()) == normal['encoder_sha256']
    assert sha(input_path(resources, 'actor-poses.h1ap').read_bytes()) == normal['source_sha256']
    nh = struct.unpack_from('<16I', data)
    assert nh[:6] == (0x50413148, 1, 480, 272, normal['entries'], 64)
    assert nh[6] == 0 and nh[7] == nh[8] == 64 + nh[4] * 24
    assert nh[9] == len(data) and nh[10] == zlib.crc32(data[64:])
    assert nh[11:] == (0, 0, 0, 0x314d4352, 52)
    previous = None
    for i in range(nh[4]):
        key, frame, offset, size, kind = struct.unpack_from('<Q4I', data, 64 + i * 24)
        assert previous is None or previous < (key, frame)
        previous = key, frame
        assert kind in (4, 7) and not frame & 0x80000000
        assert nh[8] <= offset <= len(data) - size and not offset & 3
        _, _, segments, mode, left, top, right, bottom = struct.unpack_from('<hhHHhhhh', data, offset)
        assert mode == 52 and 0 < segments <= 257 and left <= right and top <= bottom
        at = offset + 16
        for _ in range(segments):
            track, flags, runs, length = struct.unpack_from('<hHII', data, at)
            assert -1 <= track <= 255 and flags in (0, 1)
            at += 12
            end = at + length
            assert end <= offset + size
            for _ in range(runs):
                position, count, reserved = struct.unpack_from('<iHH', data, at)
                y = (position - left) // 480
                x = position - y * 480
                assert not reserved and 0 < count & 32767 <= 480
                assert left <= x < x + (count & 32767) <= right and top <= y < bottom
                at += 8
                for _ in range(count & 32767):
                    value, = struct.unpack_from('<I', data, at)
                    assert bool(count & 32768) == (value >> 24 == 255)
                    at += 4
            assert at == end
        assert at == offset + size
    lawns = json.loads((ROOT / 'provenance/offline-regular-lawns.json').read_text())
    assert sha((ROOT / 'tools/pack_regular_lawns.py').read_bytes()) == lawns['encoder_sha256']
    assert sha((ROOT / 'tools/tests/regular_lawn_export.cpp').read_bytes()) == lawns['exporter_sha256']
    linear_lawns = json.loads((ROOT / 'provenance/offline-linear-lawns.json').read_text())
    assert linear_lawns['passed'] and linear_lawns['lossless']
    assert sha((ROOT / 'tools/linearize_regular_lawns.py').read_bytes()) == linear_lawns['encoder_sha256']
    linear_rows = {r['runtime_path']: r for r in linear_lawns['files']}
    for row in lawns['files']:
        data = (resources / row['runtime_path']).read_bytes()
        linear_row = linear_rows[row['runtime_path']]
        assert len(data) == linear_row['bytes'] and sha(data) == linear_row['sha256']
        assert linear_row['source_sha256'] == row['sha256']
        lh = struct.unpack_from('<16I', data)
        assert lh[:11] == (0x424c3148, 2, 480, 272, 16, 8, 1, 480 * 272, 64, 64, len(data))
        assert lh[12:] == (0, 0, 0, 0) and zlib.crc32(data[64:]) == lh[11]
        assert sha(data[64:]) == row['source_frame_sha256'] == linear_row['pixels_sha256']
    return dict(passed=True, plant_banks=len(records), plant_types=50, sunflower_glow_intensities=h[13], ordinary_shadow_poses=shadow['poses'], aligned_ordinary_poses=normal['entries'], ordinary_backgrounds=len(lawns['files']))


if __name__ == '__main__':
    print(json.dumps(verify(ROOT / 'private/resources-h1-wide')))
