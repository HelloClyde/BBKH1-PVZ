"""Create a complete A-program / B-resource install ZIP, excluding savedata."""
from pathlib import Path
import argparse,json,zipfile
from build_paths import ROOT
from verify_repository import main as verify,sha,read
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bda',type=Path,default=ROOT/'prebuilt/H1PVZPortable.bda');p.add_argument('--output',type=Path,default=ROOT/'dist/H1PVZ-full-install.zip');a=p.parse_args()
 verify();sys_path=ROOT/'sdk'
 import sys;sys.path.insert(0,str(sys_path));from h1_bda.validate import validate_bda
 bda=a.bda.resolve();assert validate_bda(bda)['ok']
 assert sha(bda)==read(ROOT/'docs/verification/final-native-verification.json')['program']['bda_sha256'],'Install resources only with their hardware-tested program'
 output=a.output.resolve();assert output.is_relative_to(ROOT) and not output.exists(),'Choose a new package output; preserve existing archives'
 output.parent.mkdir(parents=True,exist_ok=True)
 resources=read(ROOT/'provenance/resources.json');base=ROOT/resources['directory']
 members=[('A/应用/程序/H1PVZPortable.bda',bda)]
 members.extend(('B/应用/数据/游戏/PVZ/'+name,base/name) for name in resources['files'])
 members.extend((name,ROOT/name) for name in ('README.md','NOTICE.md','LICENSE','COPYING','SDK.json','docs/verification/final-native-verification.json'))
 members.extend((path.relative_to(ROOT).as_posix(),path) for path in sorted((ROOT/'licenses').rglob('*')) if path.is_file())
 expected={name:sha(path) for name,path in members}
 with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for name,path in members:z.write(path,name)
  z.writestr('package-manifest.json',json.dumps({'program_sha256':sha(bda),'resources':resources['directory'],'includes_savedata':False,'files':expected},ensure_ascii=False,indent=2)+'\n')
 with zipfile.ZipFile(output) as z:
  assert len(z.namelist())==len(expected)+1
  import hashlib
  for name,value in expected.items():assert hashlib.sha256(z.read(name)).hexdigest()==value,name
 output.with_suffix('.zip.sha256').write_text(sha(output)+'  '+output.name+'\n',encoding='ascii')
 print('Verified complete install ZIP:',output)
if __name__=='__main__':main()
