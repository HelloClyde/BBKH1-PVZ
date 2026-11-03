"""Build a separate offline-scaled H1 image tree from the verified baseline.

H1I2: <4s7I magic,logicalW,H,channels,flags,physicalBytes,storageW,H>.
H1X2: the existing 28-byte index header, version2 and 32-byte records:
nameOffset,logicalW,H,channels,flags,physicalBytes,storageW,H.
The initial menu profile changes only the three single-image Selector B/Gs.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import PIL
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
I1 = struct.Struct('<4s5I')
I2 = struct.Struct('<4s7I')
X2 = struct.Struct('<4s6I')
REC = struct.Struct('<8I')
MENU_STEMS = ('selectorscreen_bg_left', 'selectorscreen_bg_right', 'selectorscreen_bg_center')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def read_image(path):
    data = Path(path).read_bytes()
    if data[:4] == b'H1I1':
        _, w, h, channels, flags, size = I1.unpack_from(data)
        sw, sh, offset = w, h, I1.size
    elif data[:4] == b'H1I2':
        _, w, h, channels, flags, size, sw, sh = I2.unpack_from(data)
        offset = I2.size
    else:
        raise ValueError('Unknown image header: ' + str(path))
    if channels not in (1, 3, 4) or flags not in (0, 1) or not all((w, h, sw, sh)):
        raise ValueError('Invalid image metadata: ' + str(path))
    if size != sw * sh * channels or len(data) != offset + size:
        raise ValueError('Invalid physical image payload: ' + str(path))
    return dict(width=w, height=h, storage_width=sw, storage_height=sh,
                channels=channels, flags=flags, pixel_bytes=size, format=data[:4].decode()), data[offset:]


def physical_size(n, numerator, denominator):
    return max(1, (n * numerator + denominator // 2) // denominator)


def convert(path, destination, numerator, denominator, preview, rectangles=None):
    info, pixels = read_image(path)
    if info['format'] != 'H1I1':
        raise ValueError('Scale input must be the untouched H1I1 baseline')
    mode = {1: 'L', 3: 'RGB', 4: 'RGBA'}[info['channels']]
    image = Image.frombytes(mode, (info['width'], info['height']), pixels)
    size = (physical_size(info['width'], numerator, denominator),
            physical_size(info['height'], numerator, denominator))
    def resize(source, target):
        # Filtering premultiplied color prevents transparent edge RGB leaking.
        if mode == 'RGBA':
            return source.convert('RGBa').resize(target, Image.Resampling.LANCZOS).convert('RGBA')
        return source.resize(target, Image.Resampling.LANCZOS)
    mapped = []
    if rectangles is None:
        scaled = resize(image, size)
    else:
        scaled = Image.new(mode, size, 0)
        for x, y, width, height in rectangles:
            if not width or not height:
                continue
            left, top = x * size[0] // info['width'], y * size[1] // info['height']
            right = (x + width) * size[0] // info['width']
            bottom = (y + height) * size[1] // info['height']
            if right <= left or bottom <= top:
                raise ValueError('Nonempty source rectangle maps to zero texels: ' + str(path))
            scaled.paste(resize(image.crop((x, y, x + width, y + height)),
                                (right - left, bottom - top)), (left, top))
            mapped.append([left, top, right - left, bottom - top])
    payload = scaled.tobytes()
    result = dict(info, storage_width=size[0], storage_height=size[1],
                  pixel_bytes=len(payload), format='H1I2')
    destination.write_bytes(I2.pack(b'H1I2', info['width'], info['height'],
        info['channels'], info['flags'], len(payload), size[0], size[1]) + payload)
    if preview:
        preview.parent.mkdir(parents=True, exist_ok=True)
        scaled.save(preview)
    result.update(baseline_sha256=digest(path), sha256=digest(destination),
                  baseline_bytes=path.stat().st_size, bytes=destination.stat().st_size)
    if rectangles is not None:
        result.update(rectangle_mapping='floor(bound*storage/logical)',
                      logical_rectangles=rectangles, storage_rectangles=mapped)
    return result


def canonical_stem(name):
    path = Path(name)
    return (path.parent / path.stem.strip('_')).as_posix().lower()


def full_layouts(baseline, numerator, denominator):
    subprocess.run([sys.executable, str(ROOT / 'assets/inspect_h1_image_layouts.py')], check=True)
    audit = json.loads((ROOT / 'build/h1-scale-prototype/layout-audit.json').read_text(encoding='utf8'))
    rectangles, reasons = {}, {}
    def preserve(stem, reason):
        reasons.setdefault(stem, []).append(reason)
    for item in audit['declared_sprite_grids']:
        stem = item['stem']
        if not item.get('evenly_divisible'):
            preserve(stem, 'source grid dimensions not divisible by cols/rows')
            continue
        values = [[col * item['cell_width'], row * item['cell_height'],
            item['cell_width'], item['cell_height']] for row in range(item['rows']) for col in range(item['cols'])]
        sw = physical_size(item['width'], numerator, denominator)
        sh = physical_size(item['height'], numerator, denominator)
        if any((x + w) * sw // item['width'] == x * sw // item['width'] or
               (y + h) * sh // item['height'] == y * sh // item['height'] for x, y, w, h in values):
            preserve(stem, 'nonempty sprite cell maps to zero texels')
            continue
        rectangles[stem] = values
    for item in audit['font_layers']:
        if not item['image']:
            raise ValueError('Font layer has no source image')
        stem = canonical_stem(item['image'])
        values = item['rectangles']
        if item.get('out_of_bounds') or item.get('touching_or_overlapping_glyph_pairs'):
            preserve(stem, 'font layer has touching or overlapping glyph rectangles')
        sw = physical_size(item['width'], numerator, denominator)
        sh = physical_size(item['height'], numerator, denominator)
        zeros = [i for i, (x, y, w, h) in enumerate(values) if w and h and
            ((x + w) * sw // item['width'] == x * sw // item['width'] or
             (y + h) * sh // item['height'] == y * sh // item['height'])]
        if zeros:
            preserve(stem, 'nonempty glyph rectangle maps to zero texels: ' + ','.join(map(str, zeros)))
        if stem in rectangles and rectangles[stem] != values:
            preserve(stem, 'same source atlas has conflicting rectangle layouts')
        rectangles[stem] = values
    # Existing engine alpha-grid composition operates in logical space. Keep
    # these source families at I1 until its physical overlay adapter is ready.
    for group in ET.parse(baseline / 'properties/resources.xml').getroot():
        directory = ''
        for node in group:
            if node.tag == 'SetDefaults':
                directory = node.get('path', directory)
            elif node.tag == 'Image' and 'alphagrid' in node.attrib:
                for name in (node.attrib['path'], node.attrib['alphagrid']):
                    preserve(canonical_stem(directory + '/' + name), 'logical alpha-grid source or mask')
    return rectangles, reasons, audit


def metadata_index(output):
    records = []
    names = bytearray()
    infos = {}
    files = sorted((p for p in output.rglob('*') if p.suffix in ('.h1i', '.h1a')),
                   key=lambda p: p.relative_to(output).as_posix().encode('ascii'))
    for path in files:
        name = path.relative_to(output).as_posix()
        if name != name.lower():
            raise ValueError('Non-canonical metadata path: ' + name)
        info, _ = read_image(path)
        infos[name] = info
        records.append(REC.pack(len(names), info['width'], info['height'], info['channels'],
            info['flags'], info['pixel_bytes'], info['storage_width'], info['storage_height']))
        names += name.encode('ascii') + b'\0'
    body = b''.join(records) + names
    checksum = 2166136261
    for value in body:
        checksum = ((checksum ^ value) * 16777619) & 0xffffffff
    destination = output / 'image-metadata.h1x'
    destination.write_bytes(X2.pack(b'H1X2', 2, len(records), REC.size,
                                   len(names), len(body), checksum) + body)
    # Decode the actual output and compare every field to each actual header.
    encoded = destination.read_bytes()
    magic, version, count, stride, strings, total, check = X2.unpack_from(encoded)
    assert (magic, version, count, stride, strings, total, check) == (b'H1X2', 2, len(infos), 32, len(names), len(body), checksum)
    name_data = encoded[X2.size + count * stride:]
    for i, (name, info) in enumerate(infos.items()):
        offset, w, h, channels, flags, size, sw, sh = REC.unpack_from(encoded, X2.size + i * stride)
        actual_name = name_data[offset:].split(b'\0', 1)[0].decode('ascii')
        assert actual_name == name
        assert (w, h, channels, flags, size, sw, sh) == (info['width'], info['height'], info['channels'], info['flags'], info['pixel_bytes'], info['storage_width'], info['storage_height'])
    return dict(path=destination.name, format='H1X2 version2, 32-byte logical/physical records',
                files=len(infos), bytes=len(encoded), sha256=digest(destination),
                actual_file_header_comparisons=len(infos)), infos


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, default=ROOT / 'private/resources')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--profile', choices=['menu', 'full'], default='menu')
    parser.add_argument('--numerator', type=int, default=9)
    parser.add_argument('--denominator', type=int, default=20)
    args = parser.parse_args()
    baseline = args.baseline.resolve()
    output = (args.output or ROOT / ('private/resources-h1-full' if args.profile == 'full' else 'private/resources-h1')).resolve()
    if baseline == output or output.is_relative_to(baseline) or baseline.is_relative_to(output) or not output.is_relative_to((ROOT / 'private').resolve()):
        raise ValueError('Output must be a separate directory in this project private folder')
    if not 0 < args.numerator < args.denominator:
        raise ValueError('This profile requires a reduction ratio between zero and one')
    owner = output / 'h1-scale-owner.json'
    if output.exists() and any(output.iterdir()) and not owner.is_file():
        raise ValueError('Refusing to overwrite an unowned resource directory')
    if owner.is_file() and json.loads(owner.read_text())['owner'] != 'h1-pvz-offline-scale-v1':
        raise ValueError('Scale output owner mismatch')
    before = digest(baseline / 'resource-index.json')
    output.mkdir(parents=True, exist_ok=True)
    write_json(owner, dict(owner='h1-pvz-offline-scale-v1', baseline=str(baseline),
                           baseline_index_sha256=before))
    shutil.copytree(baseline, output, dirs_exist_ok=True, copy_function=shutil.copy2)
    preview = ROOT / ('build/h1-scale-full' if args.profile == 'full' else 'build/h1-scale-prototype')
    changed = {}
    preserved = {}
    layouts = {}; reasons = {}
    if args.profile == 'full':
        layouts, reasons, audit = full_layouts(baseline, args.numerator, args.denominator)
        selected = sorted(p for p in baseline.rglob('*') if p.suffix in ('.h1i', '.h1a'))
    else:
        selected = [baseline / 'reanim' / name for stem in MENU_STEMS
            for name in (stem + '.h1i', stem + '.h1a', stem + '_.h1i', '_' + stem + '.h1i')
            if (baseline / 'reanim' / name).is_file()]
    for number, path in enumerate(selected, 1):
        relative = path.relative_to(baseline).as_posix()
        stem = canonical_stem(relative)
        if stem in reasons:
            preserved[relative] = dict(reasons=reasons[stem], sha256=digest(path), bytes=path.stat().st_size)
            continue
        changed[relative] = convert(path, output / relative, args.numerator, args.denominator,
            preview / (path.name + '.png') if args.profile == 'menu' else None,
            rectangles=layouts.get(stem))
        if number % 300 == 0:
            print(f'Offline scaled {number}/{len(selected)} images', flush=True)
    if not changed:
        raise ValueError('No original menu backgrounds found')
    index, infos = metadata_index(output)
    report = json.loads((baseline / 'resource-index.json').read_text(encoding='utf8'))
    for name, item in changed.items():
        if item['baseline_sha256'] != report['images'][name]['sha256']:
            raise ValueError('Original baseline image identity changed: ' + name)
    for name, item in preserved.items():
        if item['sha256'] != report['images'][name]['sha256']:
            raise ValueError('Preserved baseline image identity changed: ' + name)
    for name, item in changed.items():
        report['images'][name].update(item)
    report.update(schema='h1-pvz-resources-2',
        resource_dimensions='original logical dimensions; offline-scaled physical storage for profile ' + args.profile,
        image_format='H1I1 native images plus H1I2 logical/physical images',
        image_metadata_index=index, image_bytes=sum(p.stat().st_size for p in output.rglob('*') if p.suffix in ('.h1i', '.h1a')))
    manifest = dict(schema='h1-pvz-offline-scale-1', profile=args.profile,
        ratio=dict(numerator=args.numerator, denominator=args.denominator),
        dimension_rounding='max(1,(n*numerator+denominator//2)//denominator)',
        resampling='Pillow Lanczos; RGBA filtered in premultiplied RGBa then unpremultiplied',
        pillow_version=PIL.__version__, original_logical_coordinates_preserved=True,
        source_resource_index_sha256=before, production_baseline_modified=False,
        unscaled_fonts=args.profile == 'menu', unscaled_sprite_cels=args.profile == 'menu', audio_modified=False,
        scaled_images=changed, metadata_index=index,
        preserved_images=preserved,
        baseline_selected_bytes=sum(x['baseline_bytes'] for x in changed.values()),
        scaled_selected_bytes=sum(x['bytes'] for x in changed.values()))
    report['h1_offline_scale'] = {k: manifest[k] for k in ('profile', 'ratio', 'source_resource_index_sha256', 'production_baseline_modified', 'unscaled_fonts', 'unscaled_sprite_cels')}
    write_json(output / 'resource-index.json', report)
    write_json(output / 'h1-scale-manifest.json', manifest)
    preview.mkdir(parents=True, exist_ok=True)
    write_json(preview / (args.profile + '-scale-verification.json'), manifest)
    assert digest(baseline / 'resource-index.json') == before
    print(json.dumps(dict(output=str(output), images_changed=len(changed),
        baseline_bytes=manifest['baseline_selected_bytes'], scaled_bytes=manifest['scaled_selected_bytes'],
        metadata_comparisons=index['actual_file_header_comparisons'], index_sha256=index['sha256']), indent=2), flush=True)


if __name__ == '__main__':
    main()
