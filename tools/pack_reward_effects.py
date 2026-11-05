"""Pack LCD reward passes; additive precision matches the RGB565 display."""
from pathlib import Path
import argparse,array,collections,hashlib,json,struct,zlib
from build_paths import ROOT
U32=lambda *n:struct.pack('<'+'I'*len(n),*n)
def frames(path):
 with path.open('rb') as f:
  magic,w,h,kinds=struct.unpack('<4I',f.read(16));assert (magic,w,h,kinds)==(0x57524548,960,544,2)
  for _ in range(kinds):
   kind,count=struct.unpack('<2I',f.read(8))
   for frame in range(count):
    age,np=struct.unpack('<2I',f.read(8));passes=[]
    for _ in range(np):
     mode,n=struct.unpack('<2I',f.read(8));assert n==w*h and mode<2
     a=array.array('I');a.frombytes(f.read(n*4));assert len(a)==n;passes.append((mode,a))
    yield kind,age,passes
  assert not f.read(1)
def encode(a,ids):
 a=array.array('B',(ids[v] for v in a))
 rows=[]
 for y in range(544):
  row=a[y*960:(y+1)*960];out=bytearray();tokens=0;x=0
  while x<960:
   end=x+1
   while end<960 and row[end]==row[x]:end+=1
   if end-x>=3:
    out+=struct.pack('<H',0x8000|(end-x));out.append(row[x]);x=end
   else:
    start=x;x=end
    while x<960:
     end=x+1
     while end<960 and row[end]==row[x]:end+=1
     if end-x>=3:break
     x=end
    out+=struct.pack('<H',x-start)
    out+=row[start:x].tobytes()
   tokens+=1
  rows.append(struct.pack('<H',tokens)+out)
 return struct.pack('<544H',*(len(row) for row in rows))+b''.join(rows)
def index(n):
 return bytes((n,)) if n<128 else bytes(((n&127)|128,n>>7))
