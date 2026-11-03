"""Verify the private source snapshot, SDK, prebuilt BDA and complete assets."""
from pathlib import Path
import hashlib,json,subprocess,sys
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
 for info in read(ROOT/'provenance/source-assets.json')['trees']:check_files(ROOT/info['directory'],info['files'])
 sys.path.insert(0,str(SDK));from h1_bda.validate import validate_bda
 bda=ROOT/'prebuilt/H1PVZPortable.bda';native=read(ROOT/'docs/verification/final-native-verification.json')
 assert sha(bda)==native['program']['bda_sha256']
 validation=validate_bda(bda);assert validation['ok'],validation
 assert all(not p.is_symlink() for p in (ROOT/assets['directory']).rglob('*'))
 report={'passed':True,'sdk_commit':pin['commit'],'upstream_files':n,'runtime_files':count,
         'runtime_bytes':assets['bytes'],'prebuilt_sha256':sha(bda),'BDA_validation':True,
         'scope':'Source, resource bytes and tested BDA envelope; not a new hardware test'}
 print(json.dumps(report,indent=2))
 return report
if __name__=='__main__':main()
