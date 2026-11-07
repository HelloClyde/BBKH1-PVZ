"""Bake original preview factories to the H1 display density and one pack."""
from pathlib import Path
import argparse, hashlib, json, struct, zlib
from PIL import Image
from optimize_h1_resources import metadata


def fnv(data):
    value=2166136261
    for byte in data:value=((value^byte)*16777619)&0xffffffff
    return value


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('export',type=Path)
    p.add_argument('--metadata',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--output-metadata',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();source=json.loads((a.export/'cached-frames.json').read_text(encoding='utf8'));assert source['original_cached_frame_factories']
    files={};records=[];previews=[]
    for entry in source['files']:
        raw=(a.export/'raw'/(entry['name']+'.argb')).read_bytes();assert len(raw)==entry['bytes'] and zlib.crc32(raw)==entry['crc32']
        w,h=entry['width'],entry['height'];sw=max(1,(w*480+530)//1060);sh=max(1,(h*272+300)//600)
        original=Image.frombytes('RGBA',(w,h),raw,'raw','BGRA')
        image=original.convert('RGBa').resize((sw,sh),Image.Resampling.LANCZOS).convert('RGBA');pixels=image.tobytes()
        blob=struct.pack('<4s7I',b'H1I2',w,h,4,0,len(pixels),sw,sh)+pixels;name='cache/'+entry['name']+'.h1i';files[name]=blob
        row=dict(entry,runtime_path=name,storage_width=sw,storage_height=sh,sha256=hashlib.sha256(blob).hexdigest(),source_sha256=hashlib.sha256(raw).hexdigest());records.append(row)
        if entry['kind']==0 and entry['variation']==0:previews.append((entry['name'],image))
    names=sorted(files);table_bytes=len(names)*16+sum(len(n)+1 for n in names);strings=bytearray();table=bytearray();payload=bytearray()
    for name in names:
        blob=files[name];table+=struct.pack('<4I',len(strings),32+table_bytes+len(payload),len(blob),zlib.crc32(blob));strings+=name.encode('ascii')+b'\0';payload+=blob
    table+=strings;pack=struct.pack('<4s7I',b'H1PK',1,len(names),len(table),32+len(table)+len(payload),fnv(table),0,0)+table+payload;a.output.write_bytes(pack)
    old=a.metadata.read_bytes();_,version,count,stride,_,_,_=struct.unpack_from('<4s6I',old);assert version==2 and stride==32;infos={}
    fields=('width','height','channels','flags','pixel_bytes','storage_width','storage_height')
    for i in range(count):
        row=struct.unpack_from('<8I',old,28+i*32);at=28+count*32+row[0];name=old[at:old.index(0,at)].decode('ascii');infos[name]=dict(zip(fields,row[1:]))
    for entry in records:infos[entry['runtime_path']]=dict(zip(fields,[entry['width'],entry['height'],4,0,entry['storage_width']*entry['storage_height']*4,entry['storage_width'],entry['storage_height']]))
    index=metadata(infos);a.output_metadata.write_bytes(index)
    # The output pack is decoded independently and every H1I2 byte is checked.
    for i,name in enumerate(names):
        at,offset,size,crc=struct.unpack_from('<4I',pack,32+i*16);assert pack[offset:offset+size]==files[name] and zlib.crc32(files[name])==crc
    report=dict(passed=True,runtime_path='cached-frames.h1pk',bytes=len(pack),sha256=hashlib.sha256(pack).hexdigest(),
                metadata_sha256=hashlib.sha256(index).hexdigest(),encoder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                exporter_sha256=hashlib.sha256(Path('tools/tests/cached_frames_export.cpp').read_bytes()).hexdigest(),
                original_factories_sha256=hashlib.sha256(Path('upstream/src/Lawn/System/ReanimationLawn.cpp').read_bytes()).hexdigest(),
                plant_variants=source['plant_variants'],mowers=source['mowers'],zombies=source['zombies'],files=records,
                scale=[480,1060,272,600],runtime_canvas_rendering=False,maximum_runtime_data_block_bytes=4096)
    a.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    preview=Image.new('RGBA',(8*96,((len(previews)+7)//8)*96),(50,70,55,255))
    for i,(_,image) in enumerate(previews):preview.alpha_composite(image,(i%8*96+10,i//8*96+10))
    preview.save(a.output.with_suffix('.png'));print(json.dumps({k:v for k,v in report.items() if k!='files'}))


if __name__=='__main__':main()