def lz4(data):
 data=bytes(data);out=bytearray();seen={};anchor=i=0
 def length(n):
  while n>=255:out.append(255);n-=255
  out.append(n)
 while i+12<len(data):
  key=data[i:i+4];positions=seen.get(key,());previous=None;match=3
  for candidate in reversed(positions):
   if i-candidate>65535:break
   if data[candidate+match]!=data[i+match]:continue
   n=4
   while i+n<len(data)-5 and data[candidate+n]==data[i+n]:n+=1
   if n>match:previous=candidate;match=n
   if i+match>=len(data)-5:break
  seen.setdefault(key,[]).append(i)
  if len(seen[key])>16:del seen[key][0]
  if previous is None:i+=1;continue
  literal=i-anchor;token=min(literal,15)<<4| min(match-4,15);out.append(token)
  if literal>=15:length(literal-15)
  out+=data[anchor:i];out+=struct.pack('<H',i-previous)
  if match>=19:length(match-19)
  end=i+match
  for j in range(i+1,end):
   if j+4<len(data):
    key=data[j:j+4];seen.setdefault(key,[]).append(j)
    if len(seen[key])>16:del seen[key][0]
  i=anchor=end
 literal=len(data)-anchor;out.append(min(literal,15)<<4)
 if literal>=15:length(literal-15)
 out+=data[anchor:];return bytes(out)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,required=True);p.add_argument('--bounds',type=Path,required=True);a=p.parse_args()
 bounds=json.loads(a.bounds.read_text());assert len(bounds)==181
 colors=[collections.Counter(),collections.Counter()];count=0
 for kind,age,passes in frames(a.input):
  for mode,pixels in passes:colors[mode].update(pixels)
  count+=1
 def quant(v):
  return min(255,((v&255)+8)//16*16)|(min(255,(((v>>8)&255)+8)//16*16)<<8)|(min(255,(((v>>16)&255)+8)//16*16)<<16)
 frequencies=colors[0].copy()
 for v,n in colors[1].items():frequencies[quant(v)]+=n
 palette=[0]+[v for v,n in frequencies.most_common() if v];assert len(palette)<16384,len(palette)
 indices={v:i for i,v in enumerate(palette)};ids=[{v:indices[v] for v in colors[0]},{v:indices[quant(v)] for v in colors[1]}]
 print('Palette',len(palette),'frames',count,flush=True)
 blobs=bytearray();entries=[];shared={};unique=0;frame_bounds=[];frame_capacity=[0,0]
 for kind,age,passes in frames(a.input):
  box=bounds[(age*3+5)//10] if kind==0 else [0,480,0,272];frame_bounds.append(box)
  left,right=max(0,480-box[1]),min(960,960-box[0]);top,bottom=max(0,272-box[3]),min(544,544-box[2])
  for mode,pixels in passes:
   if kind==0:
    zero=array.array('I',[0])*960
    for y in range(544):
     if y<top or y>=bottom:pixels[y*960:(y+1)*960]=zero
     else:
      if left:pixels[y*960:y*960+left]=zero[:left]
      if right<960:pixels[y*960+right:(y+1)*960]=zero[:960-right]
  used={0}
  for mode,pixels in passes:used.update(ids[mode][v] for v in set(pixels))
  local=sorted(used);assert len(local)<=256,len(local);local_ids={v:i for i,v in enumerate(local)}
  maps=[{v:local_ids[i] for v,i in table.items() if i in local_ids} for table in ids]
  record=bytearray(U32(len(passes),len(local)))+struct.pack('<'+'H'*len(local),*local)
  for mode,pixels in passes:
   rowdata=encode(pixels,maps[mode]);record+=U32(mode,len(rowdata))+rowdata
  digest=hashlib.sha256(record).digest()
  frame_capacity[kind]=max(frame_capacity[kind],len(record))
  if digest not in shared:
   encoded=U32(len(record))+lz4(record);shared[digest]=(len(blobs),len(encoded));blobs+=encoded;unique+=1
  offset,size=shared[digest];entries.append((kind,age,offset,size))
  if len(entries)%30==0:print('Packed',len(entries),'bytes',len(blobs),flush=True)
 table=bytearray();bounds_offset=64+len(palette)*4+len(entries)*16;data_offset=bounds_offset+len(entries)*8
 for kind,age,offset,size in entries:table+=U32(kind,age,data_offset+offset,size)
 body=U32(*palette)+table+b''.join(struct.pack('<4h',*box) for box in frame_bounds)+blobs
 header=[0x45523148,4,960,544,len(palette),len(entries),64,64+len(palette)*4,data_offset,64+len(body),zlib.crc32(body),30,480,272,1,bounds_offset]
 decoded=U32(*header)+body
 output=ROOT/'private/resources-h1-wide/reward-effects.h1re';output.write_bytes(U32(0x5a523148,1,len(decoded),zlib.crc32(decoded))+zlib.compress(decoded,9))
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 report=dict(version=1,bytes=output.stat().st_size,sha256=sha(output),canvas=[960,544],viewport=[480,272],frames=len(entries),unique_frames=unique,palette_colors=len(palette),sample_rate=30,raw_bytes=a.input.stat().st_size,normal_passes_lossless=True,additive_max_RGB8_error=[8,8,8],runtime_path=output.name,encoder_sha256=sha(Path(__file__)),renderer_exporter_sha256=sha(ROOT/'tools/tests/reward_effect_export.cpp'),scope='Precomputed award light including original coin scale curve for the 600-tick level-end sequence, and seeded starburst through 100 ticks. Centers translate at LCD pixel resolution. Normal alpha and /256 rounding retained; summed additive colors quantized with a maximum RGB8 error of 8 per channel.')
 report.update(version=4,decoded_bytes=len(decoded),decoded_sha256=hashlib.sha256(decoded).hexdigest(),frame_scratch_bytes=frame_capacity,compression='Outer zlib expanded once before play. Prebaked LZ4-HC row streams with local byte palettes expand into two preallocated reusable frame buffers; no playback IO, allocation, particle simulation or texture sampling.',viewport_bounds_sha256=sha(a.bounds),bounds_exporter_sha256=sha(ROOT/'tools/tests/reward_bounds_export.cpp'),cropping='Only texels outside every reachable viewport of the original 350-tick collection curve are removed; actual LCD center is checked against the stored bounds.')
 (ROOT/'provenance/offline-reward-effects.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
if __name__=='__main__':main()
