"""Make valid and independently checksummed malformed H1PS reader inputs."""
from pathlib import Path
import argparse, hashlib, json, struct, sys, zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pack_actor_pages import encode, decode

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('pack',type=Path);p.add_argument('report',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.mkdir(exist_ok=False)
    source=a.pack.read_bytes();proof=json.loads(a.report.read_text());row=next(r for r in proof['files'] if r['type']==4 and r['filter']==0)
    def component(name):
        c=proof['components'][row[name]];return source[c['offset']:c['offset']+c['bytes']]
    original_geometry=decode(component('geometry'));original_index=zlib.decompress(component('index'));original_palette=zlib.decompress(component('palette'))
    records=[]
    for fault in range(14):
        geometry=bytearray(original_geometry);index=bytearray(original_index);palette=original_palette
        first=struct.unpack_from('<Q4I',index)
        if fault==8:struct.pack_into('<I',index,20,5)
        if fault==9:struct.pack_into('<H',geometry,first[2]+6,0)
        if fault==10:
            at=first[2]+16
            while True:
                _,draw,runs,size=struct.unpack_from('<hHII',geometry,at);at+=12
                if runs:break
                at+=size
            struct.pack_into('<H',geometry,at+(3 if draw&32768 else 6),len(palette)//4)
        if fault==11:index[24:36]=index[:12]
        blobs=[encode(geometry),zlib.compress(index,9),zlib.compress(palette,9)]
        if fault==7:blobs[2]=blobs[2][:-1]+bytes([blobs[2][-1]^1])
        count=17;table=bytearray(count*12);at=32+len(table);offsets=[]
        for blob in blobs:offsets.append(at);at+=len(blob)
        h=[0x53503148,1,4,0,len(index)//24,len(palette)//4,offsets[0],len(blobs[0]),len(geometry),zlib.crc32(geometry),offsets[1],len(blobs[1]),zlib.crc32(index),offsets[2],len(blobs[2]),zlib.crc32(palette)]
        if fault==1:h[0]^=1
        if fault==2:h[3]=1
        if fault==3:h[5]=0
        if fault==4:h[15]^=1
        if fault==5:h[12]^=1
        if fault==6:h[9]^=1
        if fault==12:h[6]=0xffffff00
        descriptor=struct.pack('<16I',*h);struct.pack_into('<3I',table,16*12,at,64,zlib.crc32(descriptor))
        payload=b''.join(blobs)+descriptor
        if fault==13:payload=payload[:-1]
        data=struct.pack('<8I',0x4c503148,2,count,len(table),32+len(table)+len(payload),zlib.crc32(table),0,0)+table+payload
        folder=a.output/str(fault);folder.mkdir();(folder/'plant-poses.h1pp').write_bytes(data)
        records.append(dict(fault=fault,expected_valid=not fault,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    (a.output/'manifest.json').write_text(json.dumps(records,indent=2)+'\n')
    print('Prepared',len(records),'H1PS fixtures')

if __name__=='__main__':main()
