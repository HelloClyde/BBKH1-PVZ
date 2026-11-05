"""Pack short game effects, preserving every signed 16-bit PCM sample."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, default=ROOT/'private/resources-h1-wide')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    body = bytearray()
    files = {}
    for path in sorted((args.resources/'sounds').glob('*.h1pcm')):
        data = path.read_bytes()
        if len(data)>32768 or path.stem in ('losemusic', 'evillaugh', 'dirt_rise'):
            continue
        magic, version, rate, samples = struct.unpack_from('<4s3I', data)
        assert (magic, version, rate)==(b'H1PC', 1, 16000) and len(data)==16+samples*2
        name = path.relative_to(args.resources).as_posix().encode('ascii')
        body += struct.pack('<3I', len(name), samples, zlib.crc32(data[16:]))+name+data[16:]
        files[name.decode()] = hashlib.sha256(data).hexdigest()
    packed = struct.pack('<4s3I', b'H1SB', 1, len(files), len(body))+body
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(packed)
    manifest = dict(format='H1SB1 sample-exact short PCM bank', runtime_path='sounds/effects.h1sb',
                    count=len(files), max_file_bytes=32768, bytes=len(packed), sha256=hashlib.sha256(packed).hexdigest(),
                    source_pcm_sha256=files, encoder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    args.output.with_suffix('.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf8')
    print(json.dumps({k:v for k,v in manifest.items() if k!='source_pcm_sha256'}))


if __name__=='__main__':
    main()
