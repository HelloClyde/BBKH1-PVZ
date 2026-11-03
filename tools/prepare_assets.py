"""Prepare the complete original PvZ resource tree for the H1 port.

Images retain their native size and full 8-bit alpha. RGB/RGBA byte streams
replace runtime PNG/JPEG decoding; reanimations, particle definitions, font
metrics, strings and sound inputs stay unchanged. No game rules are generated.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
from urllib.parse import quote
import xml.etree.ElementTree as ET

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
COMMIT = '8ba94e95cfe7d0de5682f8258d3b932dbdac885c'
RAW = f'https://raw.githubusercontent.com/FregD156/PvZ_Assets/{COMMIT}/'
TREE = ROOT/'private/resource-tree.json'
OLD_TREE = WORKSPACE/'h1-pvz/private/resource-tree.json'
SOURCE = ROOT/'private/source'
OUTPUT = ROOT/'private/resources'
OLD_CACHE = WORKSPACE/'h1-pvz/private/classic'
IMAGE_EXTS = ['.png','.jpg','.gif','.tga']
MASK_ONLY = 1
HEADER = struct.Struct('<4sIIIII')


def image_metadata_index():
    """Create an optional metadata accelerator; image streams remain canonical."""
    records=[];names=bytearray()
    paths=sorted((p for p in OUTPUT.rglob('*') if p.is_file() and p.suffix in ['.h1i','.h1a']),
                 key=lambda p:p.relative_to(OUTPUT).as_posix().encode('ascii'))
    for path in paths:
        relative=path.relative_to(OUTPUT).as_posix()
        if relative!=relative.lower():raise ValueError('Metadata index path is not canonical: '+relative)
        with path.open('rb') as stream:header=stream.read(HEADER.size)
        magic,w,h,channels,flags,size=HEADER.unpack(header)
        if magic!=b'H1I1' or size!=w*h*channels or path.stat().st_size!=HEADER.size+size:
            raise ValueError('Invalid converted image: '+relative)
        records.append(struct.pack('<6I',len(names),w,h,channels,flags,size))
        names+=relative.encode('ascii')+b'\0'
    body=b''.join(records)+names
    checksum=2166136261
    for value in body:checksum=((checksum^value)*16777619)&0xffffffff
    data=struct.pack('<4s6I',b'H1X1',1,len(records),24,len(names),len(body),checksum)+body
    target=OUTPUT/'image-metadata.h1x';target.write_bytes(data)
    return dict(path=target.name,format='H1X1 LE version,count,record_bytes,string_bytes,body_bytes,FNV1a32; records=(string_offset,width,height,channels,flags,pixel_bytes)',
                files=len(records),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())


def source_tree(download):
    if not TREE.exists():
        TREE.parent.mkdir(parents=True,exist_ok=True)
        if OLD_TREE.exists():
            shutil.copyfile(OLD_TREE,TREE)
        elif download:
            subprocess.run(['curl.exe','-L','--fail','--silent','--show-error','--retry','2',
                            f'https://api.github.com/repos/FregD156/PvZ_Assets/git/trees/{COMMIT}?recursive=1',
                            '-o',str(TREE)],check=True)
        else:raise FileNotFoundError('Pinned resource tree is missing; run with --download')
    tree=json.loads(TREE.read_text(encoding='utf8'))
    if tree.get('sha')!=COMMIT or tree.get('truncated'):
        raise ValueError('Unexpected or incomplete pinned resource tree revision')
    return tree


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def blob_sha(data):
    return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()


def fetch(info, download):
    path=info['path']
    dest=SOURCE/path
    dest.parent.mkdir(parents=True,exist_ok=True)
    if not dest.exists():
        cached=OLD_CACHE/path
        if cached.exists() and blob_sha(cached.read_bytes())==info['sha']:
            shutil.copyfile(cached,dest)
        elif download:
            subprocess.run(['curl.exe','-L','--fail','--silent','--show-error',
                            '--retry','2','--connect-timeout','20','--max-time','90',
                            RAW+quote(path,safe='/'),'--globoff','-o',str(dest)],check=True)
        else:
            raise FileNotFoundError(f'{dest} is missing; use --download')
    data=dest.read_bytes()
    actual=blob_sha(data)
    if actual!=info['sha']:
        raise ValueError(f'Git blob hash mismatch: {path}')
    return path,dict(bytes=len(data),git_blob_sha1=actual,sha256=sha256(data))


def image_file(path):
    im=Image.open(path)
    im.seek(0)
    return im.convert('RGBA')


def write_image(dest, im, mask_only=False):
    width,height=im.size
    if mask_only:
        channels,flags=1,MASK_ONLY
        pixels=im.getchannel('B').tobytes()
    elif im.getchannel('A').getextrema()==(255,255):
        channels,flags=3,0
        pixels=im.convert('RGB').tobytes()
    else:
        channels,flags=4,0
        pixels=im.tobytes()
    dest.parent.mkdir(parents=True,exist_ok=True)
    data=HEADER.pack(b'H1I1',width,height,channels,flags,len(pixels))+pixels
    dest.write_bytes(data)
    return dict(width=width,height=height,channels=channels,flags=flags,
                bytes=len(data),sha256=sha256(data))


def convert_images(source_paths):
    # Match upstream extension preference exactly: PNG, JPEG, GIF, then TGA.
    by_stem={}
    for path in source_paths:
        p=Path(path)
        if p.suffix.lower() in IMAGE_EXTS:
            stem=p.with_suffix('').as_posix().lower()
            if stem not in by_stem or IMAGE_EXTS.index(p.suffix.lower())<IMAGE_EXTS.index(Path(by_stem[stem]).suffix.lower()):
                by_stem[stem]=path
    report={}
    for i,(stem,path) in enumerate(sorted(by_stem.items()),1):
        im=image_file(SOURCE/path)
        target=stem+'.h1i'
        report[target]=dict(source=path,**write_image(OUTPUT/target,im))
        if i%300==0:
            print(f'Converted {i}/{len(by_stem)} native-size images',flush=True)
    # Pre-compose the automatic _mask or mask_ convention used by ImageLib.
    # The original raw image remains .h1i for lookForAlphaImage=false calls.
    masks={}
    for stem,path in by_stem.items():
        p=Path(stem)
        if p.name.startswith('_'):
            base=(p.parent/p.name[1:]).as_posix()
            masks[base]=stem
    for stem,path in by_stem.items():
        p=Path(stem)
        if p.name.endswith('_'):
            base=(p.parent/p.name[:-1]).as_posix()
            masks.setdefault(base,stem)
    for base,mask_stem in sorted(masks.items()):
        alpha=image_file(SOURCE/by_stem[mask_stem])
        base_path=by_stem.get(base)
        if base_path:
            im=image_file(SOURCE/base_path)
            if im.size!=alpha.size:
                # Original ComposeAlpha ignores mismatched masks.
                continue
            im.putalpha(alpha.getchannel('B'))
            item=write_image(OUTPUT/(base+'.h1a'),im)
        else:
            # Original ApplyAlphaAsImage uses caller's gAlphaComposeColor.
            item=write_image(OUTPUT/(base+'.h1a'),alpha,mask_only=True)
        report[base+'.h1a']=dict(source=base_path,alpha_source=by_stem[mask_stem],**item)
    return report


def audit(source_paths):
    source_set={p.lower() for p in source_paths}
    resources=ET.parse(SOURCE/'properties/resources.xml').getroot()
    ids={}
    groups=[]
    for group in resources:
        groups.append(group.get('id'))
        prefix,path='',''
        for node in group:
            if node.tag=='SetDefaults':
                prefix=node.get('idprefix',prefix)
                path=node.get('path',path)
            elif node.tag in ['Image','Font','Sound']:
                ids[prefix+node.get('id')]=dict(group=group.get('id'),kind=node.tag,
                                             path=path+'/'+node.get('path'),attributes=dict(node.attrib))
    code=(ROOT/'upstream/src/Resources.cpp').read_text(encoding='utf8')
    expected=set(re.findall(r'Get(?:Image|Font|Sound)Throw\("([^"]+)"',code))
    refs={}
    expected_strings=set()
    for p in (ROOT/'upstream/src').rglob('*.cpp'):
        text=p.read_text(encoding='utf8')
        for path in re.findall(r'"((?:reanim|particles|trails|levels)/[^"\n]+)"',text):
            if '{' not in path:
                refs[path]=p.relative_to(ROOT/'upstream').as_posix()
        expected_strings.update(re.findall(r'"\[([A-Z][A-Z0-9_]+)\]"',text))
    strings_bytes=(SOURCE/'properties/LawnStrings.txt').read_bytes()
    try:
        strings_text=strings_bytes.decode('utf-8-sig')
        strings_encoding='utf-8'
    except UnicodeDecodeError:
        # This pinned macOS input contains only two non-ASCII bytes, both 0xA8
        # in "Zamboni[registered]". MacRoman maps 0xA8 to registered trademark.
        if set(b for b in strings_bytes if b>=128)!={0xa8}:
            raise ValueError('Unidentified source LawnStrings encoding')
        strings_text=strings_bytes.decode('mac_roman')
        strings_encoding='mac_roman'
    strings={key.strip().upper() for key in re.findall(r'\[([^\[\]\r\n]+)\]',strings_text)}
    # Dynamic resource IDs resolve by reanim basename in addition to manifest.
    available_images=set(ids)
    for path in source_paths:
        p=Path(path)
        if p.parent.as_posix().lower() in ['reanim','images'] and p.suffix.lower() in IMAGE_EXTS:
            available_images.add('IMAGE_REANIM_'+p.stem.upper())
    missing_reanim_images={}
    for path in source_paths:
        if path.lower().endswith('.reanim'):
            text=(SOURCE/path).read_text(encoding='utf8')
            for image in re.findall(r'<i>([^<]+)</i>',text):
                if image not in available_images:
                    missing_reanim_images.setdefault(image,[]).append(path)
    missing_defs={k:v for k,v in refs.items() if k.lower() not in source_set}
    missing_manifest_files={}
    for key,value in ids.items():
        path=value['path']
        candidates=[path,path+'.png',path+'.jpg',path+'.gif',path+'.tga',path+'.ogg',path+'.mo3']
        p=Path(path)
        for ext in IMAGE_EXTS:
            candidates.extend([(p.parent/('_'+p.name)).as_posix()+ext,path+'_'+ext])
        if not any(p.lower().replace('\\','/') in source_set for p in candidates):
            missing_manifest_files[key]=value
    report=dict(source_repository='https://github.com/FregD156/PvZ_Assets',source_commit=COMMIT,
                claimed_source_version='Original macOS main.pak; numeric release not identified in source README',
                upstream_target='PvZ-Portable GOTY 1.2.0.1073-compatible resource IDs',
                resource_groups=groups,manifest_resource_count=len(ids),
                upstream_static_resource_count=len(expected),missing_static_resource_ids=sorted(expected-set(ids)),
                definition_literal_count=len(refs),missing_definition_files=missing_defs,
                missing_manifest_source_files=missing_manifest_files,
                string_key_count=len(strings),literal_string_key_count=len(expected_strings),
                source_strings_encoding=strings_encoding,
                missing_literal_string_keys=sorted(expected_strings-strings),
                missing_reanimation_image_ids=missing_reanim_images,
                known_runtime_resource_differences=[dict(
                    engine_file='src/Zombie.cpp',engine_operation='SetupWaterTrack("Zombie_whitewater2")',
                    source_file='reanim/Zombie.reanim',
                    difference='Pinned macOS reanimation has two tracks both named Zombie_whitewater and no Zombie_whitewater2; GOTY code requests the latter',
                    policy='Original source track names are retained; no synthetic rename or track data is supplied')],
                full_version_compatibility_proven=False,
                version_note='Matching static IDs and definition names is evidence of coverage, not proof of an identical GOTY numeric release. No missing data is synthesized.',
                optional_platform_fallbacks={'properties/partner_logo':'Explicit transparent publisher slot, identified by H1_IMAGE_OPTIONAL_TRANSPARENT; original artwork still missing'},
                resource_manifest=ids)
    return report


def convert_audio(workers, render_music=False):
    ffmpeg=shutil.which('ffmpeg')
    ffprobe=shutil.which('ffprobe')
    if not ffmpeg or not ffprobe:
        raise FileNotFoundError('ffmpeg and ffprobe are required for PCM preparation')
    temp=ROOT/'private/audio-temp'
    temp.mkdir(parents=True,exist_ok=True)
    sources=sorted((SOURCE/'sounds').glob('*.ogg'))
    if render_music:
        sources+=sorted((SOURCE/'sounds').glob('*.mo3'))
    def convert(path):
        raw=temp/(path.stem+'.s16le')
        subprocess.run([ffmpeg,'-v','error','-nostdin','-y','-i',str(path),
                        '-vn','-ac','1','-ar','16000','-acodec','pcm_s16le',
                        '-f','s16le',str(raw)],check=True)
        samples=raw.read_bytes()
        if len(samples)%2:
            raise ValueError('Odd PCM byte count: '+str(path))
        data=struct.pack('<4sIII',b'H1PC',1,16000,len(samples)//2)+samples
        output=OUTPUT/'sounds'/(path.stem.lower()+'.h1pcm')
        output.parent.mkdir(parents=True,exist_ok=True)
        output.write_bytes(data)
        raw.unlink()
        relative=output.relative_to(OUTPUT).as_posix()
        return relative,dict(source=path.relative_to(SOURCE).as_posix(),
                             source_sha256=sha256(path.read_bytes()),bytes=len(data),
                             sample_rate=16000,channels=1,format='S16LE',
                             sample_count=len(samples)//2,duration_seconds=len(samples)/32000,
                             sha256=sha256(data),
                             music_scope='libopenmpt default subsong, no order mapping' if path.suffix.lower()=='.mo3' else None)
    audio={}
    with ThreadPoolExecutor(max_workers=min(workers,4)) as pool:
        for i,(path,meta) in enumerate(pool.map(convert,sources),1):
            audio[path]=meta
            if i%40==0 or i==len(sources):
                print(f'Converted {i}/{len(sources)} original audio files to H1PC 16k mono',flush=True)
    music={}
    for path in sorted((SOURCE/'sounds').glob('*.mo3')):
        probes={}
        for mode,extra in [('default',[]),('all_subsongs',['-subsong','-1'])]:
            cmd=[ffprobe,'-v','error',*extra,'-show_entries',
                 'format=duration:format_tags:stream=sample_rate,channels,codec_name',
                 '-of','json',str(path)]
            probes[mode]=json.loads(subprocess.check_output(cmd))
        music[path.name]=dict(source_sha256=sha256(path.read_bytes()),probes=probes,
                             order_map_available=False,
                             limitation='A sequential PCM render does not implement original order/pattern jumps, channel muting, tempo or music-layer synchronization.')
    report=dict(schema='h1-pvz-audio-1',header="16 bytes: 'H1PC', LE uint32 version=1, rate, mono_sample_count; then LE signed int16 samples",
                decoder=subprocess.check_output([ffmpeg,'-version'],text=True).splitlines()[0],
                pcm=audio,music_modules=music,
                total_pcm_bytes=sum(x['bytes'] for x in audio.values()))
    (OUTPUT/'audio-index.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(pcm_files=len(audio),pcm_bytes=report['total_pcm_bytes'],music_order_mapping=False)),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download',action='store_true',help='Fetch missing files from pinned original asset tree')
    parser.add_argument('--workers',type=int,default=10)
    parser.add_argument('--audit-only',action='store_true')
    parser.add_argument('--audio',action='store_true',help='Also prepare 16kHz mono H1PC audio')
    parser.add_argument('--audio-only',action='store_true',help='Prepare audio from an already verified source tree')
    parser.add_argument('--metadata-only',action='store_true',help='Rebuild the optional metadata index from existing converted images')
    parser.add_argument('--render-music-preview',action='store_true',help='Include default MO3 subsong PCM previews; does not supply dynamic order control')
    args=parser.parse_args()
    if args.metadata_only:
        index=image_metadata_index();path=OUTPUT/'resource-index.json'
        report=json.loads(path.read_text(encoding='utf8'));report['image_metadata_index']=index
        path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
        print(json.dumps(index),flush=True);return
    if args.audio_only:
        convert_audio(args.workers,args.render_music_preview)
        return
    tree=source_tree(args.download)
    files=[x for x in tree['tree'] if x['type']=='blob' and x['path'].split('/')[0] in ['images','reanim','particles','data','properties','sounds','levels','trails']]
    SOURCE.mkdir(parents=True,exist_ok=True)
    OUTPUT.mkdir(parents=True,exist_ok=True)
    inputs={}
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures=[pool.submit(fetch,info,args.download) for info in files]
        for i,future in enumerate(as_completed(futures),1):
            path,meta=future.result()
            inputs[path]=meta
            if i%100==0 or i==len(files):
                print(f'Verified {i}/{len(files)} original source files',flush=True)
    audit_report=audit(sorted(inputs))
    (ROOT/'assets').mkdir(exist_ok=True)
    (ROOT/'assets/resource-audit.json').write_text(json.dumps(audit_report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    if args.audit_only:
        print(json.dumps({k:v for k,v in audit_report.items() if k.startswith('missing_')},ensure_ascii=False))
        return
    # Non-image inputs are exact byte copies. Original animation data stays full.
    copied={}
    for path in sorted(inputs):
        if Path(path).suffix.lower() not in IMAGE_EXTS:
            dest=OUTPUT/path.lower()
            dest.parent.mkdir(parents=True,exist_ok=True)
            data=(SOURCE/path).read_bytes()
            transformation='exact byte copy'
            if path.lower()=='properties/lawnstrings.txt' and audit_report['source_strings_encoding']=='mac_roman':
                data=data.decode('mac_roman').encode('utf8')
                transformation='MacRoman to UTF-8, preserving all original keys and values'
            dest.write_bytes(data)
            copied[path.lower()]=dict(bytes=len(data),sha256=sha256(data),
                                       source=path,source_sha256=inputs[path]['sha256'],
                                       transformation=transformation)
    images=convert_images(sorted(inputs))
    report=dict(schema='h1-pvz-resources-1',source_commit=COMMIT,
                resource_dimensions='original, no resampling',image_format='H1I1 header + RGB24/RGBA32 or caller-colored alpha mask',
                image_count=len(images),image_bytes=sum(x['bytes'] for x in images.values()),
                image_metadata_index=image_metadata_index(),
                copied_files=len(copied),copied_bytes=sum(x['bytes'] for x in copied.values()),
                inputs=dict(sorted(inputs.items())),images=dict(sorted(images.items())),
                copied=dict(sorted(copied.items())),
                audit_summary={k:v for k,v in audit_report.items() if k.startswith('missing_') or k=='known_runtime_resource_differences'})
    (OUTPUT/'resource-index.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:report[k] for k in ['image_count','image_bytes','copied_files','copied_bytes','audit_summary']},ensure_ascii=False),flush=True)
    if args.audio:
        convert_audio(args.workers,args.render_music_preview)


if __name__=='__main__':
    main()
