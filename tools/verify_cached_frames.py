"""Verify display-density preview pixels and their two-pack metadata entries."""
from pathlib import Path
import hashlib
import json
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify(resources):
    proof = json.loads((ROOT / 'provenance/offline-cached-frames.json').read_text('utf8'))
    for path, key in [('tools/bake_cached_frames.py', 'encoder_sha256'),
                      ('tools/tests/cached_frames_export.cpp', 'exporter_sha256'),
                      ('upstream/src/Lawn/System/ReanimationLawn.cpp', 'original_factories_sha256')]:
        assert digest((ROOT / path).read_bytes()) == proof[key], path
    blob = (resources / proof['runtime_path']).read_bytes()
    assert len(blob) == proof['bytes'] and digest(blob) == proof['sha256']
    magic, version, count, table_bytes, size, checksum, a, b = struct.unpack_from('<4s7I', blob)
    assert (magic, version, count, size, a, b) == (b'H1PK', 1, 214, len(blob), 0, 0)
    table = blob[32:32 + table_bytes]
    value = 2166136261
    for byte in table:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    assert value == checksum
    records = {r['runtime_path']: r for r in proof['files']}
    assert len(records) == count and (proof['plant_variants'], proof['mowers'], proof['zombies']) == (175, 4, 35)
    infos, previous, end = {}, '', 32 + table_bytes
    for i in range(count):
        name_at, offset, length, crc = struct.unpack_from('<4I', table, i * 16)
        name_at += count * 16
        assert count * 16 <= name_at < len(table)
        name = table[name_at:table.index(0, name_at)].decode('ascii')
        assert previous < name and offset == end and name in records
        previous, end = name, offset + length
        pixels = blob[offset:end]
        r = records[name]
        assert len(pixels) == length and digest(pixels) == r['sha256'] and zlib.crc32(pixels) == crc
        fields = struct.unpack_from('<4s7I', pixels)
        m, w, h, channels, flags, pixel_bytes, sw, sh = fields
        assert (m, w, h, channels, flags) == (b'H1I2', r['width'], r['height'], 4, 0)
        assert (sw, sh) == (r['storage_width'], r['storage_height']) == ((w * 480 + 530) // 1060, (h * 272 + 300) // 600)
        assert pixel_bytes == sw * sh * 4 == length - 32 and not (resources / name).exists()
        infos[name] = dict(zip(('width', 'height', 'channels', 'flags', 'pixel_bytes', 'storage_width', 'storage_height'), fields[1:]))
    assert end == len(blob) and digest((resources / 'image-metadata.h1x').read_bytes()) == proof['metadata_sha256']
    return infos


if __name__ == '__main__':
    infos = verify(ROOT / 'private/resources-h1-wide')
    print(json.dumps(dict(passed=True, original_factory_keys=len(infos), all_display_density=True, online_preview_canvas=False)))
