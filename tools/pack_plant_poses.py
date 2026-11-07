"""Pack exact plant shader literals without a shared 16-bit palette limit."""
from pathlib import Path
import argparse,hashlib,json,struct,zlib
from pack_actor_pages import encode,decode
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def pack_one(raw):
 at=0;colors={}
 while at<len(raw):
  segments=struct.unpack_from('<I',raw,at+24)[0]&0x3fffffff;at+=28
  for _ in range(segments):
   count=struct.unpack_from('<I',raw,at+8)[0];at+=12
   for _,literal in struct.iter_unpack('<II',memoryview(raw)[at:at+count*8]):
    if literal not in colors:colors[literal]=len(colors)
   at+=count*8
 direct=len(colors)>65536;width=4 if direct else 2;mode=6 if direct else 2;palette=list(colors) if not direct else None
 at=0;entries=[];packets={};body=bytearray();operations=0
 while at<len(raw):
  lo,hi,frame,type_id,bx,by,segments=struct.unpack_from('<7I',raw,at);at+=28
  assert segments&0x40000000 and not segments&0x80000000;segments&=0x3fffffff
  encoded=[];bounds=[480,272,-480,-272]
  for _ in range(segments):
   track,drawn,count=struct.unpack_from('<3I',raw,at);at+=12;assert at+count*8<=len(raw)
   runs=[];values=[];previous=-2;opaque=False;start=0
   for dest,literal in struct.iter_unpack('<II',memoryview(raw)[at:at+count*8]):
    assert dest<480*272;now=literal>>24==255
    if values and (dest!=previous+1 or dest//480!=previous//480 or now!=opaque):
     runs.append((start%480-bx,start//480-by,opaque,values));values=[]
    if not values:start=dest
    values.append(literal);previous=dest;opaque=now
    x=dest%480-bx;y=dest//480-by;bounds=[min(bounds[0],x),min(bounds[1],y),max(bounds[2],x+1),max(bounds[3],y+1)]
   if values:runs.append((start%480-bx,start//480-by,opaque,values))
   at+=count*8;operations+=count;compact=all(-128<=x<=127 and -128<=y<=127 and len(v)<=127 for x,y,o,v in runs)
   stream=bytearray()
   for x,y,o,v in runs:
    stream+=struct.pack('<bbB',x,y,len(v)|(128 if o else 0)) if compact else struct.pack('<hhH',x,y,len(v)|(32768 if o else 0))
    stream+=struct.pack('<'+str(len(v))+('I' if direct else 'H'),*(v if direct else [colors[n] for n in v]))
   # Independently expand coordinates and every literal before publishing.
   restored=[];pos=0
   for _ in runs:
    if compact:x,y,f=struct.unpack_from('<bbB',stream,pos);pos+=3;n=f&127
    else:x,y,f=struct.unpack_from('<hhH',stream,pos);pos+=6;n=f&32767
    for j in range(n):
     value=struct.unpack_from('<I' if direct else '<H',stream,pos+j*width)[0]
     restored.append(((y+by)*480+x+bx+j,value if direct else palette[value]))
    pos+=n*width
   assert pos==len(stream) and restored==list(struct.iter_unpack('<II',memoryview(raw)[at-count*8:at]))
   track=track if track<0x80000000 else track-0x100000000
   encoded.append(struct.pack('<hHII',track,drawn|(32768 if compact else 0),len(runs),len(stream))+stream)
  if bounds[2]<bounds[0]:bounds=[0]*4
  packet=struct.pack('<hhHHhhhh',bx,by,segments,mode,*bounds)+b''.join(encoded)
  if packet not in packets:packets[packet]=len(body);body+=packet
  entries.append(((hi<<32)|lo,frame,packets[packet],len(packet),type_id))
 assert at==len(raw);entries.sort();assert len({e[:2] for e in entries})==len(entries)
 color_bytes=b'' if direct else struct.pack('<'+str(len(colors))+'I',*colors)
 palette_offset=64+len(entries)*24;offset=palette_offset+len(color_bytes);index=b''.join(struct.pack('<QIIII',key,frame,offset+pos,size,type_id) for key,frame,pos,size,type_id in entries)
 payload=index+color_bytes+body;header=struct.pack('<16I',0x50413148,1,480,272,len(entries),64,0 if direct else len(colors),palette_offset,offset,64+len(payload),zlib.crc32(payload),0,0,0,0x314d4352,mode)
 assert len(entries)<=8192 and len(header+payload)<=12*1024*1024
 return header+payload,len(entries),operations
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('output',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 assert not a.output.exists();a.output.parent.mkdir(parents=True,exist_ok=True);records=[];blobs={}
 report=json.loads((a.input.parent/'plant-compiler-verification.json').read_text());assert report['passed']
 keys={r['type']:{int(k,16):f for k,f in r['state_keys']} for r in report['types']}
 for source in sorted(a.input.glob('*.raw'),key=lambda p:int(p.stem)):
  type_id=int(source.stem);raw=source.read_bytes();at=0;banks=[bytearray() for _ in range(4)]
  while at<len(raw):
   begin=at;lo,hi,frame,t,bx,by,segments=struct.unpack_from('<7I',raw,at);at+=28;assert t==type_id
   for _ in range(segments&0x3fffffff):
    _,_,n=struct.unpack_from('<3I',raw,at);at+=12+n*8
   assert at<=len(raw);banks[keys[type_id][lo|(hi<<32)]]+=raw[begin:at]
  for filter,bank in enumerate(banks):
   assert bank;data,frames,operations=pack_one(bank);blob=encode(data);assert decode(blob)==data
   slot=type_id*4+filter;blobs[slot]=blob
   records.append(dict(type=type_id,filter=filter,slot_index=slot,bytes=len(blob),decoded_bytes=len(data),frames=frames,pixel_operations=operations,sha256=hashlib.sha256(blob).hexdigest(),decoded_sha256=hashlib.sha256(data).hexdigest(),source_raw_sha256=sha(source),decoder_roundtrip_byte_exact=True))
   print('Packed plant',type_id,'filter',filter,'frames',frames,'bytes',len(blob),flush=True)
 count=max(blobs)+1;table=bytearray(count*12);payload=bytearray();offset=32+len(table)
 for r in records:
  blob=blobs[r['slot_index']];struct.pack_into('<3I',table,r['slot_index']*12,offset,len(blob),zlib.crc32(blob));r['offset']=offset;payload+=blob;offset+=len(blob)
 output=struct.pack('<8I',0x4c503148,1,count,len(table),offset,zlib.crc32(table),0,0)+table+payload;a.output.write_bytes(output)
 for r in records:assert hashlib.sha256(output[r['offset']:r['offset']+r['bytes']]).hexdigest()==r['sha256']
 a.report.write_text(json.dumps(dict(passed=True,runtime_path='plant-poses.h1pp',sha256=sha(a.output),per_track_visibility_and_attachment_boundaries=True,direct_literals=True,compiler_sha256=sha(ROOT/'tools/tests/plant_pose_export.cpp'),encoder_sha256=sha(Path(__file__)),page_encoder_sha256=sha(ROOT/'tools/pack_actor_pages.py'),files=records,bytes=len(output)),indent=2)+'\n')
 print('Plant packs',len(records),'bytes',len(output))
if __name__=='__main__':main()
