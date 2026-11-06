"""Verify H1-only runtime inventory and its archived build dependencies."""
from pathlib import Path
import hashlib,json,struct,xml.etree.ElementTree as ET
from prepare_h1_assets import read_image
from resource_inputs import input_path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify():
    proof=json.loads((ROOT/'provenance/runtime-resource-optimization.json').read_text('utf8'))
    base=ROOT/'private/resources-h1-wide';archive=ROOT/proof['archive_directory']
    assert proof['logical_geometry_unchanged']
    if proof.get('requires_actor_page_reader'):assert not proof['BDA_unchanged']
    else:assert proof['BDA_unchanged']
    assert sha(ROOT/'tools/optimize_h1_resources.py')==proof['encoder_sha256']
    preferred=proof.get('preferred_alpha_pruning')
    if preferred:
        assert sha(ROOT/'tools/prune_preferred_alpha.py')==preferred['encoder_sha256']
        for n,h in preferred['reader_source_sha256'].items():assert sha(ROOT/n)==h,n
    for n,info in proof['removed'].items():
        assert not (base/n).exists(),n
        assert sha(archive/n)==info['baseline_sha256'],n
        assert sha(input_path(base,n))==info['sha256'],n
    for n,info in proof['changed'].items():
        p=input_path(base,n);assert sha(p)==info['sha256'] and p.stat().st_size==info['bytes'],n
    for record in proof['scaled_exceptions']:
        w,h=record['logical'];sw,sh=record['storage'];assert sw<=w and sh<=h and (sw<w or sh<h)
        for x,y,rw,rh in record['rectangles']:
            assert (x+rw)*sw//w>x*sw//w and (y+rh)*sh//h>y*sh//h,record['path']
    assert not any('alphagrid' in n.attrib for g in ET.parse(base/'properties/resources.xml').getroot() for n in g)
    infos={p.relative_to(base).as_posix():read_image(p)[0] for p in base.rglob('*') if p.suffix in ('.h1i','.h1a')}
    unchanged=[n for n,i in infos.items() if (i['width'],i['height'])==(i['storage_width'],i['storage_height']) and (i['width'],i['height'])!=(1,1)]
    assert not unchanged,unchanged
    for n,i in proof['virtual_images'].items():
        assert (base/i['backing']).is_file() and not (base/n).exists()
        assert read_image(input_path(base,n))[0]==i['info'];infos[n]=i['info']
    from optimize_h1_resources import metadata
    assert (base/'image-metadata.h1x').read_bytes()==metadata(infos)
    assert not any(p.suffix in ('.ogg','.mo3','.reanim','.json','.sh') for p in base.rglob('*') if p.is_file())
    files=[p for p in base.rglob('*') if p.is_file()];size=sum(p.stat().st_size for p in files)
    return {'passed':True,'runtime_bytes':size,'runtime_files':len(files),'baseline_bytes':proof['baseline_bytes'],'saved_bytes':proof['baseline_bytes']-size,'removed_files':len(proof['removed']),'completed_alpha_grids':len(proof['alpha_compositions']),'scaled_exception_images':len(proof['scaled_exceptions']),'nonpreferred_RGB_shadows_removed':preferred['raw_images_removed'] if preferred else 0,'unscaled_nontrivial_standalone_images':0,'source_inputs_retained':True,'logical_geometry_unchanged':True}
if __name__=='__main__':print(json.dumps(verify(),indent=2))
