"""Bake LCD pixel offsets into the existing lossless ordinary-frame runs."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    source=args.source.read_bytes()
    data=bytearray(source)
    h=struct.unpack_from('<16I',data)
    assert h[:4]==(0x50413148,1,480,272) and h[15]==20
    packets=set()
    runs=0
    for i in range(h[4]):
        _,_,offset,size,_=struct.unpack_from('<Q4I',data,64+i*24)
        if offset in packets:
            continue
        packets.add(offset)
        assert struct.unpack_from('<H',data,offset+6)[0]==20
        struct.pack_into('<H',data,offset+6,52)
        segments=struct.unpack_from('<H',data,offset+4)[0]
        left,top,right,bottom=struct.unpack_from('<hhhh',data,offset+8)
        at=offset+16
        for _ in range(segments):
            _,draw,count,length=struct.unpack_from('<hHII',data,at)
            assert not draw&32768 and not length&0x80000000
            at+=12
            end=at+length
            for _ in range(count):
                x,y,flags,reserved=struct.unpack_from('<hhHH',source,at)
                assert left<=x<x+(flags&32767)<=right and top<=y<bottom and right-left<=480 and not reserved
                assert -480<=x<=480 and -272<=y<=272
                struct.pack_into('<i',data,at,y*480+x)
                linear,actual_flags,actual_reserved=struct.unpack_from('<iHH',data,at)
                restored_y=(linear-left)//480
                assert (linear-restored_y*480,restored_y,actual_flags,actual_reserved)==(x,y,flags,reserved)
                at+=8+(flags&32767)*4
                runs+=1
            assert at==end
        assert at==offset+size
    struct.pack_into('<I',data,60,52)
    struct.pack_into('<I',data,40,zlib.crc32(data[64:]))
    args.output.write_bytes(data)
    sha=lambda b:hashlib.sha256(b).hexdigest()
    report=dict(passed=True,runtime_path='actor-normal.h1ap',bytes=len(data),sha256=sha(data),source_sha256=sha(source),
                encoder_sha256=sha(Path(__file__).read_bytes()),entries=h[4],unique_packets=len(packets),
                converted_runs=runs,all_coordinates_roundtrip_exact=True,all_pixel_literals_unchanged=True,
                storage='aligned_raw_RGBA32_LCD_offsets',lossy=False)
    args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report))


if __name__=='__main__':
    main()
