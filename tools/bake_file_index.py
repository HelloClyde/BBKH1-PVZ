"""Bake an authoritative directory of immutable resource names and sizes."""
from pathlib import Path
import hashlib,json,struct
ROOT=Path(__file__).resolve().parents[1]
NAME='resource-files.h1fi'
def encode(base):
 entries={}
 for p in sorted(base.rglob('*')):
  if not p.is_file() or p.relative_to(base).as_posix()==NAME:continue
  rel=p.relative_to(base).as_posix();assert 'userdata' not in p.relative_to(base).parts
  assert rel==rel.lower() and '\\' not in rel and len(rel.encode())<512
  entries[rel]=(p.stat().st_size,0)
  for parent in Path(rel).parents:
   if str(parent)!='.':entries[parent.as_posix()]=(0,1)
 strings=bytearray();records=bytearray()
 for name,(size,flags) in sorted(entries.items()):
  records+=struct.pack('<3I',len(strings),size,flags);strings+=name.encode()+b'\0'
 body=records+strings;checksum=2166136261
 for v in body:checksum=((checksum^v)*16777619)&0xffffffff
 return struct.pack('<4s7I',b'H1FI',1,len(entries),12,len(strings),len(body),checksum,0)+body,entries
def main():
 inventory=ROOT/'provenance/resources.json';meta=json.loads(inventory.read_text(encoding='utf8'));base=ROOT/meta['directory']
 data,entries=encode(base);(base/NAME).write_bytes(data)
 files={p.relative_to(base).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(base.rglob('*')) if p.is_file()}
 meta['files']=files;meta['bytes']=sum(p.stat().st_size for p in base.rglob('*') if p.is_file())
 if 'count' in meta:meta['count']=len(files)
 inventory.write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 proof=dict(version=1,runtime_path=NAME,bytes=len(data),sha256=files[NAME],entries=len(entries),files=sum(not flag for size,flag in entries.values()),directories=sum(flag for size,flag in entries.values()),encoder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),scope='Immutable resource tree; savedata and the index itself excluded. Regenerate after resource changes.')
 (ROOT/'provenance/offline-file-index.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(json.dumps(proof))
if __name__=='__main__':main()
