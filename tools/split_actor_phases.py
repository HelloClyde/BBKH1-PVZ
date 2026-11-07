"""Split immutable actor pixels by lifetime without re-rendering any pixels."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zlib
from pack_actor_pages import decode, encode


def digest(data):
    return hashlib.sha256(data).hexdigest()


def split(source, normal, output):
    raw = decode(source)
    h = struct.unpack_from('<16I', raw)
    nh = struct.unpack_from('<16I', normal)
    assert h[:4] == nh[:4] == (0x50413148, 1, 480, 272)
    assert h[15] == 0 and nh[15] == 20
    ordinary = {struct.unpack_from('<Q4I', normal, 64+i*24)[:2] for i in range(nh[4])}
    entries = [struct.unpack_from('<Q4I', raw, 64+i*24) for i in range(h[4])]
    removed = [e for e in entries if e[:2] in ordinary]
    assert len(removed) == len(ordinary) == 211

    def packet(data, header, entry):
        _, _, offset, size, _ = entry
        head = data[offset:offset+16]
        segments, mode = struct.unpack_from('<HH', head, 4)
        assert mode in (0, 1)
        cursor = offset+16
        result = bytearray(head)
        for _ in range(segments):
            track, draw, runs, length = struct.unpack_from('<hHII', data, cursor)
            cursor += 12
            if length & 0x80000000:
                at = header[8]+(length & 0x7fffffff)
                length = struct.unpack_from('<I', data, at)[0]
                payload = data[at+4:at+4+length]
            else:
                payload = data[cursor:cursor+length]
                cursor += length
            assert len(payload) == length
            result.extend(struct.pack('<hHII', track, draw, runs, length))
            result.extend(payload)
        assert cursor == offset+size
        return bytes(result)

    records = []
    for name, kind in [('pea-transitions.h1ap', 4), ('zombie-hit.h1ap', 21), ('sod-roll.h1ap', 2)]:
        selected = [e for e in entries if e[4] == kind and e[:2] not in ordinary]
        assert selected
        palette = raw[h[7]:h[7]+h[6]*4]
        flash = raw[h[11]:h[11]+h[12]*h[13]*4] if kind == 21 else b''
        start = 64+len(selected)*24+len(palette)+len(flash)
        payload, table, offsets, streams = bytearray(), bytearray(), {}, {}
        expected = hashlib.sha256()
        for entry in selected:
            key, frame, _, _, type_id = entry
            pixels = packet(raw, h, entry)
            expected.update(pixels)
            # Retain sharing between track streams in distinct frames. Merely
            # inlining the old references would multiply the zombie bank.
            compact = bytearray(pixels[:16])
            flash_packet = struct.unpack_from('<H', pixels, 6)[0] & 1
            cursor = 16
            for _ in range(struct.unpack_from('<H', pixels, 4)[0]):
                track, draw, runs, length = struct.unpack_from('<hHII', pixels, cursor)
                cursor += 12
                stream = pixels[cursor:cursor+length]
                cursor += length
                if flash_packet:
                    if stream not in streams:
                        streams[stream] = len(payload)
                        payload.extend(struct.pack('<I', length)+stream)
                    compact.extend(struct.pack('<hHII', track, draw, runs, 0x80000000 | streams[stream]))
                else:
                    compact.extend(struct.pack('<hHII', track, draw, runs, length)+stream)
            assert cursor == len(pixels)
            compact = bytes(compact)
            if compact not in offsets:
                offsets[compact] = len(payload)
                payload.extend(compact)
            table.extend(struct.pack('<Q4I', key, frame, start+offsets[compact], len(compact), type_id))
        body = table+palette+flash+payload
        header = [0x50413148, 1, 480, 272, len(selected), 64, h[6], 64+len(table), start,
                  64+len(body), zlib.crc32(body), 64+len(table)+len(palette) if flash else 0,
                  h[12] if flash else 0, h[13] if flash else 0, h[14], 0]
        uncompressed = struct.pack('<16I', *header)+body
        encoded = encode(uncompressed)
        checked = decode(encoded)
        assert checked == uncompressed
        actual = hashlib.sha256()
        for i, original in enumerate(selected):
            key, frame, at, size, type_id = struct.unpack_from('<Q4I', checked, 64+i*24)
            assert (key, frame, type_id) == (original[0], original[1], original[4])
            restored = packet(checked, header, (key, frame, at, size, type_id))
            assert restored == packet(raw, h, original)
            actual.update(restored)
        assert actual.digest() == expected.digest()
        (output/name).write_bytes(encoded)
        records.append(dict(runtime_path=name, type=kind, entries=len(selected), bytes=len(encoded),
                            sha256=digest(encoded), decoded_bytes=len(checked),
                            decoded_sha256=digest(checked), ordered_packet_sha256=actual.hexdigest()))
    assert sum(r['entries'] for r in records)+len(removed) == len(entries)
    return dict(passed=True, source_sha256=digest(source), normal_sha256=digest(normal),
                encoder_sha256=digest(Path(__file__).read_bytes()), removed_ordinary_duplicates=len(removed),
                complete_source_entry_coverage=len(entries), packet_bytes_preserved=True,
                all_pixel_literals_preserved=True, lossy=False, files=records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--normal', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = split(args.source.read_bytes(), args.normal.read_bytes(), args.output)
    args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
