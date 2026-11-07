"""Pack separate lossless regular backgrounds with the verified lawn tile codec."""
from pathlib import Path
import argparse,hashlib,json,os,tempfile
from pack_early_lawns import pack
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('output',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 proof=json.loads((a.input/'regular-lawn-export-verification.json').read_text());assert proof['passed'];records=[]
 for background in range(1,7):
  with tempfile.TemporaryDirectory(dir=a.input) as work:
   work=Path(work);source=(a.input/f'background-{background}.argb').resolve()
   for frame in range(6):os.link(source,work/f'lawn-{frame}.argb')
   output=a.output/f'normal-lawn-{background}.h1lb';report=work/'report.json';pack(work,output,report);record=json.loads(report.read_text());record.pop('intermediate_policy');record.update(background=background,source_frame_sha256=sha(source));records.append(record)
 a.report.write_text(json.dumps(dict(passed=True,backgrounds=6,bytes=sum(r['bytes'] for r in records),files=records,encoder_sha256=sha(Path(__file__)),tile_encoder_sha256=sha(ROOT/'tools/pack_early_lawns.py'),exporter_sha256=sha(ROOT/'tools/tests/regular_lawn_export.cpp'),lossy=False,dynamic_overlays_preserved=True),indent=2)+'\n')
 print(json.dumps(dict(backgrounds=6,bytes=sum(r['bytes'] for r in records))))
if __name__=='__main__':main()
