"""Compare actual replay pixels to independent source-to-RGB565 conversion."""
from pathlib import Path
import argparse,array,hashlib,json
ROOT=Path(__file__).resolve().parents[1]
def verify(source,replay):
 size=480*272;count=348;assert source.stat().st_size==count*size*4
 table=array.array('I',(0xff000000|((((c>>11)<<3)|((c>>11)>>2))<<16)|(((((c>>5)&63)<<2)|(((c>>5)&63)>>4))<<8)|(((c&31)<<3)|((c&31)>>2)) for c in range(65536)))
 expected=hashlib.sha256();actual=hashlib.sha256();original=hashlib.sha256()
 with source.open('rb') as reference,(replay/'run/first-intro.argb').open('rb') as stream:
  for frame in range(count):
   raw=reference.read(size*4);original.update(raw);words=array.array('I');words.frombytes(raw)
   target=array.array('I',(table[((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31)] for c in words)).tobytes()
   shown=stream.read(size*4);assert shown==target,frame;expected.update(target);actual.update(shown)
  assert not stream.read(1)
 proof=json.loads((replay/'run/first-intro-export.json').read_text())
 proof.update(all_frames_match_independent_rgb565=True,source_argb_sha256=original.hexdigest(),decoded_bgra_sha256=actual.hexdigest(),expected_bgra_sha256=expected.hexdigest(),test_source_sha256=hashlib.sha256((ROOT/'tools/tests/first_intro_playback.cpp').read_bytes()).hexdigest())
 (replay/'verification.json').write_text(json.dumps(proof,indent=2)+'\n');return proof
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,default=ROOT/'build/first-intro-bake/run/first-intro.argb');p.add_argument('--replay',type=Path,default=ROOT/'build/first-intro-frames-playback');a=p.parse_args();print(json.dumps(verify(a.source,a.replay)))
