"""Losslessly intern baked glow colors; the H1 only reads 16-bit color IDs."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path
from pack_actor_pages import decode, encode


def compact(source):
    raw = decode(source)
    old = list(struct.unpack_from('<16I', raw))
    assert old[:4] == [0x50413148, 1, 480, 272] and old[15] == 258
    assert old[6] == 1 and old[8] == old[11]+old[12]*old[13]*4
    values = struct.unpack_from('<'+'I'*(old[12]*old[13]), raw, old[11])
    palette = list(dict.fromkeys(values))
    assert len(palette) <= 65536
    ids = {v: i for i, v in enumerate(palette)}
    indices = [ids[v] for v in values]
    h = old.copy()
    h[6] = len(palette)
    h[11] = h[7]+len(palette)*4
    h[8] = h[11]+len(indices)*2
    delta = h[8]-old[8]
    index = bytearray(raw[64:old[7]])
    for i in range(h[4]):
        offset, = struct.unpack_from('<I', index, i*24+12)
        assert old[8] <= offset < len(raw)
        struct.pack_into('<I', index, i*24+12, offset+delta)
    body = index+struct.pack('<'+'I'*len(palette), *palette)
    body += struct.pack('<'+'H'*len(indices), *indices)+raw[old[8]:]
    h[9] = 64+len(body)
    h[10] = zlib.crc32(body)
    h[15] |= 512
    result = encode(struct.pack('<16I', *h)+body)
    decoded = decode(result)
    colors = struct.unpack_from('<'+'I'*h[6], decoded, h[7])
    restored = [colors[i] for i in struct.unpack_from('<'+'H'*len(indices), decoded, h[11])]
    assert restored == list(values)
    assert decoded[h[8]:] == raw[old[8]:]
    for i in range(h[4]):
        before = struct.unpack_from('<Q4I', raw, 64+i*24)
        after = struct.unpack_from('<Q4I', decoded, 64+i*24)
        assert after == (before[0], before[1], before[2]+delta, before[3], before[4])
    sha = lambda b: hashlib.sha256(b).hexdigest()
    return result, dict(passed=True, source_sha256=sha(source), sha256=sha(result),
        bytes=len(result), decoded_sha256=sha(decoded), decoded_bytes=len(decoded),
        color_values=len(values), unique_colors=len(palette), color_bytes_before=len(values)*4,
        color_bytes_after=len(indices)*2+len(palette)*4, color_values_byte_exact=True,
        geometry_byte_exact=True, entry_relocations_checked=h[4], lossy=False,
        encoder_sha256=sha(Path(__file__).read_bytes()))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--report', type=Path, required=True)
    a = p.parse_args()
    result, proof = compact(a.source.read_bytes())
    a.output.write_bytes(result)
    a.report.write_text(json.dumps(proof, indent=2)+'\n', encoding='utf8')
    print(json.dumps(proof))


if __name__ == '__main__':
    main()
