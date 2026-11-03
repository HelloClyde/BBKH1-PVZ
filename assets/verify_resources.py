"""Cross-check H1 C/C++ image loading against original Pillow-decoded inputs."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'private/source'
OUTPUT=ROOT/'private/resources'
BUILD=ROOT/'private/loader-verification'
BUILD.mkdir(parents=True,exist_ok=True)


def fnv_argb(im,color=None):
    value=2166136261
    for r,g,b,a in im.get_flattened_data():
        pixel=(a<<24)|(r<<16)|(g<<8)|b
        value=((value^pixel)*16777619)&0xffffffff
    return value


def main():
    originals={p.relative_to(SOURCE).as_posix().lower():p for p in SOURCE.rglob('*') if p.is_file()}
    def find(stem):
        for ext in ['.png','.jpg','.gif','.tga']:
            if stem.lower()+ext in originals:return originals[stem.lower()+ext]
        return None
    def expected(stem,auto_alpha,color):
        base=find(stem)
        im=Image.open(base).convert('RGBA') if base else None
        if auto_alpha:
            p=Path(stem)
            mask=find((p.parent/('_'+p.name)).as_posix()) or find(stem+'_')
            if mask:
                alpha=Image.open(mask).convert('RGBA')
                if im is None:
                    im=Image.new('RGBA',alpha.size,((color>>16)&255,(color>>8)&255,color&255,255))
                if im.size==alpha.size:im.putalpha(alpha.getchannel('B'))
        if im is None:raise ValueError(stem)
        return im
    cases=[('images/background1',1,0xffffff),('reanim/peashooter_head',1,0xffffff),
           ('images/blank',1,0xffffff),('data/briannetod12',1,0x13579b),
           ('data/_briannetod12',0,0xffffff),('images/projectilepea',1,0xffffff),
           ('reanim/zombie_head',1,0xffffff),('reanim/sun1',1,0xffffff),
           ('images/seedbank',1,0xffffff),('reanim/anim_sprout',1,0xffffff),
           ('images/pool_base_',1,0xffffff),('images/pool_base',1,0xffffff),
           ('images/pool_base_night',1,0xffffff)]
    lines=[]
    for stem,auto,color in cases:
        im=expected(stem,auto,color)
        raw=find(stem)
        flags=1 if raw is None else 0
        # Exercise case and slash normalization independently from source lookup.
        logical=stem.upper().replace('/','\\')
        lines.append(f'{logical} {auto} {color:x} {im.width} {im.height} {fnv_argb(im):x} {flags}')
    lines.append('properties/partner_logo 1 ffffff 1 1 50c5d1f 2')
    # FNV for one transparent 0x00000000 pixel.
    lines[-1]=f'properties/partner_logo 1 ffffff 1 1 {(2166136261*16777619)&0xffffffff:x} 2'
    casefile=BUILD/'cases.txt';casefile.write_text('\n'.join(lines)+'\n',encoding='ascii')
    include=ROOT/'platform/resources'
    framework=ROOT/'upstream/src/SexyAppFramework'
    subprocess.run([shutil.which('gcc'),'-std=c99','-Wall','-Wextra','-Werror','-c',
                    str(include/'h1_resources.c'),'-o',str(BUILD/'h1_resources.o')],check=True)
    exe=BUILD/'test_resource_loader.exe'
    subprocess.run([shutil.which('g++'),'-std=c++20','-Wall','-Wextra','-Werror','-static-libstdc++','-static-libgcc',
                    '-I'+str(include),'-I'+str(framework),str(include/'ImageLib.cpp'),
                    str(ROOT/'assets/test_resource_loader.cpp'),str(BUILD/'h1_resources.o'),
                    '-o',str(exe)],check=True)
    result=subprocess.run([str(exe),str(OUTPUT),str(casefile)],capture_output=True,text=True,check=True)
    print(result.stdout,end='');print(result.stderr,end='')
    index=json.loads((OUTPUT/'resource-index.json').read_text(encoding='utf8'))
    checked=0
    for path,meta in index['images'].items():
        data=(OUTPUT/path).read_bytes()
        if hashlib.sha256(data).hexdigest()!=meta['sha256']:raise ValueError(path)
        checked+=1
    report=dict(host_loader_cases=len(lines),converted_images_hash_checked=checked,
                result=result.stdout.strip(),optional_logo_report=result.stderr.strip(),
                image_dimensions='Original sizes preserved; full 8-bit alpha compared',
                source_commit=index['source_commit'])
    (ROOT/'assets/loader-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(f'Verified all {checked} converted-image file hashes.')


if __name__=='__main__':main()
