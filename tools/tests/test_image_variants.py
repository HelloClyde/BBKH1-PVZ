"""Actual image-reader index-first variants versus the original search order.

No device or firmware emulator is used. All fixtures/output live under build.
Stock physical pixels, including transparent RGB and alpha-mask compose color,
are compared byte-for-byte. Indexed metadata never replaces actual header proof.
"""
from pathlib import Path
import argparse
import ctypes as C
import hashlib
import json
import os
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/image-variant-test'
OUT.mkdir(parents=True, exist_ok=True)
FIX = OUT / 'fixtures'
FIX.mkdir(exist_ok=True)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='C:/msys64/ucrt64/bin/gcc.exe')
parser.add_argument('--resources', type=Path, default=ROOT / 'private/resources-h1-full')
args = parser.parse_args()
env = os.environ.copy()
env['PATH'] = str(Path(args.cc).parent) + os.pathsep + env.get('PATH', '')
dll = OUT / 'reader.dll'
command = [args.cc, '-shared', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
           '-I', str(ROOT / 'platform/resources'),
           str(ROOT / 'tools/tests/image_variant_probe.c'), '-o', str(dll)]
subprocess.run(command, check=True, env=env, timeout=45)
lib = C.CDLL(str(dll))


class Info(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('width', 'height', 'channels', 'flags',
                                        'pixel_bytes', 'storage_width', 'storage_height')]


class Image(C.Structure):
    _fields_ = [('file', C.c_void_p), ('info', Info), ('alpha_color', C.c_uint32),
                ('rgb_source_bytes', C.c_uint32), ('rgb_next_byte', C.c_uint32),
                ('rgb_stream_valid', C.c_uint32), ('payload_offset', C.c_uint32)]


lib.h1_resources_set_root.argtypes = [C.c_char_p]
for name in ('h1_image_open', 'test_legacy_open'):
    getattr(lib, name).argtypes = [C.POINTER(Image), C.c_char_p, C.c_int, C.c_uint32]
lib.h1_image_close.argtypes = [C.POINTER(Image)]
lib.h1_image_probe.argtypes = [C.c_char_p, C.c_int, C.POINTER(Info)]
lib.h1_image_load_storage.argtypes = [C.POINTER(Image), C.POINTER(C.c_uint32), C.c_size_t]
lib.test_open_path.argtypes = [C.c_uint]
lib.test_open_path.restype = C.c_char_p
checks = 0
cases = []


def check(value):
    global checks
    assert value
    checks += 1


def root(path):
    lib.h1_resources_set_root((path.as_posix() + '/').encode())
    lib.test_reset_open_log()


def info_tuple(info):
    return tuple(getattr(info, name) for name, _ in Info._fields_)


def image_attempts():
    return [Path(lib.test_open_path(i).decode()).name
            for i in range(lib.test_open_count())
            if lib.test_open_path(i).decode().endswith(('.h1i', '.h1a'))]


def opened(path, alpha=1, legacy=False, expected=True, color=0x2468ac):
    image = Image()
    function = lib.test_legacy_open if legacy else lib.h1_image_open
    check(bool(function(C.byref(image), path.encode(), alpha, color)) == expected)
    if not expected:
        check(not image.file)
        return None
    count = image.info.storage_width * image.info.storage_height
    pixels = (C.c_uint32 * count)()
    check(lib.h1_image_load_storage(C.byref(image), pixels, count) == 1)
    result = info_tuple(image.info), bytes(pixels), image.rgb_source_bytes, image.payload_offset
    lib.h1_image_close(C.byref(image))
    check(lib.test_balanced_files())
    return result


def fixture(path, channels=3, scaled=True, fill=37):
    path.parent.mkdir(parents=True, exist_ok=True)
    w, h = 7, 5
    sw, sh = (3, 2) if scaled else (w, h)
    flags = int(channels == 1)
    raw = bytes((i * fill + 19) % 256 for i in range(sw * sh * channels))
    values = (w, h, channels, flags, len(raw), sw, sh)
    header = struct.pack('<4s7I', b'H1I2', *values) if scaled else struct.pack('<4s5I', b'H1I1', *values[:5])
    path.write_bytes(header + raw)
    return values


