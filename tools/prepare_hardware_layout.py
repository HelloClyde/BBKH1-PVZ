"""Prepare the optional byte-preserving, 64-bucket native H1 resource tree.

The original flat tree stays installed for compatibility and rollback. Copy
H1DATA first and H1DATA.MAP last. Save data and original image metadata are not
rewritten. Every copied asset is checked against the frozen release manifest.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PREFIX = 'A/应用/数据/游戏/PVZ/'
KINDS = {'data', 'images', 'particles', 'reanim', 'sounds'}
MARKER = b'H1BK' + struct.pack('<I', 1) + b'FN1A' + struct.pack('<I', 64)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def bucket_path(relative):
    canonical = ''.join(c.lower() if 'A' <= c <= 'Z' else c for c in relative.replace('\\', '/'))
    h = 0x811c9dc5
    for byte in canonical.encode('ascii'):
        h = ((h ^ byte) * 0x01000193) & 0xffffffff
    return f'H1DATA/{h & 63:02x}/' + canonical


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'dist/release-20261101-h1-paths/H1PVZPortable-resources.zip')
    parser.add_argument('--output', type=Path, default=ROOT/'private/resources-h1-hardware-patch')
    args = parser.parse_args()
    assert not args.output.exists(), 'Preserve existing prepared resources'
    source_bytes = args.source.read_bytes()
    assert digest(source_bytes) == 'faed7ca862c57507394b1284f60504fe43fcfe123b5b40c2d1e9d9f3779c9fbe'
    args.output.mkdir(parents=True)
    entries, targets = [], set()
    with zipfile.ZipFile(args.source) as archive:
        original = json.loads(archive.read('package-manifest.json'))
        for name, meta in original['files'].items():
            if not name.startswith(PREFIX):
                continue
            relative = name.removeprefix(PREFIX)
            parts = PurePosixPath(relative).parts
            if len(parts) < 2 or parts[0] not in KINDS or '.' not in parts[-1]:
                continue
            assert '..' not in parts
            target_name = bucket_path(relative)
            assert target_name not in targets, 'Case-folded resource collision'
            targets.add(target_name)
            data = archive.read(name)
            assert len(data) == meta['bytes'] and digest(data) == meta['sha256']
            target = args.output.joinpath(*PurePosixPath(target_name).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            assert digest(target.read_bytes()) == meta['sha256']
            entries.append({'logical_path': relative, 'physical_path': target_name, **meta})
    (args.output/'H1DATA.MAP').write_bytes(MARKER)
    report = {'schema': 'h1-resource-bucket-layout-1', 'source_zip_sha256': digest(source_bytes),
              'source_asset_commit': original['source_asset_commit'], 'marker_hex': MARKER.hex(),
              'algorithm': 'FNV1a32 of ASCII-lower full relative path with slash, bucket=hash&63',
              'bucket_count': 64, 'files': entries, 'asset_count': len(entries),
              'asset_bytes': sum(e['bytes'] for e in entries), 'all_assets_identical_to_original': True,
              'original_flat_files_retained': True, 'savedata_included': False,
              'probe_paths': {p: bucket_path(p) for p in ['images/Background1.h1i', 'reanim/PotatoMine_rock3.h1i', 'data/DwarvenTodcraft18GreenInset.txt', 'sounds/mainmusic.xm']}}
    (args.output/'hardware-layout-manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps({k: v for k, v in report.items() if k != 'files'}, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
