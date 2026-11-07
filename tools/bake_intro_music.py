"""Keep the original opening pool XM events/samples; strip unreachable tune data offline."""
from pathlib import Path
import argparse, hashlib, json, struct

ROOT = Path(__file__).resolve().parents[1]
u16 = lambda b, at: struct.unpack_from('<H', b, at)[0]
u32 = lambda b, at: struct.unpack_from('<I', b, at)[0]

def prune(source):
    data = source.read_bytes()
    assert data[:17] == b'Extended Module: ' and u16(data, 58) == 0x104
    channels, patterns, instruments = (u16(data, n) for n in (68, 70, 72))
    orders = list(range(0x5e, 0x7a))
    used_patterns = {data[80+n] for n in orders}
    used_instruments = set()
    at = 60 + u32(data, 60)
    out = bytearray(data[:at])
    for pattern in range(patterns):
        header, rows, packed = u32(data, at), u16(data, at+5), u16(data, at+7)
        end = at + header + packed
        assert end <= len(data) and header >= 9
        if pattern in used_patterns:
            p = at + header
            if packed:
                for _ in range(rows * channels):
                    flag = data[p]; p += 1
                    if flag & 0x80:
                        if flag & 1: p += 1
                        instrument = data[p] if flag & 2 else 0
                        if flag & 2: p += 1
                        for bit in (4, 8, 16):
                            if flag & bit: p += 1
                    else:
                        instrument = data[p]; p += 4
                    if instrument: used_instruments.add(instrument)
                assert p == end
            out.extend(data[at:end])
        else:
            out.extend(struct.pack('<IBHH', 9, 0, 1, 0))
        at = end
    retained_samples = original_samples = 0
    for instrument in range(1, instruments + 1):
        header, samples = u32(data, at), u16(data, at+27)
        sh = u32(data, at+29) if samples else 0
        length = sum(u32(data, at+header+i*sh) for i in range(samples))
        end = at + header + samples * sh + length
        assert end <= len(data) and header >= 29
        original_samples += length
        if instrument in used_instruments:
            out.extend(data[at:end]); retained_samples += length
        else:
            out.extend(struct.pack('<I', 29) + data[at+4:at+27] + b'\0\0')
        at = end
    assert at == len(data), (at, len(data))
    return bytes(out), dict(channels=channels, total_patterns=patterns,
        retained_patterns=sorted(used_patterns), total_instruments=instruments,
        retained_instruments=sorted(used_instruments), source_sample_bytes=original_samples,
        retained_sample_bytes=retained_samples, order_ranges=[[94,121]],
        pattern_and_instrument_ids_preserved=True)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=ROOT/'private/resources-h1-wide/sounds/mainmusic.xm')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args(); a.output.mkdir(parents=True, exist_ok=False)
    data, manifest = prune(a.source)
    target = a.output/'mainmusic-intro.xm'; target.write_bytes(data)
    manifest.update(source_sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),
        output_sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
        exporter_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (a.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest))

if __name__ == '__main__': main()
