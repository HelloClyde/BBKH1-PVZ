"""Read-only audit of original SelectorScreen sparse tracks for offline baking."""
from pathlib import Path
import argparse, hashlib, json, xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DEFAULTS = dict(x=0., y=0., kx=0., ky=0., sx=1., sy=1., f=0., a=1., i='', font='', text='')

def inspect(path):
    root = ET.fromstring('<reanim>' + path.read_text(encoding='utf-8') + '</reanim>')
    tracks = []
    for element in root.findall('track'):
        previous = DEFAULTS.copy()
        frames = []
        for transform in element.findall('t'):
            for field in transform:
                value = field.text or ''
                previous[field.tag] = value if field.tag in ('i', 'font', 'text') else float(value)
            frames.append(previous.copy())
        visible = [i for i, t in enumerate(frames) if t['f'] >= 0]
        tracks.append(dict(name=element.findtext('name'), frames=frames,
                           first=visible[0] if visible else 0,
                           count=(visible[-1]-visible[0]+1) if visible else 1))
    animations = []
    for track in tracks:
        if not track['name'].startswith('anim_'):
            continue
        start, count = track['first'], track['count']
        active = []
        for other in tracks:
            if other['name'].startswith('anim_'):
                continue
            segment = other['frames'][start:start+count]
            painted = [t for t in segment if t['f'] >= 0 and t['a'] > 0 and (t['i'] or t['font'])]
            if painted:
                distinct = {json.dumps(t, sort_keys=True) for t in painted}
                active.append(dict(name=other['name'], distinct_painted_transforms=len(distinct),
                                   images=sorted({t['i'] for t in painted if t['i']})))
        animations.append(dict(name=track['name'], start=start, count=count,
                               end_inclusive=start+count-1, active=active))
    return dict(source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), fps=float(root.findtext('fps')),
                track_count=len(tracks), transforms=sum(len(t['frames']) for t in tracks),
                frame_lengths=sorted({len(t['frames']) for t in tracks}),
                tracks=[dict(name=t['name'], frames=len(t['frames']), first=t['first'], count=t['count']) for t in tracks],
                animations=animations)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'private/source/reanim/SelectorScreen.reanim')
    parser.add_argument('--output', type=Path, default=ROOT/'build/selector-bake-audit.json')
    args = parser.parse_args()
    result = inspect(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('track_count', 'transforms', 'frame_lengths', 'fps')}))
    for anim in result['animations']:
        print(anim['name'], anim['start'], anim['count'], 'active:', ','.join(t['name'] for t in anim['active']))

if __name__ == '__main__':
    main()
