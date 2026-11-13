"""Merge empty track ranges offline, retaining attachment order and all pixels."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zlib
from pack_actor_pages import decode, encode


def compact(source):
    raw = decode(source)
    h = list(struct.unpack_from('<16I', raw))
    assert h[15] == 194 and h[9] == len(raw)
    body, streams, packets, entries = bytearray(), {}, {}, []
    before = after = empty = ranges = 0
    for i in range(h[4]):
        key, frame, offset, size, kind = struct.unpack_from('<Q4I', raw, 64+i*24)
        header = list(struct.unpack_from('<hhHHhhhh', raw, offset))
        at, segments, original = offset+16, [], []
        for _ in range(header[2]):
            track, draw, runs, payload, x, y = struct.unpack_from('<hHIIhh', raw, at)
            at += 16
            assert 0 <= track < 256 and payload&0x80000000
            start = h[8]+(payload&0x7fffffff)
            length, = struct.unpack_from('<I', raw, start)
            pixels = raw[start+4:start+4+length]
            assert len(pixels) == length
            before += 1
            if not runs:
                empty += 1
                original.append((track, draw&1, 0, b'', 0, 0))
                if segments and segments[-1][0] == -2 and segments[-1][1] == draw&1:
                    first, count = segments[-1][3]&65535, segments[-1][3]>>16
                    if track == first+count:
                        segments[-1][3] += 65536
                        continue
                segments.append([-2, draw&1, 0, track|65536, 0, 0])
            else:
                original.append((track, draw, runs, pixels, x, y))
                identity = runs, draw&32768, pixels
                if identity not in streams:
                    streams[identity] = len(body)
                    body += struct.pack('<I', length)+pixels
                segments.append([track, draw, runs, 0x80000000|streams[identity], x, y])
        assert at == offset+size
        restored = []
        for track, draw, runs, payload, x, y in segments:
            if track == -2:
                first, count = payload&65535, payload>>16
                restored.extend((n, draw, 0, b'', 0, 0) for n in range(first, first+count))
                ranges += 1
            else:
                start = payload&0x7fffffff
                length, = struct.unpack_from('<I', body, start)
                restored.append((track, draw, runs, bytes(body[start+4:start+4+length]), x, y))
        assert restored == original
        header[2], header[3] = len(segments), header[3]|1024
        packet = struct.pack('<hhHHhhhh', *header)+b''.join(struct.pack('<hHIIhh', *s) for s in segments)
        if packet not in packets:
            packets[packet] = len(body)
            body += packet
        entries.append((key, frame, h[8]+packets[packet], len(packet), kind))
        after += len(segments)
    prefix = bytearray(raw[64:h[8]])
    for i, entry in enumerate(entries):
        struct.pack_into('<Q4I', prefix, i*24, *entry)
    payload = prefix+body
    h[9], h[10], h[15] = 64+len(payload), zlib.crc32(payload), h[15]|1024
    decoded = struct.pack('<16I', *h)+payload
    result = encode(decoded)
    assert decode(result) == decoded
    sha = lambda b: hashlib.sha256(b).hexdigest()
    return result, dict(passed=True, bytes=len(result), sha256=sha(result), decoded_bytes=len(decoded),
        decoded_sha256=sha(decoded), source_sha256=sha(source), entries=len(entries),
        segments_before=before, segments_after=after, empty_tracks=empty, empty_ranges=ranges,
        expanded_track_order_exact=True, all_pixel_streams_byte_exact=True,
        encoder_sha256=sha(Path(__file__).read_bytes()), lossy=False)


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
