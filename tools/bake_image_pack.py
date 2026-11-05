"""Bundle byte-identical common H1 images; runtime shares one backing stream."""
from pathlib import Path
import hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1]
def fnv(data):
 h=2166136261
 for b in data:h=((h^b)*16777619)&0xffffffff
 return h
def encode(root,paths):
 paths=sorted(paths);strings=bytearray();records=[];payload=bytearray();size=len(paths)*16+sum(len(p.encode('ascii'))+1 for p in paths)
 for name in paths:
  assert name==name.lower() and not name.startswith('/') and '..' not in name and ':' not in name
  data=(root/name).read_bytes();assert data[:4] in (b'H1I1',b'H1I2')
  header=24 if data[:4]==b'H1I1' else 32
  assert header+struct.unpack_from('<I',data,20)[0]==len(data)
  records.append(struct.pack('<4I',len(strings),32+size+len(payload),len(data),zlib.crc32(data)))
  strings+=name.encode('ascii')+b'\0';payload+=data
 table=b''.join(records)+strings;assert len(table)==size
 return struct.pack('<4s7I',b'H1PK',1,len(paths),size,32+size+len(payload),fnv(table),0,0)+table+payload
def main():
 root=ROOT/'private/resources-h1-wide';paths=json.loads((ROOT/'provenance/common-image-paths.json').read_text())['files']
 data=encode(root,paths);target=root/'images-common.h1pk';target.write_bytes(data)
 info=dict(runtime_path=target.relative_to(root).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),count=len(paths),encoder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_sha256={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},scope='Exact original H1I1/H1I2 bytes, no resize or decoding during packing; startup and early-level common images')
 p=ROOT/'provenance/runtime-performance.json';j=json.loads(p.read_text());j['image_pack']=info;p.write_text(json.dumps(j,indent=2)+'\n',encoding='utf8')
 print(json.dumps({k:v for k,v in info.items() if k!='source_sha256'},indent=2))
if __name__=='__main__':main()
