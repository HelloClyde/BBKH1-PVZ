"""Prepare reviewed platform overrides without changing pinned PvZ-Portable."""
from __future__ import annotations
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
COMMIT='98b659380014c89c3553fbb2839f5e5a84d6df3d'

def prepare(destination=None):
    upstream=ROOT/'upstream';source=Path(destination).resolve() if destination is not None else ROOT/'build/source'
    if not source.is_relative_to((ROOT/'build').resolve()):raise ValueError('Prepared copies must stay inside this project build directory')
    if (upstream/'.git').exists():
        git=['git','-c','safe.directory='+upstream.resolve().as_posix(),'-C',str(upstream)]
        commit=subprocess.check_output([*git,'rev-parse','HEAD'],text=True).strip()
        if commit!=COMMIT:raise ValueError('PvZ-Portable checkout does not match pinned commit')
        if subprocess.check_output([*git,'status','--porcelain','--untracked-files=no'],text=True).strip():
            raise ValueError('Pinned upstream checkout has tracked modifications')
        date=subprocess.check_output([*git,'show','-s','--format=%cs','HEAD'],text=True).strip()
    else:
        manifest_path=ROOT/'pinned-source-manifest.json'
        if not manifest_path.is_file():raise ValueError('A source archive without Git must include its pinned-source-manifest.json')
        manifest=json.loads(manifest_path.read_text(encoding='utf8'))
        commit=manifest.get('upstream_commit')
        if commit!=COMMIT:raise ValueError('Source archive manifest commit does not match the pinned upstream')
        expected=manifest.get('files')
        if not isinstance(expected,dict) or not expected:raise ValueError('Pinned source manifest has no file identities')
        actual={}
        for path in sorted(upstream.rglob('*')):
            if '.git' in path.relative_to(upstream).parts or not path.is_file():continue
            if not path.resolve().is_relative_to(upstream.resolve()):raise ValueError('Source file escapes the pinned tree')
            actual[path.relative_to(upstream).as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
        if actual!=expected:
            changed=sorted(set(actual)^set(expected) | {name for name in actual.keys()&expected.keys() if actual[name]!=expected[name]})
            raise ValueError('Pinned source archive file-set/hash mismatch: '+', '.join(changed[:12]))
        date=manifest.get('commit_date','2026-10-31')
    # Recopy pristine inputs on every preparation, making checked patches
    # repeatable even when a previous prepared tree was already patched.
    shutil.copytree(upstream/'src',source,dirs_exist_ok=True,ignore=shutil.ignore_patterns('SDL-Mixer-X'))
    patch_path=ROOT/'platform/source_patches.py'
    spec=importlib.util.spec_from_file_location('pvz_h1_source_patches',patch_path)
    patches=importlib.util.module_from_spec(spec);spec.loader.exec_module(patches)
    patches.apply(source)
    version=(upstream/'CMake/ProjectVersion.h.in').read_text(encoding='utf8')
    version=version.replace('@PVZP_VERSION@','0.1-h1-'+COMMIT[:12]).replace('@PVZP_BUILD_NUMBER@','1').replace('@PVZP_COMMIT_DATE@',date)
    (source/'ProjectVersion.h').write_text(version,encoding='utf8')
    meta={'upstream_commit':commit,'patches_sha256':hashlib.sha256(patch_path.read_bytes()).hexdigest(),
          'prepared_sources':{p.relative_to(source).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in sorted(source.rglob('*')) if p.is_file() and p.suffix in ('.h','.cpp','.c','.inl','.inc')}}
    (source.parent/'prepared.json').write_text(json.dumps(meta,indent=2)+'\n',encoding='utf8')
    return source,patches.REPLACEMENTS

if __name__=='__main__':
    source,replacements=prepare()
    print(json.dumps({'source':str(source),'upstream_commit':COMMIT,'replacements':replacements},indent=2))
