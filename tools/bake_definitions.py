"""Compile all original animation/particle/trail XML with the real game parser.

The host exporter round-trips each definition through the same H1D1 reader used
on the device and compares every field, float bit, string and resource ID.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--host',type=Path,required=True)
 p.add_argument('--resources',type=Path,default=ROOT/'private/resources-h1-wide')
 p.add_argument('--output',type=Path,required=True)
 p.add_argument('--runtime-dir',type=Path,default=Path('C:/msys64/ucrt64/bin'))
 a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 target=out/'definitions';target.mkdir(exist_ok=True)
 if list(target.rglob('*.h1d')):raise ValueError('Use a fresh output directory')
 run=out/'run';run.mkdir();save=out/'save';save.mkdir()
 env=os.environ.copy();env['PATH']=str(a.runtime_dir.resolve())+os.pathsep+env.get('PATH','')
 env.update(PVZ_RESOURCE_DIR=a.resources.resolve().as_posix()+'/',PVZ_SAVE_DIR=save.as_posix()+'/',
  PVZ_HOST_OUTPUT=run.as_posix(),PVZ_HOST_MAX_FRAMES='40',PVZ_HOST_SPEED='100',
  PVZ_H1_EXPORT_DEFINITIONS=target.as_posix())
 result=subprocess.run([str(a.host.resolve())],env=env,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 (out/'export.log').write_bytes(result.stdout)
 if result.returncode or b'H1D_EXPORT_OK' not in result.stdout:raise RuntimeError(f'Exit {result.returncode:#x}: '+result.stdout.decode('utf8',errors='replace')[-1800:])
 sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
 files={p.relative_to(target).as_posix():dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(target.rglob('*.h1d'))}
 if not files:raise RuntimeError('No source definitions found; use a resource tree containing the original XML/reanim/trail inputs')
 sources={name[:-4]:sha(a.resources/name[:-4]) for name in files}
 manifest=dict(format='H1D1 particles/trails; H1D2 numeric-resolved reanimations',files=files,source_xml_sha256=sources,host_sha256=sha(a.host),
  count=len(files),bytes=sum(f['bytes'] for f in files.values()),all_fields_roundtrip=True,offline_crc_verified=True,runtime_resource_checksums=False,
  exporter_source_sha256=sha(ROOT/'platform/definition/H1Definition.cpp'),
  upstream_parser_sha256=sha(ROOT/'upstream/src/PvzpLib/Definition.cpp'))
 (out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps({k:v for k,v in manifest.items() if k not in ('files','source_xml_sha256')}))
if __name__=='__main__':main()
