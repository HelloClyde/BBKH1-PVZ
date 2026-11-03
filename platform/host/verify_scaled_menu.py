"""Report real host menu frames for an offline-storage/physical-screen profile."""
from pathlib import Path
import argparse
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--physical-screen', action='store_true')
    args = parser.parse_args()
    run = args.run.resolve()
    states = [json.loads(line) for line in (run / 'states.jsonl').read_text().splitlines()]
    meta = json.loads((run / 'run-metadata.json').read_text())
    warm = [s for s in states if s['event'] == 'frame' and s['scene'] == 1 and s['frame'] >= 360]
    assert len(warm) >= 3 and states[-1]['event'] == 'closed' and meta['exit_code'] == 0
    assert all(s['image_load_failures'] == 0 for s in states)
    before, after = warm[0], warm[-1]
    fields = ['image_loads', 'image_evictions', 'image_pixel_payload_bytes_loaded']
    deltas = {name: after[name] - before[name] for name in fields}
    assert not any(deltas.values())
    if args.physical_screen:
        assert all(s['physical_screen'][0] == 1 and s['physical_screen'][1:5] == [360, 270, 800, 600]
                   and s['physical_screen'][5] == 0 for s in warm)
    manifest = args.resources.resolve() / 'h1-scale-manifest.json'
    report = {
        'scope': 'Host-only complete original game menu; ordinary scripted mouse input; no emulator or physical H1 FPS claim',
        'passed': True, 'run': str(run.relative_to(ROOT)), 'resource_profile': str(manifest.relative_to(ROOT)),
        'resource_manifest_sha256': sha(manifest), 'metadata': meta,
        'states_sha256': sha(run / 'states.jsonl'),
        'compile_report_sha256': sha(run / 'compile-report.json'),
        'warm_frame_range': [before['frame'], after['frame']], 'warm_io_deltas': deltas,
        'host_warm_fps': (after['frame'] - before['frame']) * 1000 / (after['wall_ms'] - before['wall_ms']),
        'source_cache_bytes': after['source_cache_bytes'], 'storage_diagnostic': after['storage_textures'],
        'physical_screen': after.get('physical_screen'), 'image_failures': 0,
        'logical_fallback_policy': 'Generic GetBits retains logical nearest-expanded pixels. PoolEffect startup grayscale preparation may use this explicit fallback; dense source draws do not.',
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(args.output)
    print(sha(args.output))


if __name__ == '__main__':
    main()
