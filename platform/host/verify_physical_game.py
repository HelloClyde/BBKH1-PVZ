"""Bind the actual scaled/physical first-level and portable-v4 host runs."""
from pathlib import Path
import hashlib
import json
import zlib

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'build/host-physical'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_run(name):
    path = BUILD / name
    states = [json.loads(line) for line in (path / 'states.jsonl').read_text().splitlines()]
    metadata = json.loads((path / 'run-metadata.json').read_text())
    assert metadata['exit_code'] == 0 and metadata['physical_screen']
    assert metadata['read_only_entity_input_controller']
    assert all(s['image_load_failures'] == 0 and s['physical_screen'][5] == 0 for s in states)
    return path, states, metadata


def save_evidence(path):
    data = path.read_bytes()
    assert data[:12] == b'PVZP_SAVE4\0\0'
    version = int.from_bytes(data[12:16], 'little')
    size = int.from_bytes(data[16:20], 'little')
    crc = int.from_bytes(data[20:24], 'little')
    assert version == 1 and len(data) == size + 24 and zlib.crc32(data[24:]) & 0xffffffff == crc
    return {'path': str(path.relative_to(ROOT)), 'bytes': len(data), 'sha256': sha(path),
            'version': version, 'payload_bytes': size, 'crc32': crc, 'valid': True}


def main():
    first_path, first, first_meta = read_run('run-first')
    restored_path, restored, restored_meta = read_run('run-new-v4-restore')
    event = {s['event']: s for s in first if s['event'] not in ('frame', 'scene', 'loading-frame')}
    for name in ('first-plant', 'first-projectile', 'first-hit', 'pause-held', 'resumed',
                 'level-award-input', 'level2-after-first-win'):
        assert name in event
    assert event['first-projectile']['projectiles'] > 0
    hit = event['first-hit']
    assert any(z['hp'] < z['max_hp'] for z in hit['zombies'])
    paused = [s for s in first if s.get('paused')]
    assert len(paused) >= 2 and len({s['board_counter'] for s in paused}) == 1
    assert event['resumed']['board_counter'] > paused[-1]['board_counter']
    level1 = [s for s in first if s.get('level') == 1 and s['scene'] == 3]
    assert max(s['wave'] for s in level1) == 4 and {s['total_waves'] for s in level1} == {4}
    assert any(s['scene'] == 5 for s in first)
    assert event['level2-after-first-win']['profile_level'] == 2
    assert 'Loaded save game (v4)' in (restored_path / 'pvz.log').read_text()
    assert any(s['event'] == 'resume-save-input' for s in restored)
    assert any(s['scene'] == 3 and s['level'] == 2 and not s.get('paused') for s in restored)
    assert restored[-1]['scene'] == 1
    labels = [d['restart_label'] for s in restored for d in s.get('dialogs', []) if 'restart_label' in d]
    assert labels and all(label == 'Restart Level' for label in labels)
    fixtures = ['build/reduced-opaque-probe/run/reduced-render-verification.json',
                'build/physical-screen-probe/run/physical-screen-verification.json',
                'assets/h1-offline-scale-verification.json']
    report = {
        'scope': 'Host-only original game with offline H1 assets, logical 800x600 coordinates and physical 360x270 screen; not a native performance result',
        'passed': True, 'input_only': True,
        'input_statement': 'The script and read-only entity controller send ordinary Input.cpp key/mouse events. They do not modify game fields, clocks, resources or save contents.',
        'fresh': {'run': str(first_path.relative_to(ROOT)), 'metadata': first_meta, 'frames': first[-1]['frame'],
                  'states_sha256': sha(first_path / 'states.jsonl'),
                  'events': {name: {'frame': event[name]['frame'], 'level': event[name]['level'],
                                   'board_counter': event[name]['board_counter']} for name in event if name in
                             ('first-plant', 'first-projectile', 'first-hit', 'pause-held', 'resumed',
                              'level-award-input', 'level2-after-first-win')},
                  'first_damage': hit['zombies'], 'four_waves': True, 'original_reward_and_level2_unlock': True,
                  'pause': [{'frame': s['frame'], 'board_counter': s['board_counter']} for s in paused],
                  'cache_peak_bytes': max(s['source_cache_peak'] for s in first),
                  'image_evictions': first[-1]['image_evictions']},
        'new_v4_checkpoint': save_evidence(BUILD / 'save-first/userdata/game1_0.v4'),
        'restore': {'run': str(restored_path.relative_to(ROOT)), 'metadata': restored_meta,
                    'frames': restored[-1]['frame'], 'states_sha256': sha(restored_path / 'states.jsonl'),
                    'original_continue_and_return_menu': True, 'restart_labels': labels},
        'image_failures': 0, 'screen_raw_pointer_fallbacks': 0,
        'fixtures': {name: sha(ROOT / name) for name in fixtures},
        'resource_manifest_sha256': sha(ROOT / 'private/resources-h1-full/h1-scale-manifest.json'),
        'menu_report': {'path': 'build/host-physical/menu-verification.json',
                        'sha256': sha(BUILD / 'menu-verification.json')},
    }
    target = BUILD / 'game-verification.json'
    target.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(target)
    print(sha(target))


if __name__ == '__main__':
    main()
