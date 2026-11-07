"""Share exact plant geometry across four offline color variants."""
from pathlib import Path
import argparse,hashlib,json,struct,zlib
from pack_actor_pages import encode,decode
from pack_plant_poses import pack_one
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()

def parse(data,filters):
 at=0;groups=[[] for _ in range(4)]
 while at<len(data):
  begin=at;header=struct.unpack_from('<7I',data,at);at+=28;segments=[]
  for _ in range(header[6]&0x3fffffff):
   track,drawn,count=struct.unpack_from('<3I',data,at);at+=12
   values=memoryview(data)[at:at+count*8].cast('I');at+=count*8
   segments.append((track,drawn,count,values))
  groups[filters[header[0]|(header[1]<<32)]].append((header,segments,memoryview(data)[begin:at]))
 assert at==len(data) and len({len(g) for g in groups})==1
 return groups

def merge(groups):
 tuples={};surrogates=bytearray();operations=0
 for entries in zip(*groups):
  assert all(e[0][2:]==entries[0][0][2:] for e in entries)
  payload=bytearray()
  for segments in zip(*(e[1] for e in entries)):
   track,drawn,count,first=segments[0]
   assert all(s[:3]==segments[0][:3] and s[3][::2]==first[::2] for s in segments)
   payload+=struct.pack('<3I',track,drawn,count)
   for i in range(count):
    colors=tuple(s[3][i*2+1] for s in segments)
    assert len({v>>24 for v in colors})==1
    index=tuples.setdefault(colors,len(tuples))
    if index>=65536:return None
    payload+=struct.pack('<2I',first[i*2],(colors[0]&0xff000000)|index)
   operations+=count
  for entry in entries:surrogates+=struct.pack('<7I',*entry[0])+payload
 original,_,_=pack_one(surrogates);h=list(struct.unpack_from('<16I',original));assert h[15]==2
 colors=list(tuples);palette=[colors[struct.unpack_from('<I',original,h[7]+i*4)[0]&65535] for i in range(h[6])]
 flash=b''.join(struct.pack('<4I',*p) for p in palette)
 index=bytearray(original[64:h[7]]);body=bytearray(original[h[8]:]);new_offset=h[7]+4+len(flash);shift=new_offset-h[8];packets=set()
 for i in range(h[4]):
  offset=struct.unpack_from('<I',index,i*24+12)[0];packets.add(offset-h[8]);struct.pack_into('<I',index,i*24+12,offset+shift)
 for offset in packets:assert struct.unpack_from('<H',body,offset+6)[0]==2;struct.pack_into('<H',body,offset+6,10)
 h[11]=h[7]+4;h[12]=len(palette);h[13]=4;h[6]=1;h[8]=new_offset;h[15]=10
 payload=index+bytes(4)+flash+body;h[9]=64+len(payload);h[10]=zlib.crc32(payload)
 assert h[4]<=8192 and h[9]<=12*1024*1024
 return struct.pack('<16I',*h)+payload,h[4],operations,len(palette)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('output',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
 assert not a.output.exists()
 proof=json.loads((a.input.parent/'plant-compiler-verification.json').read_text());assert proof['passed']
 states={r['type']:{int(k,16):v for k,v in r['state_keys']} for r in proof['types']}
 blobs={};records=[];merged=0
 for source in sorted(a.input.glob('*.raw'),key=lambda p:int(p.stem)):
  type_id=int(source.stem);data=source.read_bytes();groups=parse(data,states[type_id]);result=merge(groups)
  if result:
   raw,frames,operations,colors=result;blob=encode(raw);assert decode(blob)==raw;blobs[type_id,0]=blob;merged+=1
   for f in range(4):records.append(dict(type=type_id,filter=f,slot_index=type_id*4+f,shared_variants=True,bytes=len(blob),decoded_bytes=len(raw),frames=frames,pixel_operations=operations,palette_tuples=colors,sha256=sha(blob),decoded_sha256=sha(raw),source_raw_sha256=sha(data),decoder_roundtrip_byte_exact=True))
  else:
   for f,group in enumerate(groups):
    raw,frames,operations=pack_one(b''.join(e[2] for e in group));blob=encode(raw);assert decode(blob)==raw;blobs[type_id,f]=blob
    records.append(dict(type=type_id,filter=f,slot_index=type_id*4+f,shared_variants=False,bytes=len(blob),decoded_bytes=len(raw),frames=frames,pixel_operations=operations,sha256=sha(blob),decoded_sha256=sha(raw),source_raw_sha256=sha(data),decoder_roundtrip_byte_exact=True))
  print('Packed variants',type_id,'shared',bool(result),flush=True)
 count=max(r['slot_index'] for r in records)+1;table=bytearray(count*12);payload=bytearray();offsets={}
 for group,blob in sorted(blobs.items()):offsets[group]=32+len(table)+len(payload);payload+=blob
 for r in records:
  key=r['type'],0 if r['shared_variants'] else r['filter'];offset=offsets[key];blob=blobs[key];r['offset']=offset;struct.pack_into('<3I',table,r['slot_index']*12,offset,len(blob),zlib.crc32(blob))
 output=struct.pack('<8I',0x4c503148,1,count,len(table),32+len(table)+len(payload),zlib.crc32(table),0,0)+table+payload;a.output.write_bytes(output)
 report=dict(passed=True,runtime_path='plant-poses.h1pp',sha256=sha(output),bytes=len(output),per_track_visibility_and_attachment_boundaries=True,direct_literals=True,shared_color_variant_types=merged,unique_banks=len(blobs),compiler_sha256=sha((ROOT/'tools/tests/plant_pose_export.cpp').read_bytes()),encoder_sha256=sha(Path(__file__).read_bytes()),legacy_encoder_sha256=sha((ROOT/'tools/pack_plant_poses.py').read_bytes()),page_encoder_sha256=sha((ROOT/'tools/pack_actor_pages.py').read_bytes()),files=records)
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(bytes=len(output),shared_types=merged,unique_banks=len(blobs))))
if __name__=='__main__':main()
