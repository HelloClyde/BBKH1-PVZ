"""Remove RGB shadows after verifying the production preferred-alpha readers."""
from pathlib import Path
import json,hashlib,shutil
from optimize_h1_resources import ROOT,RUNTIME,INPUTS,PROOF,metadata,write_json,sha
from prepare_h1_assets import read_image
from resource_inputs import input_path
from bake_image_pack import encode

def main():
    proof=json.loads(PROOF.read_text('utf8'));assert 'preferred_alpha_pruning' not in proof
    observation=ROOT/'build/resource-opt-preference-host/run/resource-runtime-verification.json'
    test=json.loads(observation.read_text('utf8'))
    assert test['passed'] and test['non_alpha_image_opens']==test['non_alpha_image_probes']==0
    assert (test['rendered_images'],test['rendered_fonts'],test['reanimation_definitions'])==(691,20,144)
    names={p.relative_to(RUNTIME).as_posix() for p in RUNTIME.rglob('*') if p.is_file()}|set(proof['virtual_images'])
    shadows=sorted(n for n in names if n.endswith('.h1i') and n[:-4]+'.h1a' in names)
    def drop(n,reason):
        source=RUNTIME/n;assert source.resolve().is_relative_to(RUNTIME.resolve()) and source.is_file()
        target=INPUTS/n
        if target.exists():
            assert n in proof['changed'];target=INPUTS/'optimized'/n
        target.parent.mkdir(parents=True,exist_ok=True)
        if target.exists():assert sha(target)==sha(source)
        else:shutil.copy2(source,target)
        baseline=INPUTS/n
        proof['removed'][n]=dict(reason=reason,bytes=source.stat().st_size,sha256=sha(source),baseline_sha256=sha(baseline),baseline_bytes=baseline.stat().st_size)
        source.unlink()
    paths_file=ROOT/'provenance/common-image-paths.json';paths_meta=json.loads(paths_file.read_text('utf8'))
    paths=sorted({n[:-4]+'.h1a' if n in shadows else n for n in paths_meta['files']})
    packed=encode(RUNTIME,paths)
    for n in shadows:
        if (RUNTIME/n).exists():drop(n,'Non-preferred RGB source; production readers exclusively select existing alpha variant')
        else:
            assert n in proof['virtual_images'];del proof['virtual_images'][n]
            proof['removed'][n]['reason']='Non-preferred RGB pack entry replaced by its actual preferred alpha variant'
    for n in paths:
        if n not in proof['virtual_images']:
            proof['virtual_images'][n]=dict(backing='images-common.h1pk',info=read_image(RUNTIME/n)[0])
            drop(n,'Preferred alpha image now backed by common pack; standalone duplicate removed')
    paths_meta['files']=paths
    if 'count' in paths_meta:paths_meta['count']=len(paths)
    paths_meta['preferred_variants_only']=True;write_json(paths_file,paths_meta)
    (RUNTIME/'images-common.h1pk').write_bytes(packed)
    perf_file=ROOT/'provenance/runtime-performance.json';perf=json.loads(perf_file.read_text('utf8'));pack=perf['image_pack']
    pack.update(count=len(paths),bytes=len(packed),sha256=sha(RUNTIME/'images-common.h1pk'),source_sha256={n:sha(input_path(RUNTIME,n)) for n in paths})
    write_json(perf_file,perf)
    infos={p.relative_to(RUNTIME).as_posix():read_image(p)[0] for p in RUNTIME.rglob('*') if p.suffix in ('.h1i','.h1a')}
    infos.update({n:v['info'] for n,v in proof['virtual_images'].items()});(RUNTIME/'image-metadata.h1x').write_bytes(metadata(infos))
    for n in ('images-common.h1pk','image-metadata.h1x'):
        proof['changed'][n].update(bytes=(RUNTIME/n).stat().st_size,sha256=sha(RUNTIME/n))
    sources=('platform/resources/ImageLib.cpp','platform/graphics/memory_patches.py','platform/graphics/MemoryImageCache.cpp')
    proof['preferred_alpha_pruning']=dict(raw_shadows=shadows,raw_images_removed=len(shadows),encoder_sha256=sha(Path(__file__)),reader_source_sha256={n:sha(ROOT/n) for n in sources},observation_sha256=sha(observation),scope='Same preferred alpha pixels; remove never-selected RGB variant and pack shadows')
    write_json(PROOF,proof)
    manifest_path=ROOT/'provenance/source-assets.json';manifest=json.loads(manifest_path.read_text('utf8'));tree=next(t for t in manifest['trees'] if t['directory']==INPUTS.relative_to(ROOT).as_posix())
    files=[p for p in INPUTS.rglob('*') if p.is_file()];tree.update(files={p.relative_to(INPUTS).as_posix():sha(p) for p in sorted(files)},bytes=sum(p.stat().st_size for p in files));write_json(manifest_path,manifest)
    from bake_file_index import main as bake_file_index
    bake_file_index()
    print(json.dumps({'removed_raw_shadows':len(shadows),'common_pack_images':len(paths),'runtime_bytes':sum(p.stat().st_size for p in RUNTIME.rglob('*') if p.is_file())},indent=2))
if __name__=='__main__':main()