def write_index(path, records, version=2, corrupt=False):
    names = bytearray()
    rows = []
    for name, values in sorted(records.items()):
        values = values if version == 2 else values[:5]
        rows.append(struct.pack('<' + 'I' * (len(values) + 1), len(names), *values))
        names += name.encode() + b'\0'
    body = b''.join(rows) + names
    fnv = 2166136261
    for byte in body:
        fnv = ((fnv ^ byte) * 16777619) & 0xffffffff
    data = struct.pack('<4s6I', b'H1X2' if version == 2 else b'H1X1', version,
                       len(rows), 32 if version == 2 else 24, len(names), len(body), fnv) + body
    if corrupt:
        data = data[:-1] + bytes([data[-1] ^ 1])
    (path / 'image-metadata.h1x').write_bytes(data)


def variant_case(name, records, actual, expected_order, expected_channels=3,
                 version=2, index='valid', expected=True, alpha=1):
    directory = FIX / name
    directory.mkdir(exist_ok=True)
    values = {}
    for suffix, channels in actual.items():
        file = directory / ('image' + suffix)
        values[suffix] = fixture(file, channels, version == 2)
        if suffix.startswith('.bad'):
            raise AssertionError(suffix)
    fallback_info = (7, 5, 3, 0, 18 if version == 2 else 105,
                     3 if version == 2 else 7, 2 if version == 2 else 5)
    table = {'image' + suffix: values.get(suffix, fallback_info) for suffix in records}
    if index == 'absent':
        stale = directory / 'image-metadata.h1x'
        if stale.exists():
            stale.unlink()
    else:
        if not table:
            table = {'unknown.h1i': fallback_info}
        write_index(directory, table, version, index == 'corrupt')
    root(directory)
    result = opened('image', alpha, expected=expected)
    check(image_attempts() == ['image' + suffix for suffix in expected_order])
    if expected:
        check(result[0][2] == expected_channels)
    check(lib.test_balanced_files())
    cases.append(dict(name=name, index=index, index_version=version,
                      attempts=image_attempts(), selected_channels=result[0][2] if result else None))
    return directory, result


for version in (1, 2):
    prefix = f'x{version}-'
    variant_case(prefix + 'rgb-one-open', ['.h1i'], {'.h1i': 3}, ['.h1i'], version=version)
    variant_case(prefix + 'both-alpha-first', ['.h1i', '.h1a'], {'.h1i': 3, '.h1a': 4}, ['.h1a'], 4, version)
    variant_case(prefix + 'alpha-only', ['.h1a'], {'.h1a': 1}, ['.h1a'], 1, version)
    directory, result = variant_case(prefix + 'new-unindexed-alpha', ['.h1i'],
        {'.h1i': 3, '.h1a': 4}, ['.h1i'], version=version)
    info = Info()
    check(lib.h1_image_probe(b'image', 1, C.byref(info)) == 1 and info.channels == result[0][2])
    variant_case(prefix + 'indexed-i-missing', ['.h1i'], {'.h1a': 4}, ['.h1i', '.h1a'], 4, version)
    variant_case(prefix + 'indexed-a-missing', ['.h1a', '.h1i'], {'.h1i': 3}, ['.h1a', '.h1i'], version=version)
    variant_case(prefix + 'only-indexed-a-missing', ['.h1a'], {'.h1i': 3}, ['.h1a', '.h1i'], version=version)
    variant_case(prefix + 'unknown-key', [], {'.h1i': 3, '.h1a': 4}, ['.h1a'], 4, version)
    variant_case(prefix + 'no-index', [], {'.h1i': 3}, ['.h1a', '.h1i'], version=version, index='absent')
    variant_case(prefix + 'bad-index', ['.h1i'], {'.h1i': 3, '.h1a': 4}, ['.h1a'], 4, version, index='corrupt')
    variant_case(prefix + 'all-missing', ['.h1i'], {}, ['.h1i', '.h1a'], version=version, expected=False)
    variant_case(prefix + 'auto-alpha-off', ['.h1a', '.h1i'], {'.h1i': 3, '.h1a': 4},
                 ['.h1i'], version=version, alpha=0)
    # A valid index cannot turn an invalid actual image header into a success.
    for suffix in ('.h1i', '.h1a'):
        name = prefix + ('bad-actual-i' if suffix == '.h1i' else 'bad-actual-a')
        directory, _ = variant_case(name, ['.h1i'] if suffix == '.h1i' else ['.h1a', '.h1i'],
            {'.h1i': 3, '.h1a': 4}, ['.h1i'] if suffix == '.h1i' else ['.h1a'],
            3 if suffix == '.h1i' else 4, version)
        (directory / ('image' + suffix)).write_bytes(b'H1I2broken')
        root(directory)
        result = opened('image')
        check(image_attempts() == ['image' + suffix, 'image' + ('.h1a' if suffix == '.h1i' else '.h1i')])
        check(result[0][2] == (4 if suffix == '.h1i' else 3))
        cases[-1]['after_corrupt_header_attempts'] = image_attempts()
    # Prefix/case/source-extension canonicalization reaches the same indexed key.
    directory, _ = variant_case(prefix + 'canonical', ['.h1i'], {'.h1i': 3}, ['.h1i'], version=version)
    root(directory)
    opened(directory.as_posix() + '/./IMAGE.PNG')
    check(image_attempts() == ['image.h1i'])

