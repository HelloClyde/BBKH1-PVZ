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
  assert raw[:4]==b'H1D1' and len(raw)-16==struct.unpack_from('<I',raw,8)[0]
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
 header=struct.unpack_from('<16I',data);assert header[0]==0x424d3148 and header[8]==120 and header[13]==1
 poses=set()
 for i in range(header[8]):
  kind,identifier,variant,color,runs,pixels,offset,size,x,y=struct.unpack_from('<10I',data,64+i*40)
  raw=zlib.decompress(data[offset:offset+size]);assert len(raw)==runs*12+pixels*4
  if kind==3:poses.add(identifier)
 assert poses==set(range(0,449,4))|{450}
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
