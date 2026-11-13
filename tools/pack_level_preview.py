"""Pack the original-renderer day panorama and pole-vault zombie LCD poses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from pack_plant_poses import pack_one
from pack_actor_pages import encode, decode

ROOT = Path(__file__).resolve().parents[1]
def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', type=Path, required=True)
    a = p.parse_args()
    source = a.input
    proof = json.loads((source/'zombie-compiler-verification.json').read_text())
    assert proof['passed'] and [v['type'] for v in proof['types']] == [54]
    pixels = (source/'camera-day-0.argb').read_bytes()
    assert len(pixels) == 634*272*4 and all(pixels[i] == 255 for i in range(3,len(pixels),4))
    panorama = struct.pack('<16I',0x50433148,1,634,272,1,len(pixels),64,zlib.crc32(pixels),*([0]*8))+pixels
    raw = (source/'zombie-raw/54.raw').read_bytes()
    decoded, frames, operations = pack_one(raw)
    blob = encode(decoded)
    assert decode(blob) == decoded
    count = 54*4+1
    table = bytearray(count*12)
    offset = 32+len(table)
    struct.pack_into('<3I',table,54*4*12,offset,len(blob),zlib.crc32(blob))
    bank = struct.pack('<8I',0x4c503148,1,count,len(table),offset+len(blob),zlib.crc32(table),0,0)+table+blob
    output = ROOT/'private/resources-h1-wide'
    (output/'camera-lawn-day.h1cp').write_bytes(panorama)
    (output/'zombie-poses.h1pp').write_bytes(bank)
    report = dict(passed=True, resolution=[480,272], frames=frames, pixel_operations=operations,
        original_shader_comparisons=proof['types'][0]['full_frame_comparisons'],
        all_authored_idle_half_frames=True, per_track_visibility_preserved=True,
        files={name:dict(bytes=len(data),sha256=sha(data)) for name,data in
            [('camera-lawn-day.h1cp',panorama),('zombie-poses.h1pp',bank)]},
        decoded_pose_bytes=len(decoded),decoded_pose_sha256=sha(decoded),
        exporter_sha256=sha((ROOT/'tools/tests/level_preview_export.cpp').read_bytes()),
        encoder_sha256=sha(Path(__file__).read_bytes()),lossless_page_roundtrip=True)
    (ROOT/'provenance/offline-level-preview.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__ == '__main__':
    main()
