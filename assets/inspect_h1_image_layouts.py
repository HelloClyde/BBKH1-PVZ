"""Read original sprite grids and font glyph rectangles before offline scaling."""
from pathlib import Path
import json
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'private/resources'
INDEX = json.loads((BASE / 'resource-index.json').read_text(encoding='utf8'))
IMAGES = INDEX['images']


def image(stem):
    stem = stem.replace('\\', '/').lower()
    for extension in ('.h1a', '.h1i'):
        if stem + extension in IMAGES:
            return stem + extension, IMAGES[stem + extension]
    return None, None


grids = []
ids = {}
for group in ET.parse(BASE / 'properties/resources.xml').getroot():
    directory = prefix = ''
    for node in group:
        if node.tag == 'SetDefaults':
            directory = node.get('path', directory)
            prefix = node.get('idprefix', prefix)
        elif node.tag == 'Image':
            stem = (directory + '/' + node.attrib['path']).lower()
            path, info = image(stem)
            cols, rows = int(node.get('cols', '1')), int(node.get('rows', '1'))
            item = dict(id=prefix + node.attrib['id'], path=path, stem=stem,
                        cols=cols, rows=rows, group=group.get('id'))
            if info:
                w, h = info['width'], info['height']
                item.update(width=w, height=h, cell_width=w // cols, cell_height=h // rows,
                            evenly_divisible=w % cols == 0 and h % rows == 0,
                            alpha_grid=node.get('alphagrid'))
            ids[item['id']] = item
            if cols > 1 or rows > 1:
                grids.append(item)

conflicts = []
for stem in sorted({item['stem'] for item in ids.values()}):
    uses = [item for item in ids.values() if item['stem'] == stem]
    values = sorted({(item['cols'], item['rows']) for item in uses})
    if len(values) > 1:
        conflicts.append(dict(stem=stem, grids=values, ids=[item['id'] for item in uses]))

fonts = []
for source in sorted((BASE / 'data').glob('*.txt')):
    text = source.read_bytes().decode('latin1')
    if 'LayerSetImage' not in text:
        continue
    rects = {}
    for match in re.finditer(r'Define\s+(\w+)\s*\((.*?)\);', text, re.S):
        values = [tuple(map(int, item)) for item in re.findall(r'\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)', match[2])]
        if values:
            rects[match[1]] = values
    maps = {match[1]: match[3] for match in re.finditer(r'LayerSetImageMap\s+(\w+)\s+(\w+)\s+(\w+)', text)}
    for match in re.finditer(r'LayerSetImage\s+(\w+)\s+[\'\"]([^\'\"]+)[\'\"]', text):
        layer, stem = match[1], 'data/' + match[2]
        path, info = image(stem)
        values = rects.get(maps.get(layer), [])
        item = dict(definition=source.relative_to(BASE).as_posix(), layer=layer, image=path,
                    rect_list=maps.get(layer), glyph_rectangles=len(values),
                    rectangles=[list(value) for value in values])
        if info:
            item.update(width=info['width'], height=info['height'],
                out_of_bounds=sum(x < 0 or y < 0 or w < 0 or h < 0 or x + w > info['width'] or y + h > info['height'] for x, y, w, h in values),
                touching_or_overlapping_glyph_pairs=sum(
                    x1 <= x2 + w2 and x2 <= x1 + w1 and y1 <= y2 + h2 and y2 <= y1 + h1
                    for i, (x1, y1, w1, h1) in enumerate(values)
                    for x2, y2, w2, h2 in values[i + 1:]))
        fonts.append(item)

particle_frames = []
for source in sorted((BASE / 'particles').glob('*.xml')):
    text = source.read_bytes().decode('latin1')
    for emitter in re.findall(r'<Emitter>(.*?)</Emitter>', text, re.S):
        target = re.search(r'<Image>(.*?)</Image>', emitter)
        frame_count = re.search(r'<ImageFrames>(.*?)</ImageFrames>', emitter)
        if target and frame_count:
            key = target[1].strip()
            resource = ids.get(key)
            particle_frames.append(dict(definition=source.relative_to(BASE).as_posix(),
                image_id=key, frames=frame_count[1].strip(),
                manifest_cols=resource['cols'] if resource else None,
                manifest_rows=resource['rows'] if resource else None))

report = dict(schema='h1-offline-layout-audit-1', source_resources_modified=False,
    declared_sprite_grids=grids, conflicting_grid_uses=conflicts,
    non_divisible_grids=[item for item in grids if item.get('evenly_divisible') is False],
    font_layers=fonts, particle_frame_uses=particle_frames,
    policy='Menu prototype leaves every grid/font unchanged. Later grid conversion must resize each cel independently; glyph scaling requires source-rectangle mapping and clipping tests.')
destination = ROOT / 'build/h1-scale-prototype/layout-audit.json'
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
print(json.dumps(dict(path=str(destination), grids=len(grids), conflicts=len(conflicts),
    non_divisible=len(report['non_divisible_grids']), font_layers=len(fonts),
    font_rectangles=sum(item['glyph_rectangles'] for item in fonts), particle_frame_uses=len(particle_frames))))
