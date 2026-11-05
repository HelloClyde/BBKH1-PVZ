"""Deduplicate 16x8 tiles from original 480x272 lawn composites, losslessly."""
from pathlib import Path
import struct,zlib,json,hashlib,argparse
def pack(folder,output,report):
 tiles={};maps=bytearray();sources={}
 for i in range(6):
  p=folder/f'lawn-{i}.argb';data=p.read_bytes();assert len(data)==480*272*4;sources[p.name]=hashlib.sha256(data).hexdigest()
  rebuilt=bytearray(len(data))
  for y in range(0,272,8):
   for x in range(0,480,16):
    tile=b''.join(data[((y+j)*480+x)*4:((y+j)*480+x+16)*4] for j in range(8))
    if tile not in tiles:tiles[tile]=len(tiles)
    maps+=struct.pack('<H',tiles[tile])
    for j in range(8):rebuilt[((y+j)*480+x)*4:((y+j)*480+x+16)*4]=tile[j*64:(j+1)*64]
  assert rebuilt==data
 body=maps+b''.join(tiles);header=struct.pack('<16I',0x424c3148,1,480,272,16,8,6,len(tiles),64,64+len(maps),64+len(body),zlib.crc32(body),0,0,0,0)
 output.write_bytes(header+body);proof=dict(runtime_path=output.name,bytes=len(header+body),sha256=hashlib.sha256(header+body).hexdigest(),tiles=len(tiles),frames=6,decoded_pixels_compared=6*480*272,source_frames=sources,lossy=False,intermediate_policy='Fixed original endpoint samples; horizontal reveal on the LCD pixel grid eliminates crop-width-dependent UV shimmer',encoder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
 report.write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:proof[k] for k in ('bytes','sha256','tiles','decoded_pixels_compared')}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('output',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args();pack(a.folder,a.output,a.report)
