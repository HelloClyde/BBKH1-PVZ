"""Summarize read-only firmware watch counters without controlling a guest.

Example: python tools/tests/analyze_native_performance.py watch.json --output build/performance.json
The result binds the source watch, BDA, and ELF identities. Counter changes show
I/O churn; they do not by themselves apportion CPU time versus filesystem time.
"""
from pathlib import Path
import argparse
import hashlib
import json


def difference(last, first):
    return (last - first) & 0xFFFFFFFF


def summarize(path, scene=None, skip_seconds=0):
    raw = path.read_bytes()
    watch = json.loads(raw)
    states = [s for s in watch['states'] if s['pvz_runtime_diagnostic'][1] == 2
              and (scene is None or s['pvzp_diagnostic'][7] == scene)]
    if states and skip_seconds:
        cutoff = states[0]['wall_monotonic'] + skip_seconds
        states = [s for s in states if s['wall_monotonic'] >= cutoff]
    if len(states) < 2:
        raise ValueError('Need at least two running application observations in the requested scene')
    first, last = states[0], states[-1]
    seconds = last['wall_monotonic'] - first['wall_monotonic']
    if seconds <= 0:
        raise ValueError('Non-increasing wall clock')
    frames = difference(last['pvzp_diagnostic'][2], first['pvzp_diagnostic'][2])
    updates = difference(last['pvzp_diagnostic'][8], first['pvzp_diagnostic'][8])
    cache0, cache1 = first['pvzh1_cache_diagnostic'], last['pvzh1_cache_diagnostic']
    result = {
        'schema': 'h1-pvz-native-performance-1',
        'source_watch': str(path), 'source_sha256': hashlib.sha256(raw).hexdigest(),
        'bda_sha256': watch['bda_sha256'], 'elf_sha256': watch['elf_sha256'],
        'observations': len(states), 'scene_filter': scene, 'skip_seconds': skip_seconds,
        'wall_seconds': seconds, 'rendered_frames': frames,
        'rendered_frames_per_second': frames / seconds, 'application_updates': updates,
        'file_opens': difference(last['pvz_runtime_diagnostic'][5], first['pvz_runtime_diagnostic'][5]),
        'source_loads': difference(cache1[2], cache0[2]),
        'source_evictions': difference(cache1[3], cache0[3]),
        'loaded_pixel_bytes': difference(cache1[5], cache0[5]),
        'pressure_callbacks': difference(last['pvz_runtime_pressure_diagnostic'][0], first['pvz_runtime_pressure_diagnostic'][0]),
        'pressure_recoveries': difference(last['pvz_runtime_pressure_diagnostic'][2], first['pvz_runtime_pressure_diagnostic'][2]),
        'source_resident_first': cache0[0], 'source_resident_last': cache1[0],
        'user_heap_first': first['pvz_runtime_diagnostic'][3],
        'user_heap_last': last['pvz_runtime_diagnostic'][3],
        'native_charge_first': first['pvz_runtime_native_charge'][0],
        'native_charge_last': last['pvz_runtime_native_charge'][0],
        'timing_scope': 'Wall time includes emulation, GDB sampling, rasterization and filesystem work; CPU/I/O time split is not instrumented',
    }
    if frames:
        result['loaded_pixel_bytes_per_frame'] = result['loaded_pixel_bytes'] / frames
        result['source_loads_per_frame'] = result['source_loads'] / frames
    for name in ('pvzh1_rgba_raster_page_diagnostic', 'pvzh1_paged_rgb_diagnostic',
                 'pvzh1_storage_texture_diagnostic', 'pvzh1_physical_screen_diagnostic'):
        if name in first and name in last:
            result[name] = {'first': first[name], 'last': last[name]}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('watch', type=Path)
    parser.add_argument('--scene', type=int)
    parser.add_argument('--skip-seconds', type=float, default=0, help='Skip initial warming observations')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.skip_seconds < 0:
        parser.error('--skip-seconds must be nonnegative')
    result = summarize(args.watch, args.scene, args.skip_seconds)
    output = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding='utf8')
    print(output)


if __name__ == '__main__':
    main()
