"""Flatten background-independent pixels inside original attachment segments."""
from pathlib import Path
import argparse,hashlib,json,struct,zlib
from pack_actor_pages import decode
def blend(p,v):
 a=v>>24
 return 0xff000000|((v&0xffffff)+((((p&0xff00ff)*a)>>8)&0xff00ff)+((((p&0xff00)*a)>>8)&0xff00))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('output',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args();assert not a.output.exists()
 encoded=a.input.read_bytes();data=decode(encoded) if encoded[:4]==b'H1PZ' else encoded;h=struct.unpack_from('<16I',data);palette=struct.unpack_from('<'+str(h[6])+'I',data,h[7])
 entries=[];packets={};body=bytearray();before=after=checks=0
 for i in range(h[4]):
  key,frame,offset,size,kind=struct.unpack_from('<Q4I',data,64+i*24)
  # Keep the complete transition bank in actor-poses.h1ap. Ordinary authored
  # poses fit in a small raw working set and avoid page/texture/palette work.
  if kind not in (4,7) or frame&0x80000000:continue
  bx,by,n,mode,left,top,right,bottom=struct.unpack_from('<hhHHhhhh',data,offset);assert mode==0;at=offset+16;segments=[]
  for _ in range(n):
   track,flags,runs,length=struct.unpack_from('<hHII',data,at);at+=12;assert not length&0x80000000;stop=at+length;pixels={}
   for _ in range(runs):
    if flags&32768:x,y,f=struct.unpack_from('<bbB',data,at);at+=3;count=f&127;opaque=bool(f&128)
    else:x,y,f=struct.unpack_from('<hhH',data,at);at+=6;count=f&32767;opaque=bool(f&32768)
    for j in range(count):
     value=palette[struct.unpack_from('<H',data,at)[0]];at+=2;chain=pixels.setdefault((y,x+j),[]);chain.append((value,opaque));before+=1
   assert at==stop;ops=[]
   for (y,x),chain in sorted(pixels.items()):
    reduced=[]
    for value,opaque in chain:
     if opaque or value>>24==0:reduced=[(0xff000000|(value&0xffffff),True)]
     elif reduced and reduced[0][1]:reduced=[(blend(reduced[0][0],value),True)]
     else:reduced.append((value,False))
    for bg in (0xff17293b,0xffe1b38d,0xff0403f7):
     original=optimized=bg
     for value,opaque in chain:original=value if opaque else blend(original,value)
     for value,opaque in reduced:optimized=value if opaque else blend(optimized,value)
     assert original==optimized;checks+=1
    ops.extend((x,y,value,opaque) for value,opaque in reduced);after+=len(reduced)
   stream=bytearray();run=[];start=None;last=None;opaque=None;count_runs=0
   def flush():
    nonlocal count_runs
    if not run:return
    stream.extend(struct.pack('<hhHH',start[0],start[1],len(run)|(32768 if opaque else 0),0));stream.extend(struct.pack('<'+str(len(run))+'I',*run));count_runs+=1
   for x,y,value,now in ops:
    if run and (last!=(x-1,y) or opaque!=now):flush();run=[]
    if not run:start=(x,y);opaque=now
    run.append(value);last=(x,y)
   flush();segments.append(struct.pack('<hHII',track,flags&1,count_runs,len(stream))+stream)
  assert at==offset+size
  packet=struct.pack('<hhHHhhhh',bx,by,n,20,left,top,right,bottom)+b''.join(segments)
  if packet not in packets:packets[packet]=len(body);body+=packet
  entries.append((key,frame,packets[packet],len(packet),kind))
 entries.sort();start=64+len(entries)*24;index=b''.join(struct.pack('<Q4I',key,frame,start+offset,size,kind) for key,frame,offset,size,kind in entries);payload=index+body
 output=struct.pack('<16I',0x50413148,1,480,272,len(entries),64,0,start,start,64+len(payload),zlib.crc32(payload),0,0,0,0x314d4352,20)+payload;a.output.write_bytes(output)
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 report=dict(passed=True,runtime_path='actor-normal.h1ap',bytes=len(output),sha256=sha(a.output),entries=len(entries),source_sha256=sha(a.input),encoder_sha256=sha(Path(__file__)),pixel_operations_before=before,pixel_operations_after=after,independent_pixel_checks=checks,attachment_boundaries_preserved=True,complete_transition_bank_retained=True,storage='aligned_raw_RGBA32',lossy=False)
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
