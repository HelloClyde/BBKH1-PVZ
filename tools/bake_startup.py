"""Compile original font descriptors and sound header metadata off the H1."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,struct,subprocess,zlib
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--resources',type=Path,default=ROOT/'private/resources-h1-wide');a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);target=out/'assets';target.mkdir();run=out/'run';run.mkdir();save=out/'save';save.mkdir()
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 env.update(PVZ_RESOURCE_DIR=a.resources.resolve().as_posix()+'/',PVZ_SAVE_DIR=save.as_posix()+'/',PVZ_HOST_OUTPUT=run.as_posix(),PVZ_HOST_MAX_FRAMES='5',PVZ_HOST_SPEED='100',PVZ_H1_EXPORT_FONTS=target.as_posix())
 result=subprocess.run([str(a.host.resolve())],env=env,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'export.log').write_bytes(result.stdout)
 if result.returncode or b'H1FC_EXPORT_OK' not in result.stdout:raise RuntimeError(result.stdout.decode('utf8',errors='replace')[-2400:])
 payload=bytearray();sounds={}
 for file in sorted(a.resources.rglob('*.h1pcm')):
  data=file.read_bytes();magic,version,rate,samples=struct.unpack_from('<4s3I',data)
  assert magic==b'H1PC' and version==1 and rate==16000 and len(data)==16+samples*2,file
  name=file.relative_to(a.resources).as_posix();key=name.encode();payload+=struct.pack('<2I',len(key),samples)+key;sounds[name]=hashlib.sha256(data).hexdigest()
 index=target/'sounds/index.h1si';index.parent.mkdir(exist_ok=True);index.write_bytes(struct.pack('<4s3I',b'H1SI',1,len(payload),zlib.crc32(payload))+payload)
 sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
 files={f.relative_to(target).as_posix():{'bytes':f.stat().st_size,'sha256':sha(f)} for f in sorted(target.rglob('*')) if f.is_file()}
 fonts=[name for name in files if name.endswith('.h1fc')];assert len(fonts)==20,fonts
 manifest={'files':files,'font_count':len(fonts),'sound_count':len(sounds),'source_sound_sha256':sounds,'source_font_sha256':{name[:-5]:sha(a.resources/name[:-5]) for name in fonts},'font_all_fields_roundtrip':True,'host_sha256':sha(a.host),'font_reader_sha256':sha(ROOT/'platform/localization/FontCache.h')}
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n',encoding='utf8');print(json.dumps({'fonts':len(fonts),'sounds':len(sounds),'bytes':sum(f['bytes'] for f in files.values())}))
if __name__=='__main__':main()
