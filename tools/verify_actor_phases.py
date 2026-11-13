"""Check every lifetime bank against its preserved original pixel packets."""
from pathlib import Path
import hashlib, json, struct, zlib
from pack_actor_pages import decode
from resource_inputs import input_path
ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def packets(data):
    h = struct.unpack_from('<16I', data)
    assert h[:4] == (0x50413148, 1, 480, 272) and h[9] == len(data)
    assert h[10] == zlib.crc32(data[64:])
    result = {}
    for i in range(h[4]):
        key, frame, start, size, kind = struct.unpack_from('<Q4I', data, 64 + i * 24)
        assert (key, frame) not in result and h[8] <= start <= len(data) - size
        segments, mode = struct.unpack_from('<HH', data, start + 4)
        assert mode in (0, 1) and 0 < segments <= 257
        packet, at = bytearray(data[start:start + 16]), start + 16
        for _ in range(segments):
            track, draw, runs, length = struct.unpack_from('<hHII', data, at); at += 12
            if length & 0x80000000:
                source = h[8] + (length & 0x7fffffff)
                length, = struct.unpack_from('<I', data, source)
                payload = data[source + 4:source + 4 + length]
            else:
                payload = data[at:at + length]; at += length
            assert len(payload) == length
            packet.extend(struct.pack('<hHII', track, draw, runs, length)); packet.extend(payload)
        assert at == start + size
        result[key, frame] = kind, bytes(packet)
    return result


def verify(resources):
    proof = json.loads((ROOT / 'provenance/offline-actor-phases.json').read_text())
    assert proof['passed'] and not proof['lossy']
    assert digest((ROOT / 'tools/split_actor_phases.py').read_bytes()) == proof['encoder_sha256']
    source = input_path(resources, 'actor-poses.h1ap').read_bytes()
    assert digest(source) == proof['source_sha256']
    original = packets(decode(source))
    normal = json.loads((ROOT / 'provenance/offline-actor-normal.json').read_text())
    assert normal['sha256'] == proof['normal_sha256']
    data = (ROOT / 'private/resource-build-inputs/optimized/actor-normal-mode20.h1ap').read_bytes()
    assert digest(data) == proof['normal_sha256']
    count, = struct.unpack_from('<I', data, 16)
    covered = {struct.unpack_from('<Q4I', data, 64 + i * 24)[:2] for i in range(count)}
    assert len(covered) == proof['removed_ordinary_duplicates'] == 211
    for record in proof['files']:
        # Retain the exact old packet audit even when a layer-preserving
        # export supersedes its whole-head renderer. Check the new bank too.
        encoded = ((ROOT / record['original_archive_path']) if record.get('original_archive_path') else
                   (resources / record['runtime_path'])).read_bytes()
        assert len(encoded) == record['bytes'] and digest(encoded) == record['sha256']
        raw = decode(encoded)
        assert len(raw) == record['decoded_bytes'] and digest(raw) == record['decoded_sha256']
        actual = packets(raw)
        assert len(actual) == record['entries'] and not actual.keys() & covered
        joined = hashlib.sha256()
        for key, value in actual.items():
            assert value == original[key] and value[0] == record['type']
            joined.update(value[1])
        assert joined.hexdigest() == record['ordered_packet_sha256']
        covered.update(actual)
    assert covered == original.keys() and len(covered) == proof['complete_source_entry_coverage']
    if any(r.get('replacement_provenance') for r in proof['files']):
        from verify_combat_sprites import verify as verify_combat
        verify_combat(resources)
    return dict(passed=True, original_entries=len(covered), phase_banks=len(proof['files']), duplicate_frames_removed=211, all_pixel_packets_identical=True)


if __name__ == '__main__':
    print(json.dumps(verify(ROOT / 'private/resources-h1-wide')))
