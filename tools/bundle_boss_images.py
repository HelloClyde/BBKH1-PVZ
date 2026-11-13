"""Bundle measured Boss texture misses using the existing byte-exact H1PK reader."""
from pathlib import Path
import argparse,hashlib,json,shutil
from bake_image_pack import encode
from resource_inputs import input_path
from prepare_h1_assets import read_image
from bake_file_index import encode as encode_index,NAME as INDEX
ROOT=Path(__file__).resolve().parents[1]
RUNTIME=ROOT/'private/resources-h1-wide'
INPUTS=ROOT/'private/resource-build-inputs'
WORK=ROOT/'build/boss-image-pack-assets'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf8'))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def main():
 p=argparse.ArgumentParser();p.add_argument('phase',choices=['prepare','commit']);a=p.parse_args()
 pathsfile=ROOT/'provenance/common-image-paths.json';packfile=RUNTIME/'images-common.h1pk'
 if a.phase=='prepare':
  WORK.mkdir(exist_ok=False);meta=read(pathsfile);old=set(meta['files'])
  added=sorted(f.relative_to(RUNTIME).as_posix() for f in RUNTIME.rglob('*')
    if f.is_file() and f.suffix in ('.h1i','.h1a') and 'boss' in f.name and f.relative_to(RUNTIME).as_posix() not in old)
  assert added
  shutil.copy2(packfile,WORK/'original-images-common.h1pk');shutil.copy2(pathsfile,WORK/'original-common-image-paths.json')
  write(WORK/'inputs.json',{'files':{n:{'sha256':sha(RUNTIME/n),'bytes':(RUNTIME/n).stat().st_size} for n in added},
        'old_pack_sha256':sha(packfile)})
  meta['files']=sorted(old|set(added));packfile.write_bytes(encode(RUNTIME,meta['files']));write(pathsfile,meta)
  print(json.dumps({'added':len(added),'pack_bytes':packfile.stat().st_size}))
  return
 test=read(ROOT/'build/boss-image-pack-test/verification.json');assert test['passed'] and test['pack_sha256']==sha(packfile)
 inputs=read(WORK/'inputs.json');proofpath=ROOT/'provenance/runtime-resource-optimization.json';proof=read(proofpath)
 manifestpath=ROOT/'provenance/source-assets.json';manifest=read(manifestpath)
 tree=next(t for t in manifest['trees'] if t['directory']==INPUTS.relative_to(ROOT).as_posix())
 for n,row in inputs['files'].items():
  source=RUNTIME/n;assert source.resolve().is_relative_to(RUNTIME.resolve()) and sha(source)==row['sha256']
  target=INPUTS/n
  if target.exists() and sha(target)!=row['sha256']:target=INPUTS/'optimized'/n
  assert target.resolve().is_relative_to(INPUTS.resolve());target.parent.mkdir(parents=True,exist_ok=True)
  if target.exists():assert sha(target)==row['sha256']
  else:
   shutil.copy2(source,target);tree['files'][target.relative_to(INPUTS).as_posix()]=row['sha256'];tree['bytes']+=row['bytes']
  baseline=INPUTS/n
  proof['virtual_images'][n]={'backing':'images-common.h1pk','info':read_image(source)[0]}
  proof['removed'][n]={'reason':'Byte-identical Boss texture in common pack; avoid repeated native FAT opens',
    **row,'baseline_sha256':sha(baseline),'baseline_bytes':baseline.stat().st_size}
  source.unlink()
 meta=read(pathsfile);perfpath=ROOT/'provenance/runtime-performance.json';perf=read(perfpath)
 perf['image_pack'].update(count=len(meta['files']),bytes=packfile.stat().st_size,sha256=sha(packfile),
   source_sha256={n:sha(input_path(RUNTIME,n)) for n in meta['files']})
 perf['image_pack']['scope']='Byte-identical common and Boss H1I bytes; shared stream, no new decoded frames'
 write(perfpath,perf);write(manifestpath,manifest)
 proof['changed']['images-common.h1pk'].update(bytes=packfile.stat().st_size,sha256=sha(packfile))
 proof['boss_image_bundle']={'files':inputs['files'],'test_sha256':sha(ROOT/'build/boss-image-pack-test/verification.json'),
   'encoder_sha256':sha(Path(__file__)),'resource_pixels_changed':False}
 write(proofpath,proof)
 data,entries=encode_index(RUNTIME);(RUNTIME/INDEX).write_bytes(data)
 inventorypath=ROOT/'provenance/resources.json';inventory=read(inventorypath)
 for n in inputs['files']:inventory['files'].pop(n)
 for n in ('images-common.h1pk',INDEX):inventory['files'][n]=sha(RUNTIME/n)
 inventory['bytes']=sum(f.stat().st_size for f in RUNTIME.rglob('*') if f.is_file())
 if 'count' in inventory:inventory['count']=len(inventory['files'])
 write(inventorypath,inventory)
 oldindex=read(ROOT/'provenance/offline-file-index.json')
 oldindex.update(bytes=len(data),sha256=sha(RUNTIME/INDEX),entries=len(entries),
   files=sum(not flag for size,flag in entries.values()),directories=sum(flag for size,flag in entries.values()))
 write(ROOT/'provenance/offline-file-index.json',oldindex)
 write(WORK/'committed.json',{'runtime_bytes':inventory['bytes'],'runtime_files':len(inventory['files']),
   'pack_sha256':sha(packfile),'pack_bytes':packfile.stat().st_size,'removed_loose_images':len(inputs['files'])})
 print((WORK/'committed.json').read_text(encoding='utf8'))
if __name__=='__main__':main()
