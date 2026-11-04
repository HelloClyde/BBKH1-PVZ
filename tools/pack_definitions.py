"""Pack already verified H1D files for one sequential startup read."""
from pathlib import Path
import argparse,hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--resources',type=Path,default=ROOT/'private/resources-h1-wide');a=p.parse_args()
 manifest=json.loads((ROOT/'provenance/offline-definitions.json').read_text());payload=bytearray()
 entries=0
 for name,meta in manifest['files'].items():
  if not name.startswith('particles/'):continue
  entries+=1
  data=(a.resources/name).read_bytes();assert hashlib.sha256(data).hexdigest()==meta['sha256'];key=name[:-4].encode('utf8')
  payload+=struct.pack('<3I',len(key),len(data),zlib.crc32(data))+key+data
 output=struct.pack('<4s3I',b'H1DP',1,entries,len(payload))+payload;a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(output)
 print(json.dumps(dict(entries=entries,bytes=len(output),sha256=hashlib.sha256(output).hexdigest())))
if __name__=='__main__':main()
