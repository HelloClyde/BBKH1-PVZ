"""Lossless original-shader glow palettes with shared ordered track runs."""
from pathlib import Path
import argparse,struct,json,hashlib,zlib
from pack_actor_poses import compact_runs
from pack_actor_pages import encode,decode
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('output',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args();raw=a.input.read_bytes();at=0;palette={};body=bytearray();streams={};packets={};entries=[];shades=89
 while at<len(raw):
  lo,hi,frame,type_id,bx,by,n=struct.unpack_from('<7I',raw,at);at+=28;assert n&0xc0000000==0xc0000000;n&=0x3fffffff;segments=[];bounds=[480,272,-480,-272]
  for _ in range(n):
   track,drawn,count=struct.unpack_from('<3I',raw,at);at+=12;stream=bytearray();values=[];start=previous=-2;opaque=False;nr=0
   def flush():
    nonlocal nr
    if not values:return
    stream.extend(struct.pack('<hhH',start%480-bx,start//480-by,len(values)|(32768 if opaque else 0)));stream.extend(struct.pack('<'+str(len(values))+'H',*values));nr+=1
   for _ in range(count):
    dest,*v=struct.unpack_from('<'+str(shades+1)+'I',raw,at);at+=(shades+1)*4;v=tuple(v);now=v[0]>>24==255
    if values and (dest!=previous+1 or dest//480!=previous//480 or now!=opaque):flush();values=[]
    if not values:start=dest
    if v not in palette:palette[v]=len(palette)
    values.append(palette[v]);previous=dest;opaque=now;x=dest%480-bx;y=dest//480-by;bounds=[min(bounds[0],x),min(bounds[1],y),max(bounds[2],x+1),max(bounds[3],y+1)]
   flush();stream,compact=compact_runs(stream,nr);key=(nr,compact,stream)
   if key not in streams:streams[key]=len(body);body.extend(struct.pack('<I',len(stream))+stream)
   segments.append(struct.pack('<hHII',track,drawn|(32768 if compact else 0),nr,0x80000000|streams[key]))
  if bounds[2]<bounds[0]:bounds=[0]*4
  packet=struct.pack('<hhHHhhhh',bx,by,n,3,*bounds)+b''.join(segments)
  if packet not in packets:packets[packet]=len(body);body+=packet
  entries.append((lo|(hi<<32),frame,packets[packet],len(packet),type_id))
 assert at==len(raw) and len(palette)<=65536;entries.sort();assert len({e[:2] for e in entries})==len(entries)
 palette_offset=64+len(entries)*24;flash_offset=palette_offset+4;flash=b''.join(struct.pack('<'+str(shades)+'I',*v) for v in palette);data_offset=flash_offset+len(flash)
 index=b''.join(struct.pack('<QIIII',key,frame,data_offset+offset,size,t) for key,frame,offset,size,t in entries);payload=index+bytes(4)+flash+body
 data=struct.pack('<16I',0x50413148,1,480,272,len(entries),64,1,palette_offset,data_offset,64+len(payload),zlib.crc32(payload),flash_offset,len(palette),shades,0x314d4352,2)+payload
 assert len(data)<=12*1024*1024;compressed=encode(data);assert decode(compressed)==data
 # This palette and its track runs exceed the decoded working set together.
 # Raw pages occupy LESS RAM than the compressed bytes + 128 decoded pages +
 # inflater, and every frame can read the precomputed colors without inflation.
 encoded=data;a.output.write_bytes(encoded)
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();root=Path(__file__).resolve().parents[1]
 a.report.write_text(json.dumps(dict(passed=True,runtime_path=a.output.name,bytes=len(encoded),sha256=sha(a.output),decoded_bytes=len(data),decoded_sha256=hashlib.sha256(data).hexdigest(),storage='raw_pages',compressed_comparison_bytes=len(compressed),no_runtime_inflate=True,frames=len(entries),intensities=shades,palette_tuples=len(palette),ordered_track_boundaries=True,lossy=False,roundtrip_byte_exact=True,encoder_sha256=sha(Path(__file__)),exporter_sha256=sha(root/'tools/tests/sunflower_glow_export.cpp')),indent=2)+'\n');print('Sunflower glow',len(encoded),'bytes',len(palette),'tuples')
if __name__=='__main__':main()
