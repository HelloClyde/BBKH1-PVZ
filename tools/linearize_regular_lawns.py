"""Lay out immutable lawn pixels in LCD row order before installation."""
from pathlib import Path
import argparse, hashlib, json, struct, zlib


def convert(source):
    h = struct.unpack_from('<16I', source)
    assert h[:7] == (0x424c3148, 1, 480, 272, 16, 8, 6)
    assert h[10] == len(source) and zlib.crc32(source[64:]) == h[11]
    pixels = bytearray(480 * 272 * 4)
    for cell in range(1020):
        tile, = struct.unpack_from('<H', source, 64 + cell * 2)
        assert tile < h[7]
        assert all(struct.unpack_from('<H', source, 64 + (frame * 1020 + cell) * 2)[0] == tile for frame in range(1, 6))
        y, x = divmod(cell, 30)
        for row in range(8):
            at = ((y * 8 + row) * 480 + x * 16) * 4
            start = h[9] + tile * 512 + row * 64
            pixels[at:at + 64] = source[start:start + 64]
    return struct.pack('<16I', 0x424c3148, 2, 480, 272, 16, 8, 1, 480 * 272, 64, 64, 64 + len(pixels), zlib.crc32(pixels), 0, 0, 0, 0) + pixels


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path); p.add_argument('output', type=Path)
    p.add_argument('--report', type=Path, required=True); a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True); records = []; sha = lambda b: hashlib.sha256(b).hexdigest()
    for number in range(1, 7):
        name = f'normal-lawn-{number}.h1lb'; source = (a.source / name).read_bytes(); data = convert(source)
        (a.output / name).write_bytes(data)
        records.append(dict(runtime_path=name, bytes=len(data), sha256=sha(data), source_sha256=sha(source), pixels_sha256=sha(data[64:])))
    result = dict(passed=True, lossless=True, runtime_tiles_reassembled=False, files=records, encoder_sha256=sha(Path(__file__).read_bytes()))
    a.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf8')
    print(json.dumps(dict(passed=True, backgrounds=6, total_bytes=sum(r['bytes'] for r in records))))


if __name__ == '__main__':
    main()
