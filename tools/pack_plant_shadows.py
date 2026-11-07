"""Pack exact LCD shadow spans; installed bytes require no decompression."""
from pathlib import Path
import argparse,hashlib,json
from pack_plant_poses import pack_one
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('output',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 proof=json.loads((a.input.parent/'plant-shadow-export-verification.json').read_text());assert proof['passed']
 data,frames,operations=pack_one(a.input.read_bytes());a.output.write_bytes(data)
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 a.report.write_text(json.dumps(dict(passed=True,runtime_path=a.output.name,bytes=len(data),sha256=sha(a.output),poses=frames,pixel_operations=operations,storage='raw_pages',no_runtime_inflate=True,encoder_sha256=sha(Path(__file__)),span_encoder_sha256=sha(ROOT/'tools/pack_plant_poses.py'),exporter_sha256=sha(ROOT/'tools/tests/plant_shadow_export.cpp'),lossy=False),indent=2)+'\n')
 print(json.dumps(dict(poses=frames,bytes=len(data))))
if __name__=='__main__':main()
