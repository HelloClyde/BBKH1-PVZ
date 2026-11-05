"""Lay out existing H1-density A8 glyph pixels as independently readable tiles."""
from pathlib import Path
import hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1]
REL='data/font-tiles.h1ft'

def encode(base):
    fonts=[];unique={bytes(256):0};payload=bytearray();tables=[]
    for path in sorted((base/'data').glob('h1zh*.h1a')):
        src=path.read_bytes();magic,w,h,ch,flags,n,sw,sh=struct.unpack_from('<4s7I',src)
        assert (magic,ch,flags,n,len(src))==(b'H1I2',1,1,sw*sh,32+sw*sh)
        cols=(sw+15)//16;rows=(sh+15)//16;table=[]
        for ty in range(rows):
            for tx in range(cols):
                tile=bytearray(256)
                for y in range(min(16,sh-ty*16)):
                    start=(ty*16+y)*sw+tx*16;k=min(16,sw-tx*16)
                    tile[y*16:y*16+k]=src[32+start:32+start+k]
                tile=bytes(tile)
                if tile not in unique:
                    unique[tile]=len(payload)+1;payload+=tile
                table.append((unique[tile],zlib.crc32(tile)))
        fonts.append(dict(name=path.stem,storage_width=sw,storage_height=sh,columns=cols,rows=rows,
                          source_sha256=hashlib.sha256(src).hexdigest(),source_bytes=n))
        tables.append(table)
    table_base=32+64*len(fonts);data_base=table_base+sum(len(t)*8 for t in tables)
    directory=bytearray();all_tables=bytearray()
    for f,table in zip(fonts,tables):
        packed=b''.join(struct.pack('<2I',data_base+off-1 if off else 0,crc) for off,crc in table)
        name=f['name'].encode();assert len(name)<32
        directory+=struct.pack('<32s8I',name,f['storage_width'],f['storage_height'],f['columns'],f['rows'],
                               table_base+len(all_tables),len(packed),zlib.crc32(packed),0)
        all_tables+=packed
    total=data_base+len(payload)
    header=struct.pack('<4s7I',b'H1FT',1,len(fonts),64,total,data_base,zlib.crc32(directory),16)
    data=header+directory+all_tables+payload
    assert len(data)==total
    # Reconstruct every physical pixel, including edge tiles, without resampling.
    for f,table in zip(fonts,tables):
        src=(base/'data'/f"{f['name']}.h1a").read_bytes()[32:];sw=f['storage_width'];sh=f['storage_height']
        for y in range(sh):
            for x in range(sw):
                off,crc=table[(y//16)*f['columns']+x//16]
                value=payload[off-1+(y%16)*16+x%16] if off else 0
                assert value==src[y*sw+x]
    return data,fonts,len(unique)-1

def main():
    base=ROOT/'private/resources-h1-wide';data,fonts,count=encode(base)
    (base/REL).write_bytes(data)
    proof=dict(format='H1FT1 exact A8 16x16 raw tiles; zero tiles omitted; duplicates shared on disk',
               runtime_path=REL,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),fonts=fonts,
               unique_nonzero_tiles=count,all_physical_pixels_verified=sum(f['source_bytes'] for f in fonts),
               encoder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    path=ROOT/'provenance/runtime-performance.json';meta=json.loads(path.read_text());meta['font_tiles']=proof
    path.write_text(json.dumps(meta,indent=2)+'\n',encoding='utf8',newline='\n')
    print(json.dumps({k:v for k,v in proof.items() if k!='fonts'}))
if __name__=='__main__':main()
