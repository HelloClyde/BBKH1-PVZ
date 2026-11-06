"""Finish H1-density images and separate reproducible inputs from runtime files.

The native BDA and logical resource IDs/coordinates stay unchanged. Alpha-grid
composition follows ResourceManager's blue-channel rule before filtering.
"""
from pathlib import Path
import argparse, hashlib, json, re, shutil, struct, xml.etree.ElementTree as ET
from PIL import Image
from prepare_h1_assets import read_image

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT/'private/resources-h1-wide'
INPUTS = ROOT/'private/resource-build-inputs'
PROOF = ROOT/'provenance/runtime-resource-optimization.json'

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def write_json(p, v): p.write_text(json.dumps(v, ensure_ascii=False, indent=2)+'\n', encoding='utf8', newline='\n')
def scaled(n): return max(1, (n*9+10)//20)
def load(p):
    info, pixels = read_image(p)
    return info, Image.frombytes({1:'L',3:'RGB',4:'RGBA'}[info['channels']], (info['storage_width'],info['storage_height']), pixels)
def resize(im, size):
    return im.convert('RGBa').resize(size,Image.Resampling.LANCZOS).convert('RGBA') if im.mode=='RGBA' else im.resize(size,Image.Resampling.LANCZOS)
def encode(info, im):
    ch={'L':1,'RGB':3,'RGBA':4}[im.mode]; b=im.tobytes()
    return struct.pack('<4s7I',b'H1I2',info['width'],info['height'],ch,1 if ch==1 else 0,len(b),*im.size)+b
def metadata(infos):
    names=bytearray(); records=bytearray()
    for name,i in sorted(infos.items()):
        records+=struct.pack('<8I',len(names),*[i[k] for k in ('width','height','channels','flags','pixel_bytes','storage_width','storage_height')]);names+=name.encode('ascii')+b'\0'
    body=records+names; h=2166136261
    for v in body:h=((h^v)*16777619)&0xffffffff
    return struct.pack('<4s6I',b'H1X2',2,len(infos),32,len(names),len(body),h)+body

def font_layouts(base):
    layouts={}
    for p in (base/'data').glob('*.txt'):
        text=p.read_text('utf8',errors='replace')
        lists={m[1]:[tuple(map(int,r)) for r in re.findall(r'\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)',m[2])] for m in re.finditer(r'Define\s+(\w+)\s*\((.*?)\);',text,re.S)}
        maps={m[1]:m[3] for m in re.finditer(r'LayerSetImageMap\s+(\w+)\s+(\w+)\s+(\w+)',text)}
        for m in re.finditer(r'LayerSetImage\s+(\w+)\s+[\'"]([^\'"]+)[\'"]',text):layouts.setdefault('data/'+m[2].lower(),set()).update(lists.get(maps.get(m[1]),[]))
    return layouts

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--baseline',type=Path,required=True);a=parser.parse_args()
    base=a.baseline.resolve();assert base.is_relative_to(ROOT/'build') and base!=RUNTIME
    assert not PROOF.exists(), 'Use a fresh baseline/review existing proof before regenerating'
    original={p.relative_to(base).as_posix():p for p in base.rglob('*') if p.is_file()}
    assert set(original)=={p.relative_to(RUNTIME).as_posix() for p in RUNTIME.rglob('*') if p.is_file()}
    assert all(sha(p)==sha(RUNTIME/n) for n,p in original.items()),'Working runtime differs from frozen baseline'
    before=sum(p.stat().st_size for p in original.values());changed={};removed={};virtual={}
    def archive(name):
        src=original[name];dest=INPUTS/name;dest.parent.mkdir(parents=True,exist_ok=True)
        if dest.exists():assert sha(dest)==sha(src)
        else:shutil.copy2(src,dest)
    def put(name,data,reason):
        p=RUNTIME/name
        if name in original:archive(name)
        p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        changed[name]={'reason':reason,'before_bytes':original[name].stat().st_size if name in original else 0,'bytes':len(data),'sha256':sha(p)}
    def drop(name,reason):
        if name not in original or not (RUNTIME/name).is_file():return
        archive(name);current=RUNTIME/name
        if name in changed:
            dest=INPUTS/'optimized'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(current,dest)
        removed[name]={'reason':reason,'bytes':current.stat().st_size,'sha256':sha(current),'baseline_sha256':sha(original[name]),'baseline_bytes':original[name].stat().st_size};current.unlink()
    xml=(base/'properties/resources.xml').read_text('utf8');tree=ET.fromstring(xml);grids={};composed=[];mask_names=set()
    for group in tree:
        directory=''
        for node in group:
            if node.tag=='SetDefaults':directory=node.get('path',directory).lower()
            if node.tag!='Image':continue
            stem=(directory+'/'+node.attrib['path']).lower();cols=int(node.get('cols','1'));rows=int(node.get('rows','1'));grids[stem]=(cols,rows)
            if 'alphagrid' not in node.attrib:continue
            mask=(directory+'/'+node.attrib['alphagrid']).lower();mask_names.add(mask+'.h1i')
            src=next(base/(stem+ext) for ext in ('.h1a','.h1i') if (base/(stem+ext)).exists());info,im=load(src);mi,alpha=load(base/(mask+'.h1i'))
            assert im.size==(info['width'],info['height']) and alpha.size==(im.width//cols,im.height//rows)
            alpha=alpha.convert('RGB').getchannel('B');full=Image.new('L',im.size)
            for y in range(rows):
                for x in range(cols):full.paste(alpha,(x*alpha.width,y*alpha.height))
            im=im.convert('RGBA');im.putalpha(full);size=(scaled(im.width),scaled(im.height));im=resize(im,size)
            name=stem+'.h1a';put(name,encode(info,im),'Offline blue-channel alpha-grid composition, premultiplied filtering at H1 density')
            if src.suffix=='.h1i':drop(stem+'.h1i','Superseded by precomposed RGBA alpha variant')
            attribute=' alphagrid="'+node.attrib['alphagrid']+'"'
            # Restrict the edit to this exact Image element; preserve all other XML bytes/IDs.
            pattern=r'(<Image\b[^>]*\bid="'+re.escape(node.attrib['id'])+r'"[^>]*>)'
            matches=list(re.finditer(pattern,xml));assert len(matches)==1
            old=matches[0][0];assert attribute in old;xml=xml.replace(old,old.replace(attribute,''),1)
            composed.append({'id':node.attrib['id'],'path':name,'mask':mask,'logical':[info['width'],info['height']],'storage':list(size)})
    put('properties/resources.xml',xml.encode('utf8'),'Alpha grids baked into RGBA; same IDs and logical geometry')
    for name in mask_names:drop(name,'Mask fully composed offline; no remaining runtime XML reference')
    layouts=font_layouts(base);scaled_exceptions=[]
    for name,p in original.items():
        if p.suffix not in ('.h1a','.h1i') or name in changed or name in removed:continue
        info,im=load(p);w,h=info['width'],info['height']
        if im.size!=(w,h) or (w,h)==(1,1):continue
        stem=name.rsplit('.',1)[0].replace('data/_','data/');rects=[]
        if name.startswith('data/'):
            rects=sorted(r for r in layouts[stem] if r[2]>0 and r[3]>0)
            assert rects
            # Small punctuation still needs one physical texel. Select the
            # smallest near-target grid that preserves every nonempty glyph.
            sw=next(k for k in range(scaled(w),w+1) if all((x+rw)*k//w>x*k//w for x,y,rw,rh in rects))
            sh=next(k for k in range(scaled(h),h+1) if all((y+rh)*k//h>y*k//h for x,y,rw,rh in rects))
        else:
            cols,rows=grids.get(stem,(1,1));cw,ch=w//cols,h//rows
            rects=[(x*cw,y*ch,cw,ch) for y in range(rows) for x in range(cols)];sw,sh=scaled(w),scaled(h)
        out=Image.new(im.mode,(sw,sh),0)
        for x,y,rw,rh in rects:
            left,top=x*sw//w,y*sh//h;right,bottom=(x+rw)*sw//w,(y+rh)*sh//h
            assert right>left and bottom>top
            out.paste(resize(im.crop((x,y,x+rw,y+rh)),(right-left,bottom-top)),(left,top))
        put(name,encode(info,out),'Offline per-cel/per-glyph H1 scaling; source metrics and nonempty punctuation preserved')
        scaled_exceptions.append({'path':name,'logical':[w,h],'storage':[sw,sh],'rectangles':rects})
    performance=json.loads((ROOT/'provenance/runtime-performance.json').read_text('utf8'))
    paths=sorted(performance['image_pack']['source_sha256'])
    from bake_image_pack import encode as image_pack_encode
    data=image_pack_encode(RUNTIME,paths);put('images-common.h1pk',data,'Common pack rebuilt with final scaled Latin glyphs')
    performance['image_pack'].update(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),source_sha256={n:sha(RUNTIME/n) for n in paths})
    write_json(ROOT/'provenance/runtime-performance.json',performance)
    # Native image_pack_open precedes standalone fopen. Keep virtual image
    # metadata while removing only the byte-identical packed backing copies.
    for name in performance['image_pack']['source_sha256']:
        virtual[name]={'backing':'images-common.h1pk','info':read_image(RUNTIME/name)[0]};drop(name,'Byte-identical image exists in mandatory common image pack')
    for name in sorted(original):
        p=RUNTIME/name
        if not p.is_file():continue
        if re.fullmatch(r'data/h1zh\w+\.h1a',name):
            virtual[name]={'backing':'data/font-tiles.h1ft','info':read_image(p)[0]};drop(name,'Physical A8 pixels already stored in native mandatory font tile pack')
        elif p.suffix in ('.ogg','.mo3'):drop(name,'H1 backend reads converted PCM/XM, preserving original audio input only for builds')
        elif p.suffix in ('.reanim','.xml','.trail') and (base/(name+'.h1d')).is_file():drop(name,'Original parser input replaced by verified H1D definition')
        elif name.startswith('particles/') and p.suffix=='.xml':
            source=b'\n'.join(f.read_bytes().lower() for directory in (ROOT/'upstream/src',ROOT/'platform') for f in directory.rglob('*') if f.suffix in ('.cpp','.h'))
            assert name.encode() not in source,'Keep particle definitions referenced by game code'
            drop(name,'Unregistered particle editor/example input; no production source reference')
        elif p.suffix in ('.json','.sh'):drop(name,'Build provenance/tooling; no runtime reader')
        elif name=='h1menu/selector.h1mb':drop(name,'Non-widescreen selector; production uses selector-wide.h1mb')
        elif name.startswith('data/') and p.suffix=='.txt' and (base/(name+'.h1fc')).is_file():drop(name,'Font descriptor fully represented in verified H1FC cache; retain build input')
    infos={p.relative_to(RUNTIME).as_posix():read_image(p)[0] for p in RUNTIME.rglob('*') if p.suffix in ('.h1i','.h1a')}
    infos.update({name:v['info'] for name,v in virtual.items()});put('image-metadata.h1x',metadata(infos),'Metadata covers standalone images and packed/virtual font sources')
    write_json(PROOF,{'schema':1,'baseline_bytes':before,'baseline_files':len(original),'BDA_unchanged':True,'logical_geometry_unchanged':True,'archive_directory':INPUTS.relative_to(ROOT).as_posix(),'changed':changed,'removed':removed,'virtual_images':virtual,'alpha_compositions':composed,'scaled_exceptions':scaled_exceptions,'encoder_sha256':sha(Path(__file__))})
    print(json.dumps({'baseline_bytes':before,'new_bytes_before_file_index':sum(p.stat().st_size for p in RUNTIME.rglob('*') if p.is_file()),'removed_files':len(removed),'changed_files':len(changed),'compositions':len(composed),'scaled_exceptions':len(scaled_exceptions)},indent=2))

if __name__=='__main__':main()
