"""Losslessly compress independent offline actor pages; retain all original bytes."""
from pathlib import Path
import hashlib,json,struct,zlib,shutil
ROOT=Path(__file__).resolve().parents[1]
PAGE=4096
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def encode(raw):
 pages=[raw[i:i+PAGE] for i in range(0,len(raw),PAGE)]
 rows=[];payload=bytearray()
 for page in pages:
  blob=zlib.compress(page,9);assert len(blob)<=4128
  rows.append(struct.pack('<3I',len(payload),len(blob),zlib.crc32(page)));payload+=blob
 return struct.pack('<4s7I',b'H1PZ',1,len(raw),len(pages),PAGE,len(payload),len(rows)*12,zlib.crc32(raw))+b''.join(rows)+payload
def decode(blob):
 magic,version,size,count,page_bytes,stored,table,crc=struct.unpack_from('<4s7I',blob)
 assert magic==b'H1PZ' and version==1 and page_bytes==PAGE and count==(size+PAGE-1)//PAGE and table==count*12 and len(blob)==32+table+stored
 result=bytearray();last=0
 for i in range(count):
  offset,length,checksum=struct.unpack_from('<3I',blob,32+i*12);assert offset==last and 0<length<=4128 and offset+length<=stored
  raw=zlib.decompress(blob[32+table+offset:32+table+offset+length]);assert len(raw)==min(PAGE,size-i*PAGE) and zlib.crc32(raw)==checksum
  result+=raw;last+=length
 assert last==stored and len(result)==size and zlib.crc32(result)==crc
 return bytes(result)
def main():
 runtime=ROOT/'private/resources-h1-wide';name='actor-poses.h1ap';target=runtime/name
 raw=target.read_bytes();assert raw[:4]==b'H1AP';encoded=encode(raw);assert decode(encoded)==raw
 archive=ROOT/'private/resource-build-inputs'/name;assert not archive.exists();shutil.copy2(target,archive);target.write_bytes(encoded)
 proof_path=ROOT/'provenance/offline-actor-poses.json';proof=json.loads(proof_path.read_text('utf8'))
 proof.update(decoded_sha256=hashlib.sha256(raw).hexdigest(),decoded_bytes=len(raw),sha256=sha(target),bytes=len(encoded),page_container=dict(schema=1,page_bytes=PAGE,decoded_cache_pages=32,encoder_sha256=sha(Path(__file__)),decoded_byte_exact=True))
 proof_path.write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8')
 p=ROOT/'provenance/runtime-resource-optimization.json';d=json.loads(p.read_text('utf8'));d['changed'][name]=dict(reason='Lossless independent pages of existing offline actor pixels; bounded decoded cache',before_bytes=len(raw),bytes=len(encoded),sha256=sha(target));d['BDA_unchanged']=False;d['requires_actor_page_reader']=True;d['BDA_unchanged_scope']='Initial image adaptation/pruning used the existing image reader. The new lossless actor page envelope requires the matching tested program; distribute them together.';p.write_text(json.dumps(d,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
 p=ROOT/'provenance/source-assets.json';d=json.loads(p.read_text('utf8'));t=next(t for t in d['trees'] if t['directory']=='private/resource-build-inputs');t['files'][name]=sha(archive);t['bytes']+=len(raw);p.write_text(json.dumps(d,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
 from bake_file_index import main as bake_file_index
 bake_file_index()
 print(json.dumps(dict(passed=True,decoded_bytes=len(raw),encoded_bytes=len(encoded),decoded_byte_exact=True,decoded_cache_bytes=32*PAGE)))
if __name__=='__main__':main()
