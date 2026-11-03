"""Fetch the pinned upstream zlib required by PvZ's real resource/save code."""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

ROOT=Path(__file__).resolve().parents[1]
def prepare_zlib():
    cache=ROOT/'build/downloads';cache.mkdir(parents=True,exist_ok=True)
    archive=cache/'zlib-1.3.1.tar.gz';url='https://zlib.net/fossils/zlib-1.3.1.tar.gz'
    if not archive.exists() and not (ROOT/'third_party/zlib-1.3.1/zlib.h').is_file():
        subprocess.run(['C:/Windows/System32/curl.exe','--fail','--location','--retry','3',
                        '--silent','--show-error','--output',str(archive),url],check=True)
    digest=hashlib.sha256(archive.read_bytes()).hexdigest() if archive.exists() else '9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23'
    if digest!='9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23':
        raise ValueError('Pinned zlib archive checksum mismatch')
    target=ROOT/'third_party/zlib-1.3.1'
    if not target.exists():
        target.parent.mkdir(exist_ok=True)
        with tarfile.open(archive) as source:source.extractall(target.parent,filter='data')
    report={'name':'zlib','version':'1.3.1','url':url,'archive_sha256':digest}
    (ROOT/'build/zlib-source.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    return target

if __name__=='__main__':print(prepare_zlib())
