"""Pack pre-rendered LCD-density panoramas with independent frame CRCs."""
from pathlib import Path
import argparse,hashlib,json,struct,zlib
from build_paths import ROOT

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,required=True);a=p.parse_args()
 frames=[(a.input/f'camera-lawn-{i}.argb').read_bytes() for i in range(3)];size=634*272*4;assert all(len(f)==size for f in frames)
 assert all(f[j]==255 for f in frames for j in range(3,len(f),4))
 h=[0x50433148,1,634,272,3,size,64,*[zlib.crc32(f) for f in frames]]+[0]*6
 data=struct.pack('<16I',*h)+b''.join(frames);output=ROOT/'private/resources-h1-wide/camera-lawns.h1cp';output.write_bytes(data)
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 report=dict(version=1,dimensions=[634,272],viewport=[480,272],frames=3,bytes=len(data),sha256=sha(output),frame_sha256=[hashlib.sha256(f).hexdigest() for f in frames],crc32=[f'{zlib.crc32(f):08x}' for f in frames],runtime_path='camera-lawns.h1cp',encoder_sha256=sha(Path(__file__)),renderer_exporter_sha256=sha(ROOT/'tools/tests/camera_lawn_export.cpp'),scope='Opaque original background/sod composites at LCD density. Camera translation is rounded to an LCD pixel; authored camera and actor update clocks are unchanged. Current-level panorama is read before the intro and freed when sod starts.')
 (ROOT/'provenance/offline-camera-lawns.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
