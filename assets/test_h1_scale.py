"""Check offline frame/glyph isolation, file identity and logical/physical maps."""
from pathlib import Path
import hashlib
import importlib.util
import json
import struct
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('h1_scale_converter', ROOT / 'tools/prepare_h1_assets.py')
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)
work = ROOT / 'build/h1-scale-tests'
work.mkdir(parents=True, exist_ok=True)


def original(path, image):
    pixels = image.tobytes()
    path.write_bytes(converter.I1.pack(b'H1I1', *image.size, 4, 0, len(pixels)) + pixels)


def pixel_hash(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rgba_colors(image):
    return set(struct.iter_unpack('4B', image.tobytes()))


tests = []
# Constant neighboring cel colors must never blend with each other at resize.
sheet = Image.new('RGBA', (22, 11))
sheet.paste((255, 0, 0, 160), (0, 0, 11, 11))
sheet.paste((0, 255, 0, 230), (11, 0, 22, 11))
source, target = work / 'two-cels.h1i', work / 'two-cels.h1i2'
original(source, sheet)
item = converter.convert(source, target, 9, 20, None, [[0, 0, 11, 11], [11, 0, 11, 11]])
info, pixels = converter.read_image(target)
scaled = Image.frombytes('RGBA', (info['storage_width'], info['storage_height']), pixels)
assert (info['width'], info['height'], *scaled.size) == (22, 11, 10, 5)
assert rgba_colors(scaled.crop((0, 0, 5, 5))) == {(255, 0, 0, 160)}
assert rgba_colors(scaled.crop((5, 0, 10, 5))) == {(0, 255, 0, 230)}
tests.append('RGBA neighboring cels stay isolated, including exact alpha')

# A one-logical-pixel gutter may vanish; independent glyph filtering is still
# isolated and the renderer has the exact half-open physical rectangles.
glyphs = Image.new('RGBA', (7, 5), (0, 0, 255, 0))
glyphs.paste((255, 0, 0, 255), (0, 0, 3, 5))
glyphs.paste((0, 255, 0, 255), (4, 0, 7, 5))
original(work / 'glyphs.h1i', glyphs)
item = converter.convert(work / 'glyphs.h1i', work / 'glyphs.h1i2', 9, 20, None,
                         [[0, 0, 3, 5], [4, 0, 3, 5]])
assert item['storage_rectangles'] == [[0, 0, 1, 2], [1, 0, 2, 2]]
info, pixels = converter.read_image(work / 'glyphs.h1i2')
scaled = Image.frombytes('RGBA', (3, 2), pixels)
assert rgba_colors(scaled.crop((0, 0, 1, 2))) == {(255, 0, 0, 255)}
assert rgba_colors(scaled.crop((1, 0, 3, 2))) == {(0, 255, 0, 255)}
tests.append('Glyph floor-boundary mapping prevents adjacent glyph color bleed')

original(work / 'one-pixel.h1i', Image.new('RGBA', (11, 5), (255, 255, 255, 255)))
try:
    converter.convert(work / 'one-pixel.h1i', work / 'one-pixel.h1i2', 9, 20, None, [[0, 0, 1, 5]])
except ValueError as error:
    assert 'zero texels' in str(error)
else:
    raise AssertionError('A disappearing nonempty glyph must not be fabricated')
tests.append('Nonempty logical glyph mapping to zero texels is rejected')

baseline = ROOT / 'private/resources'
full = ROOT / 'private/resources-h1-full'
index = json.loads((baseline / 'resource-index.json').read_text(encoding='utf8'))
manifest = json.loads((full / 'h1-scale-manifest.json').read_text(encoding='utf8'))
rectangle_count = 0
for name, item in manifest['scaled_images'].items():
    assert item['baseline_sha256'] == index['images'][name]['sha256']
    assert pixel_hash(full / name) == item['sha256']
    info, pixels = converter.read_image(full / name)
    assert info['format'] == 'H1I2'
    assert info['width'] == index['images'][name]['width']
    assert info['height'] == index['images'][name]['height']
    logical = item.get('logical_rectangles', [])
    physical = item.get('storage_rectangles', [])
    assert len(logical) == len(physical)
    for (x, y, w, h), (sx, sy, sw, sh) in zip(logical, physical):
        assert [sx, sy, sx + sw, sy + sh] == [x * info['storage_width'] // info['width'],
            y * info['storage_height'] // info['height'],
            (x + w) * info['storage_width'] // info['width'],
            (y + h) * info['storage_height'] // info['height']]
        assert sw > 0 and sh > 0
        rectangle_count += 1
for name, item in manifest['preserved_images'].items():
    assert pixel_hash(full / name) == item['sha256'] == index['images'][name]['sha256']
    assert converter.read_image(full / name)[0]['format'] == 'H1I1'
tests.append('All converted original-image identities and nonempty rectangle bins verified')
tests.append('All safety-exception images retained byte-for-byte as H1I1')
menu = json.loads((ROOT / 'private/resources-h1/h1-scale-manifest.json').read_text(encoding='utf8'))
assert all(pixel_hash(full / name) == item['sha256'] for name, item in menu['scaled_images'].items())
tests.append('Frozen nine-image menu prototype bytes match the corresponding full-stage images')

unchanged_files = 0
for path in baseline.rglob('*'):
    if not path.is_file() or path.suffix in ('.h1i', '.h1a') or path.name in ('resource-index.json', 'image-metadata.h1x'):
        continue
    assert pixel_hash(path) == pixel_hash(full / path.relative_to(baseline))
    unchanged_files += 1
tests.append('Original audio, module, XML, font metrics, text and definition files unchanged')
report = dict(schema='h1-offline-scale-verification-1', passed=True, native_verified=False,
    prototype_menu_native_pending=True, tests=tests, scaled_images=len(manifest['scaled_images']),
    preserved_images=len(manifest['preserved_images']), rectangle_bins_checked=rectangle_count,
    unchanged_nonimage_files=unchanged_files, metadata_header_comparisons=manifest['metadata_index']['files'],
    full_manifest_sha256=pixel_hash(full / 'h1-scale-manifest.json'),
    frozen_menu_manifest_sha256=pixel_hash(ROOT / 'private/resources-h1/h1-scale-manifest.json'))
destination = ROOT / 'assets/h1-offline-scale-verification.json'
destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
print(json.dumps(report, indent=2))
