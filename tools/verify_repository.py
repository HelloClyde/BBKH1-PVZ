"""Verify the private source snapshot, SDK, prebuilt BDA and complete assets."""
from pathlib import Path
import hashlib,json,subprocess,sys,struct,zlib
from build_paths import ROOT,SDK
def read(path):return json.loads(path.read_text(encoding='utf8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def check_files(folder,expected):
 actual={p.relative_to(folder).as_posix():sha(p) for p in sorted(folder.rglob('*')) if p.is_file()}
 if actual!=expected:
  changed=set(actual)^set(expected)|{n for n in actual.keys()&expected.keys() if actual[n]!=expected[n]}
  raise ValueError('Snapshot mismatch: '+str(folder)+': '+', '.join(sorted(changed)[:10]))
 return len(actual)
def main():
 pin=read(ROOT/'SDK.json')
 sdk_git=['git','-c','safe.directory='+SDK.as_posix(),'-C',str(SDK)]
 assert subprocess.check_output([*sdk_git,'rev-parse','HEAD'],text=True).strip()==pin['commit'],'Wrong SDK commit'
 assert not subprocess.check_output([*sdk_git,'status','--porcelain','--untracked-files=no'],text=True).strip(),'SDK has tracked edits'
 upstream=read(ROOT/'pinned-source-manifest.json');n=check_files(ROOT/'upstream',upstream['files'])
 assets=read(ROOT/'provenance/resources.json');count=check_files(ROOT/assets['directory'],assets['files'])
 from bake_file_index import encode
 file_index=read(ROOT/'provenance/offline-file-index.json');index=ROOT/assets['directory']/file_index['runtime_path']
 assert index.read_bytes()==encode(ROOT/assets['directory'])[0] and sha(index)==file_index['sha256']
 assert sha(ROOT/'tools/bake_file_index.py')==file_index['encoder_sha256']
 for info in read(ROOT/'provenance/source-assets.json')['trees']:check_files(ROOT/info['directory'],info['files'])
 definitions=read(ROOT/'provenance/offline-definitions.json')
 assert definitions['count']==len(definitions['files'])
 assert definitions['exporter_source_sha256']==sha(ROOT/'platform/definition/H1Definition.cpp')
 for name,meta in definitions['files'].items():
  path=ROOT/assets['directory']/name
  data=path.read_bytes();assert len(data)==meta['bytes'] and sha(path)==meta['sha256']
  assert data[:4]==b'H1DZ';raw=zlib.decompress(data[8:]);assert len(raw)==struct.unpack_from('<I',data,4)[0]
  assert raw[:4] in (b'H1D1',b'H1D2') and (raw[:4]!=b'H1D2' or name.startswith('reanim/')) and len(raw)-16==struct.unpack_from('<I',raw,8)[0]
  assert zlib.crc32(raw[16:])==struct.unpack_from('<I',raw,12)[0]
 for name,expected in definitions['source_xml_sha256'].items():assert sha(ROOT/assets['directory']/name)==expected
 startup=read(ROOT/'provenance/offline-startup.json')
 assert startup['font_count']==20 and startup['font_all_fields_roundtrip']
 assert startup['font_reader_sha256']==sha(ROOT/'platform/localization/FontCache.h')
 for name,meta in startup['files'].items():
  path=ROOT/assets['directory']/name;data=path.read_bytes()
  assert len(data)==meta['bytes'] and sha(path)==meta['sha256']
  magic,version,size,crc=struct.unpack_from('<4s3I',data)
  assert magic in (b'H1FC',b'H1SI') and version==1 and size+16==len(data) and zlib.crc32(data[16:])==crc
 for group in ('source_font_sha256','source_sound_sha256'):
  for name,expected in startup[group].items():assert sha(ROOT/assets['directory']/name)==expected
 music=read(ROOT/'provenance/offline-title-music.json');path=ROOT/assets['directory']/'sounds/mainmusic-title.h1pcm';data=path.read_bytes()
 assert sha(path)==music['output_sha256'] and len(data)==music['bytes'] and music['whole_loop_sample_exact']
 assert sha(ROOT/'tools/audio/bake_title.cpp')==music['exporter_sha256']
 assert sha(ROOT/assets['directory']/'sounds/mainmusic.xm')==music['source_sha256']
 magic,version,rate,samples=struct.unpack_from('<4s3I',data);assert (magic,version,rate)==(b'H1PC',1,16000) and len(data)==16+samples*2
 day=read(ROOT/'provenance/offline-day-music.json')
 from bake_day_music import prune
 pruned,details=prune(ROOT/assets['directory']/'sounds/mainmusic.xm')
 assert pruned==(ROOT/assets['directory']/'sounds/mainmusic-day.xm').read_bytes()
 assert hashlib.sha256(pruned).hexdigest()==day['output_sha256'] and len(pruned)==day['bytes']
 assert sha(ROOT/'tools/bake_day_music.py')==day['exporter_sha256']
 assert details['retained_sample_bytes']==day['retained_sample_bytes']==1841555
 proof=read(ROOT/day['verification_path']);assert proof['passed'] and proof['sample_exact'] and proof['dynamic_channels']==30
 assert proof['input_sha256']['mainmusic-day.xm']==day['output_sha256']
 assert proof['verifier_sha256']==sha(ROOT/'tools/audio/verify_day.cpp')
 pack=read(ROOT/'provenance/offline-startup-pack.json');path=ROOT/assets['directory']/pack['runtime_path'];data=path.read_bytes()
 assert sha(path)==pack['sha256'] and len(data)==pack['bytes'];magic,version,pack_count,size=struct.unpack_from('<4s3I',data)
 assert (magic,version,pack_count)==(b'H1DP',1,107) and size==len(data)-16
 at=16;names=set()
 for i in range(pack_count):
  length,size,crc=struct.unpack_from('<3I',data,at);at+=12;name=data[at:at+length].decode();at+=length;blob=data[at:at+size];at+=size
  assert name not in names and zlib.crc32(blob)==crc;names.add(name)
  assert hashlib.sha256(blob).hexdigest()==definitions['files'][name+'.h1d']['sha256']
 assert at==len(data) and names=={name[:-4] for name in definitions['files'] if name.startswith('particles/')}
 adventure=read(ROOT/'provenance/offline-adventure.json');path=ROOT/assets['directory']/adventure['runtime_path'];data=path.read_bytes()
 assert sha(path)==adventure['sha256'] and len(data)==adventure['bytes']
 assert sha(ROOT/'tools/selector_bake_export.cpp')==adventure['renderer_exporter_sha256'] and sha(ROOT/'tools/bake_selector.py')==adventure['encoder_sha256']
 header=struct.unpack_from('<16I',data);assert header[0]==0x424d3148 and header[8]==120 and header[13] in (0,1)
 poses=set()
 for i in range(header[8]):
  kind,identifier,variant,color,runs,pixels,offset,size,x,y=struct.unpack_from('<10I',data,64+i*40)
  raw=zlib.decompress(data[offset:offset+size]) if header[13] else data[offset:offset+size];assert len(raw)==runs*12+pixels*4
  if kind==3:poses.add(identifier)
 assert poses==set(range(0,449,4))|{450}
 performance=read(ROOT/'provenance/runtime-performance.json')
 actor=read(ROOT/'provenance/offline-actor-poses.json');pose=ROOT/assets['directory']/actor['runtime_path'];data=pose.read_bytes()
 assert sha(pose)==actor['sha256'] and len(data)==actor['bytes'] and not actor['lossy']
 header=struct.unpack_from('<16I',data)
 assert header[:4]==(0x50413148,1,480,272) and header[4]==actor['entries'] and header[6]==actor['palette_colors']
 assert header[5]==64 and header[7]==64+header[4]*24 and header[11]==header[7]+header[6]*4 and header[13]==26 and header[12]==actor['flash_palette_tuples']
 assert header[8]==header[11]+header[12]*104 and header[9]==len(data) and header[14]==0x314d4352
 assert zlib.crc32(data[64:])==header[10]==int(actor['crc32'],16)
 assert sha(ROOT/'tools/pack_actor_poses.py')==actor['encoder_sha256'] and sha(ROOT/'tools/tests/actor_pose_export.cpp')==actor['renderer_exporter_sha256']
 assert sha(ROOT/'platform/actors/ActorPoseFormat.h')==actor['signature_format_sha256']
 assert sha(ROOT/'tools/tests/sod_roll_export.cpp')==actor['sod_roll_exporter_sha256']
 assert sha(ROOT/'tools/tests/pea_blend_export.cpp')==actor['pea_blend_exporter_sha256']
 assert sha(ROOT/'tools/tests/zombie_flash_export.cpp')==actor['zombie_flash_exporter_sha256']
 sod_frames=set();blend_frames=set()
 for i in range(header[4]):
  key,frame,offset,size,kind=struct.unpack_from('<Q4I',data,64+i*24)
  assert kind in (2,4,7,21) and header[8]<=offset and size>=16 and offset+size<=len(data)
  if key==0x534f44524f4c4c31:sod_frames.add(frame)
  if frame&0x80000000:blend_frames.add((key,frame))
 assert sod_frames==set(range(actor['sod_roll_frames'])) and len(blend_frames)==actor['pea_blend_frames']
 assert read(ROOT/actor['transition_verification'])['passed']
 assert read(ROOT/actor['zombie_flash_replay_verification'])['passed']
 sun=read(ROOT/'provenance/offline-sun-poses.json');path=ROOT/assets['directory']/sun['runtime_path'];blob=path.read_bytes();h=struct.unpack_from('<16I',blob)
 assert sha(path)==sun['sha256'] and len(blob)==sun['bytes'] and not sun['lossy']
 assert h[:5]==(0x50413148,1,480,272,49) and h[12]==0 and h[14]==0x314d4352 and h[9]==len(blob)
 assert zlib.crc32(blob[64:])==h[10]==int(sun['crc32'],16)
 assert {struct.unpack_from('<Q4I',blob,64+i*24)[1] for i in range(49)}==set(range(49))
 assert {struct.unpack_from('<Q4I',blob,64+i*24)[4] for i in range(49)}=={22}
 assert sha(ROOT/'tools/tests/sun_pose_export.cpp')==sun['renderer_exporter_sha256']
 assert sha(ROOT/'platform/actors/ActorPoseFormat.h')==sun['signature_format_sha256']
 assert read(ROOT/sun['replay_verification'])['passed']
 lawns=read(ROOT/'provenance/offline-early-lawns.json');path=ROOT/assets['directory']/lawns['runtime_path'];data=path.read_bytes();h=struct.unpack_from('<16I',data)
 assert sha(path)==lawns['sha256'] and len(data)==lawns['bytes']
 assert h[:7]==(0x424c3148,1,480,272,16,8,6) and h[7]==lawns['tiles'] and h[8]==64 and h[9]==64+6*1020*2 and h[10]==len(data)==h[9]+h[7]*512 and h[11]==zlib.crc32(data[64:])
 assert sha(ROOT/'tools/pack_early_lawns.py')==lawns['encoder_sha256'] and sha(ROOT/'tools/tests/early_lawn_export.cpp')==lawns['renderer_exporter_sha256']
 for frame in range(6):
  pixels=bytearray(480*272*4)
  for cell in range(1020):
   tile=struct.unpack_from('<H',data,64+(frame*1020+cell)*2)[0];assert tile<h[7];at=h[9]+tile*512;y,x=divmod(cell,30);y*=8;x*=16
   for row in range(8):pixels[((y+row)*480+x)*4:((y+row)*480+x+16)*4]=data[at+row*64:at+(row+1)*64]
  assert hashlib.sha256(pixels).hexdigest()==lawns['source_frames'][f'lawn-{frame}.argb']
 camera=read(ROOT/'provenance/offline-camera-lawns.json');path=ROOT/assets['directory']/camera['runtime_path'];blob=path.read_bytes();h=struct.unpack_from('<16I',blob)
 assert sha(path)==camera['sha256'] and len(blob)==camera['bytes'] and h[:7]==(0x50433148,1,634,272,3,689792,64)
 assert len(blob)==64+3*689792 and sha(ROOT/'tools/pack_camera_lawns.py')==camera['encoder_sha256'] and sha(ROOT/'tools/tests/camera_lawn_export.cpp')==camera['renderer_exporter_sha256']
 for i in range(3):
  frame=blob[64+i*689792:64+(i+1)*689792];assert zlib.crc32(frame)==h[7+i]==int(camera['crc32'][i],16) and hashlib.sha256(frame).hexdigest()==camera['frame_sha256'][i]
 from bake_font_tiles import encode as encode_font_tiles
 font_tiles=performance['font_tiles'];font_path=ROOT/assets['directory']/font_tiles['runtime_path']
 encoded,fonts,unique=encode_font_tiles(ROOT/assets['directory'])
 assert font_path.read_bytes()==encoded and sha(font_path)==font_tiles['sha256']
 assert len(encoded)==font_tiles['bytes'] and fonts==font_tiles['fonts'] and unique==font_tiles['unique_nonzero_tiles']
 assert sha(ROOT/'tools/bake_font_tiles.py')==font_tiles['encoder_sha256']
 from bake_additive_lut import encode as encode_additive_lut
 additive=performance['additive_lut'];path=ROOT/assets['directory']/additive['runtime_path'];data=path.read_bytes()
 assert data==encode_additive_lut() and sha(path)==additive['sha256'] and len(data)==additive['bytes']
 assert sha(ROOT/'tools/bake_additive_lut.py')==additive['encoder_sha256']
 magic,version,size,crc=struct.unpack_from('<4s3I',data)
 assert (magic,version,size)==(b'H1AL',1,additive['entries']) and size==65536 and zlib.crc32(data[16:])==crc
 from bake_image_pack import encode as encode_image_pack
 pack=performance['image_pack'];paths=read(ROOT/'provenance/common-image-paths.json')['files']
 path=ROOT/assets['directory']/pack['runtime_path'];data=path.read_bytes()
 assert data==encode_image_pack(ROOT/assets['directory'],paths) and sha(path)==pack['sha256'] and len(data)==pack['bytes']
 assert len(paths)==pack['count'] and sha(ROOT/'tools/bake_image_pack.py')==pack['encoder_sha256']
 assert pack['source_sha256']=={p:sha(ROOT/assets['directory']/p) for p in paths}
 bank=performance['sound_bank'];path=ROOT/assets['directory']/bank['runtime_path'];data=path.read_bytes()
 assert sha(path)==bank['sha256'] and len(data)==bank['bytes']
 assert sha(ROOT/'tools/bake_sound_bank.py')==bank['encoder_sha256']
 magic,version,effects,size=struct.unpack_from('<4s3I',data);assert (magic,version)==(b'H1SB',1) and effects==bank['count'] and size==len(data)-16
 at=16;names=set()
 for i in range(effects):
  length,samples,crc=struct.unpack_from('<3I',data,at);at+=12;name=data[at:at+length].decode('ascii');at+=length;pcm=data[at:at+samples*2];at+=samples*2
  source=ROOT/assets['directory']/name
  assert name not in names and sha(source)==bank['source_pcm_sha256'][name] and source.read_bytes()[16:]==pcm and zlib.crc32(pcm)==crc;names.add(name)
 assert at==len(data) and names==set(bank['source_pcm_sha256'])
 menu=performance['menu_visibility'];path=ROOT/assets['directory']/menu['runtime_path'];data=path.read_bytes();header=struct.unpack_from('<16I',data)
 assert sha(path)==menu['output_sha256'] and len(data)==menu['bytes'] and header[13]==0 and header[14]==0x31434c56
 assert sha(ROOT/'tools/bake_menu_visibility.py')==menu['encoder_sha256']==adventure['offline_expander_sha256']
 assert menu['fixed_tracks']==[22,23,25,27,29,45,46,47] and menu['variant_replay_checks']==128
 numeric=sum(zlib.decompress((ROOT/assets['directory']/name).read_bytes()[8:])[:4]==b'H1D2' for name in definitions['files'])
 assert numeric==performance['numeric_resolved_reanimations']==146
 icon=read(ROOT/'assets/icon.provenance.json');assert icon['sha256']==sha(ROOT/icon['output'])
 sys.path.insert(0,str(SDK));from h1_bda.validate import validate_bda
 bda=ROOT/'prebuilt/H1PVZPortable.bda';native=read(ROOT/'docs/verification/final-native-verification.json')
 assert sha(bda)==native['program']['bda_sha256']
 validation=validate_bda(bda);assert validation['ok'],validation
 assert validation['title']==icon['desktop_title']
 assert all(not p.is_symlink() for p in (ROOT/assets['directory']).rglob('*'))
 report={'passed':True,'sdk_commit':pin['commit'],'upstream_files':n,'runtime_files':count,
         'runtime_bytes':assets['bytes'],'prebuilt_sha256':sha(bda),'BDA_validation':True,
         'desktop_title':validation['title'],'offline_definitions':definitions['count'],
         'scope':'Source, resource bytes and tested BDA envelope; not a new hardware test'}
 print(json.dumps(report,indent=2))
 return report
if __name__=='__main__':main()
