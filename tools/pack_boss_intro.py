"""Pack one measured slow static Boss-intro view, without animation frames."""
from pathlib import Path
import argparse,hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);a=p.parse_args()
 proof=json.loads((a.input/'boss-intro-export-verification.json').read_text());assert proof['passed']
 raw=(a.input/'boss-intro.argb').read_bytes();assert len(raw)==480*272*4 and all(raw[i]==255 for i in range(3,len(raw),4))
 blob=struct.pack('<16I',0x424c3148,2,480,272,16,8,1,480*272,64,64,64+len(raw),zlib.crc32(raw),0,0,0,0)+raw
 path=ROOT/'private/resources-h1-wide/boss-intro.h1lb';path.write_bytes(blob)
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 report=dict(passed=True,runtime_path=path.name,bytes=len(blob),sha256=sha(path),static_background_only=True,view_x=220,dave_animation_baked=False,exporter_sha256=sha(ROOT/'tools/tests/boss_intro_background_probe.cpp'),encoder_sha256=sha(Path(__file__)),original_renderer_opaque_checks=proof['original_renderer_frame_comparisons'])
 (ROOT/'provenance/offline-boss-intro.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
