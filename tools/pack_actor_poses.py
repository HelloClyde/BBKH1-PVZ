"""Pack original-shader, LCD-resolution actor operations without lossy conversion.

Input is emitted by actor_pose_export.cpp linked to the identified host build.
All palette expansion, run construction and packet validation happens offline.
"""
from pathlib import Path
import argparse,struct,json,hashlib,zlib

def compact_runs(stream,nruns):
 original=bytes(stream);out=bytearray();at=0
 for _ in range(nruns):
  x,y,flags=struct.unpack_from('<hhH',original,at);n=flags&32767;at+=6
  if not(-128<=x<=127 and -128<=y<=127 and n<=127):return original,False
  out.extend(struct.pack('<bbB',x,y,n|(128 if flags&32768 else 0)));out.extend(original[at:at+n*2]);at+=n*2
 assert at==len(original)
 # Independently expand every fixed-width field; all pixel indices are copied.
 restored=bytearray();at=0
 for _ in range(nruns):
  x,y,flags=struct.unpack_from('<bbB',out,at);n=flags&127;at+=3
  restored.extend(struct.pack('<hhH',x,y,n|(32768 if flags&128 else 0)));restored.extend(out[at:at+n*2]);at+=n*2
 assert restored==original
 return bytes(out),True
def pack(raw:Path,output:Path,report_path:Path|None=None):
 data=raw.read_bytes();pos=0;entries=[];palette={};flash_palette={};packets={};shared_streams={};body=bytearray();counts={};operations=0;runs=0
 def words(n):
  nonlocal pos
  value=struct.unpack_from('<'+'I'*n,data,pos);pos+=4*n;return value
 while pos<len(data):
  lo,hi,frame,actor,bx,by,nsegments=words(7);flash=bool(nsegments&0x80000000);nsegments&=0x7fffffff;segments=[];xs=[];ys=[]
  for _ in range(nsegments):
   track,drawn,n=words(3);stream=bytearray();run=[];previous=-2;opaque=False
   def flush():
    nonlocal runs
    if not run:return
    x,y=run[0][0]%480,run[0][0]//480
    stream.extend(struct.pack('<hhH',x-bx,y-by,len(run)|(0x8000 if opaque else 0)))
    for dest,literal in run:
     colors=flash_palette if flash else palette
     if literal not in colors:colors[literal]=len(colors)
     stream.extend(struct.pack('<H',colors[literal]));xs.append(dest%480-bx);ys.append(dest//480-by)
    runs+=1
   nr=0
   for _ in range(n):
    values=words(27 if flash else 2);dest=values[0];literal=values[1:] if flash else values[1];now=(literal[0] if flash else literal)>>24==255
    if run and (dest!=previous+1 or dest//480!=previous//480 or opaque!=now):flush();run=[];nr+=1
    run.append((dest,literal));previous=dest;opaque=now
   if run:flush();nr+=1
   signed_track=track if track<0x80000000 else track-0x100000000
   stream,compact=compact_runs(stream,nr);drawn|=32768 if compact else 0
   if flash:
    key=(nr,compact,bytes(stream))
    if key not in shared_streams:shared_streams[key]=len(body);body.extend(struct.pack('<I',len(stream))+stream)
    segments.append(struct.pack('<hHII',signed_track,drawn,nr,0x80000000|shared_streams[key]))
   else:segments.append(struct.pack('<hHII',signed_track,drawn,nr,len(stream))+stream)
   operations+=n
  bounds=(min(xs),min(ys),max(xs)+1,max(ys)+1) if xs else (0,0,0,0)
  packet=struct.pack('<hhHHhhhh',bx,by,nsegments,int(flash),*bounds)+b''.join(segments)
  if packet not in packets:packets[packet]=len(body);body.extend(packet)
  entries.append(((hi<<32)|lo,frame,packets[packet],len(packet),actor));counts[actor]=counts.get(actor,0)+1
 assert len(palette)<=65536 and len(flash_palette)<=65536
 entries.sort();assert len({(e[0],e[1]) for e in entries})==len(entries)
 index=bytearray();flash_bytes=b''.join(struct.pack('<26I',*value) for value in flash_palette);flash_offset=64+24*len(entries)+4*len(palette);body_offset=flash_offset+len(flash_bytes)
 for key,frame,offset,size,actor in entries:index.extend(struct.pack('<QIIII',key,frame,body_offset+offset,size,actor))
 colors=b''.join(struct.pack('<I',value) for value in palette)
 payload=index+colors+flash_bytes+body;crc=zlib.crc32(payload)
 header=struct.pack('<16I',0x50413148,1,480,272,len(entries),64,len(palette),64+len(index),body_offset,64+len(payload),crc,flash_offset if flash_palette else 0,len(flash_palette),26 if flash_palette else 0,0x314d4352,0)
 output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(header+payload)
 report=dict(version=1,dimensions=[480,272],entries=len(entries),actors=counts,palette_colors=len(palette),flash_palette_tuples=len(flash_palette),flash_intensities=26 if flash_palette else 0,shared_flash_streams=len(shared_streams),unique_packets=len(packets),pixel_operations=operations,runs=runs,bytes=output.stat().st_size,crc32=f'{crc:08x}',sha256=hashlib.sha256(output.read_bytes()).hexdigest(),raw_sha256=hashlib.sha256(data).hexdigest(),lossy=False)
 if report_path:report_path.write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2));return report
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('raw',type=Path);p.add_argument('output',type=Path);p.add_argument('--report',type=Path);a=p.parse_args();pack(a.raw,a.output,a.report)
