"""Audit exact RGBA font palettes without changing any resource pixels."""
from pathlib import Path
import hashlib
import json
import struct

ROOT = Path(__file__).resolve().parents[1]
items = []
for source in sorted((ROOT / 'private/resources/data').glob('*.h1i')):
    data = source.read_bytes()
    magic, width, height, channels, flags, size = struct.unpack('<4s5I', data[:24])
    if channels != 4:
        continue
    if magic != b'H1I1' or len(data) != size + 24 or size != width * height * channels:
        raise ValueError('Invalid converted font header: ' + str(source))
    pixels = data[24:]
    colors = len(set(pixels[i:i + 4] for i in range(0, size, 4)))
    items.append(dict(path=source.relative_to(ROOT / 'private/resources').as_posix(),
        width=width, height=height, channels=channels, flags=flags, payload_bytes=size,
        ARGB_allocation_bytes=width * height * 4 + 4, distinct_RGBA_colors=colors,
        lossless_256_palette_eligible=colors <= 256,
        palette_pixel_and_table_bytes=width * height + 1024 if colors <= 256 else None,
        source_sha256=hashlib.sha256(data).hexdigest()))
report = dict(scope='offline existing converted RGBA data images only; no resources modified',
    resource_tree_modified=False, total_RGBA_data_images=len(items),
    eligible_count=sum(item['lossless_256_palette_eligible'] for item in items), images=items)
destination = ROOT / 'assets/rgba-font-palette-inventory.json'
destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
print(json.dumps(dict(report=str(destination), images=len(items), eligible=report['eligible_count'])))
