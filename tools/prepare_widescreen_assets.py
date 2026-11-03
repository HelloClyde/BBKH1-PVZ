"""Create a separate wide resource tree; original assets and savedata stay intact."""
from pathlib import Path
import argparse,hashlib,json,shutil,struct
import xml.etree.ElementTree as ET
from PIL import Image
from prepare_h1_assets import metadata_index,read_image
ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',type=Path,default=ROOT/'private/resources-h1-wide')
    args=ap.parse_args();out=args.output.resolve();baseline=ROOT/'private/resources-h1-full'
    assert out.is_relative_to((ROOT/'private').resolve()) and out!=baseline.resolve()
    out.mkdir(parents=True,exist_ok=True)
    owner=out/'wide-owner.json'
    if any(out.iterdir()) and not owner.exists():raise ValueError('Unowned output directory')
    owner.write_text(json.dumps({'owner':'pvzh1-wide-480x272-v1'}))
    shutil.copytree(baseline,out,dirs_exist_ok=True)
    changed={}
    def encode(source,name):
        with Image.open(source) as im:im=im.convert('RGB').resize((480,272),Image.Resampling.LANCZOS)
        pixels=im.tobytes();p=out/name
        p.write_bytes(struct.pack('<4s7I',b'H1I2',1060,600,3,0,len(pixels),480,272)+pixels)
        changed[name]={'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'source':str(source.relative_to(ROOT))}
    encode(ROOT/'private/widescreen/source/title-outpaint-v1.png','images/titlescreen.h1i')
    encode(ROOT/'private/widescreen/source/menu-clean-background-v1.png','reanim/selectorscreen_bg.h1i')
    encode(ROOT/'private/widescreen/source/award-outpaint-v1.png','images/awardscreen_back.h1i')
    path=out/'reanim/selectorscreen.reanim';root=ET.fromstring('<r>'+path.read_text(encoding='utf8')+'</r>')
    shifted=[]
    for track in root.findall('track'):
        name=track.findtext('name','');frames=track.findall('t')
        right=(name=='SelectorScreen_BG_Right' or name.startswith('almanac_') or
               name.startswith('SelectorScreen_') and any(n in name for n in ('_Survival_','_Challenges_','_ZenGarden_','_StartAdventure_','_Adventure_')) or name.startswith('flower'))
        if right:
            shifted.append(name)
            initial_missing=bool(frames) and frames[0].find('x') is None
            for frame in frames:
                node=frame.find('x')
                if node is not None:node.text=f'{float(node.text)+260:.6f}'
            if initial_missing:ET.SubElement(frames[0],'x').text='260'
        if name=='SelectorScreen_BG':
            for frame in frames:
                for axis in ('sx','sy'):
                    node=frame.find(axis)
                    if node is not None:node.text=f'{float(node.text)/8:.6f}'
        if name.startswith('Cloud'):
            for frame in frames:
                node=frame.find('x')
                if node is not None:node.text=f'{float(node.text)*1060/800:.6f}'
    path.write_text('\n'.join(ET.tostring(e,encoding='unicode') for e in root),encoding='utf8')
    changed['reanim/selectorscreen.reanim']={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'shifted_tracks':shifted}
    # This animation is instantiated only by the selector's Adventure action.
    # Its debris and rising hand must follow the relocated original gravestone.
    path=out/'reanim/zombie_hand.reanim';hand=ET.fromstring('<r>'+path.read_text(encoding='utf8')+'</r>')
    for track in hand.findall('track'):
        frames=track.findall('t');initial_missing=bool(frames) and frames[0].find('x') is None
        for frame in frames:
            node=frame.find('x')
            if node is not None:node.text=f'{float(node.text)+260:.6f}'
        if initial_missing:ET.SubElement(frames[0],'x').text='260'
    path.write_text('\n'.join(ET.tostring(e,encoding='unicode') for e in hand),encoding='utf8')
    changed['reanim/zombie_hand.reanim']={'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'horizontal_offset':260}
    index,_=metadata_index(out);changed['image-metadata.h1x']=index
    inventory=json.loads((out/'resource-index.json').read_text(encoding='utf8'))
    for name in ('images/titlescreen.h1i','reanim/selectorscreen_bg.h1i','images/awardscreen_back.h1i'):
        info,_=read_image(out/name);inventory['images'][name].update(info,bytes=(out/name).stat().st_size,sha256=hashlib.sha256((out/name).read_bytes()).hexdigest())
    inventory.update(schema='h1-pvz-resources-wide-1',image_metadata_index=index,
                     resource_dimensions='Original game grids; widescreen title and selector background use 1060x600 logical / 480x272 storage')
    (out/'resource-index.json').write_text(json.dumps(inventory,indent=2)+'\n',encoding='utf8')
    report={'schema':'h1-pvz-wide-assets-v1','physical':[480,272],'logical':[1060,600],'board':[130,0,800,600],
            'baseline_modified':False,'savedata_included':False,'changed':changed}
    (out/'widescreen-manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
