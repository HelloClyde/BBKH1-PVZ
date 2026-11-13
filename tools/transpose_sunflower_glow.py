"""Arrange the existing glow palette by intensity for small streamed reads.

No color, animation frame, track boundary, or pixel sample changes. Reversing
the palette layout reconstructs the original bank byte for byte.
"""
import argparse, hashlib, json, struct, zlib
from pathlib import Path
from pack_actor_pages import encode, decode

ROOT = Path(__file__).resolve().parents[1]


def transpose(source):
    raw = bytearray(source)
    h = list(struct.unpack_from('<16I', raw))
    assert h[:4] == [0x50413148, 1, 480, 272] and h[15] == 2
    colors, shades, start = h[12], h[13], h[11]
    assert shades == 89 and colors and h[8] == start+colors*shades*4
    old = struct.unpack_from('<'+str(colors*shades)+'I', raw, start)
    column = [old[i*shades+s] for s in range(shades) for i in range(colors)]
    struct.pack_into('<'+str(len(column))+'I', raw, start, *column)
    h[15] |= 256
    h[10] = zlib.crc32(raw[64:])
    struct.pack_into('<16I', raw, 0, *h)
    result = encode(raw)
    check = bytearray(decode(result))
    values = struct.unpack_from('<'+str(len(column))+'I', check, start)
    restored = [values[s*colors+i] for i in range(colors) for s in range(shades)]
    assert restored == list(old)
    struct.pack_into('<'+str(len(restored))+'I', check, start, *restored)
    original_header = struct.unpack_from('<16I', source)
    struct.pack_into('<16I', check, 0, *original_header)
    assert check == source
    return result, dict(passed=True, original_bytes=len(source), bytes=len(result),
                        source_sha256=hashlib.sha256(source).hexdigest(),
                        sha256=hashlib.sha256(result).hexdigest(),
                        decoded_sha256=hashlib.sha256(raw).hexdigest(),
                        decoded_bytes=len(raw), colors=colors, shades=shades,
                        palette_values=len(restored), reverse_layout_byte_exact=True,
                        pixel_streams_unchanged=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    result, proof = transpose(a.source.read_bytes())
    a.output.write_bytes(result)
    proof['encoder_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    a.output.with_suffix('.verification.json').write_text(json.dumps(proof, indent=2)+'\n', encoding='utf8')
    print(json.dumps(proof))


if __name__ == '__main__':
    main()