# The stock X2 file records each real variant separately; compare every source
# with the former auto-alpha order and compare raw-load behavior as well.
stock = args.resources.resolve()
check(stock.is_dir())
files = sorted(p for p in stock.rglob('*') if p.suffix in ('.h1i', '.h1a'))
check(bool(files))
stems = {}
for file in files:
    relative = file.relative_to(stock).as_posix()
    stems.setdefault(relative[:-4], set()).add(file.suffix)
root(stock)
prime = Info()
check(lib.h1_image_probe(next(iter(stems)).encode(), 1, C.byref(prime)) == 1)
check(lib.test_metadata_state() == 1)
stock_counts = dict(images=len(files), stems=len(stems), indexed_rgb_only=0,
                    both_variants=0, alpha_only=0, auto_comparisons=0,
                    raw_comparisons=0, new_image_opens=0, legacy_image_opens=0)
aggregate = hashlib.sha256()
for stem, variants in stems.items():
    if variants == {'.h1i'}:
        stock_counts['indexed_rgb_only'] += 1
    elif variants == {'.h1a'}:
        stock_counts['alpha_only'] += 1
    else:
        stock_counts['both_variants'] += 1
    for alpha in ((1, 0) if '.h1i' in variants else (1,)):
        lib.test_reset_open_log()
        actual = opened(stem, alpha)
        actual_attempts = image_attempts()
        check(len(actual_attempts) == 1)
        check(actual_attempts[0] == Path(stem).name + ('.h1a' if alpha and '.h1a' in variants else '.h1i'))
        lib.test_reset_open_log()
        baseline = opened(stem, alpha, legacy=True)
        baseline_attempts = image_attempts()
        check(actual == baseline)
        aggregate.update(stem.encode() + bytes([alpha]) + actual[1])
        stock_counts['new_image_opens'] += len(actual_attempts)
        stock_counts['legacy_image_opens'] += len(baseline_attempts)
        stock_counts['auto_comparisons' if alpha else 'raw_comparisons'] += 1

report = dict(all_passed=True, checks=checks, fixture_cases=cases, stock=stock_counts,
              stock_storage_argb_comparison_sha256=aggregate.hexdigest(),
              partial_update_rule='An unindexed new alpha variant does not outrank a valid indexed ordinary image; rebuild the index, matching h1_image_probe.',
              failed_preferred_variant_fallback='Try the alternate variant; do not repeat an already failed identical file open.',
              stock_index_sha256=hashlib.sha256((stock / 'image-metadata.h1x').read_bytes()).hexdigest(),
              reader_sha256=hashlib.sha256((ROOT / 'platform/resources/h1_resources.c').read_bytes()).hexdigest(),
              observer_sha256=hashlib.sha256((ROOT / 'tools/tests/image_variant_probe.c').read_bytes()).hexdigest(),
              command=command, device_access_performed=False, firmware_emulator_used=False)
(OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: v for k, v in report.items() if k not in ('fixture_cases', 'command')}))
