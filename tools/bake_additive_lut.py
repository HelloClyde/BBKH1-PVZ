"""Precompute the exact legacy /256 then /255 additive sample contribution."""
from pathlib import Path
import hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1]
def encode():
    data=bytearray()
    for alpha in range(256):
        for channel in range(256):
            sample=(((channel*alpha)&0xff00)//alpha) if alpha else 0
            data.append(channel if alpha>=240 else (sample*max(0,alpha-1)+127)//255)
    return struct.pack('<4s3I',b'H1AL',1,len(data),zlib.crc32(data))+data
def main():
    data=encode();name='data/additive.h1lut';(ROOT/'private/resources-h1-wide'/name).write_bytes(data)
    path=ROOT/'provenance/runtime-performance.json';meta=json.loads(path.read_text());meta['additive_lut']=dict(runtime_path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),encoder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),entries=65536,scope='Exact legacy nearest-texel additive contribution; no effect or animation removed')
    path.write_text(json.dumps(meta,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps(meta['additive_lut']))
if __name__=='__main__':main()
