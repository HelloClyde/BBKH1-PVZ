"""Install the SHA256-pinned Windows MIPS GCC; the H1 SDK has no downloader."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,os,shutil,subprocess,tempfile,urllib.request,zipfile
from build_paths import ROOT,TOOLCHAIN
URL='https://static.grumpycoder.net/pixel/mips/g++-mipsel-none-elf-15.2.0.zip'
SHA256='8ba866e25c9826ee04ab4310365d264e3e73769e3738bb58ae38fd6740b7ee8d'
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--archive',type=Path);p.add_argument('--output',type=Path,default=TOOLCHAIN);a=p.parse_args()
 assert os.name=='nt','The pinned distribution is Windows-only'
 out=a.output.resolve();assert out.is_relative_to(ROOT) and not out.exists(),'Preserve existing toolchain; pass its bin directory to build.py'
 archive=a.archive.resolve() if a.archive else ROOT/'.tools/mips-gcc-15.2.0.zip'
 archive.parent.mkdir(parents=True,exist_ok=True)
 if not archive.exists():
  temporary=archive.with_suffix('.download');assert not temporary.exists(),'Existing partial download preserved'
  req=urllib.request.Request(URL,headers={'User-Agent':'BBKH1-PVZ/1'})
  with urllib.request.urlopen(req,timeout=60) as src,temporary.open('xb') as dst:shutil.copyfileobj(src,dst)
  assert sha(temporary)==SHA256,'Toolchain download checksum mismatch';temporary.replace(archive)
 assert sha(archive)==SHA256,'Toolchain archive checksum mismatch'
 out.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(dir=out.parent,prefix='mips-stage-') as d:
  stage=Path(d).resolve()
  with zipfile.ZipFile(archive) as z:
   for member in z.infolist():
    name=PurePosixPath(member.filename.replace('\\','/'));target=(stage/Path(*name.parts)).resolve()
    assert not name.is_absolute() and '..' not in name.parts and ':' not in member.filename and target.is_relative_to(stage),'Unsafe archive path'
    assert ((member.external_attr>>16)&0o170000)!=0o120000,'Archive symlink refused'
   z.extractall(stage)
  compilers=list(stage.rglob('bin/mipsel-none-elf-gcc.exe'));assert len(compilers)==1
  assert '15.2.0' in subprocess.check_output([compilers[0],'--version'],text=True).splitlines()[0]
  shutil.copytree(compilers[0].parent.parent,out)
 print('Installed verified GNU MIPS GCC 15.2.0:',out)
if __name__=='__main__':main()
