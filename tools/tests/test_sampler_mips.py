"""Strict production -Os sampler code-size and shader-isolation check."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, re, shutil, subprocess
from concurrent.futures import ThreadPoolExecutor
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--base',type=Path,default=Path('build/native-hardware-opt-fast3'));args=ap.parse_args()
    base=(ROOT/args.base).resolve();out=ROOT/'build/sampler-mips';out.mkdir(exist_ok=True)
    report=json.loads((base/'compile-report.json').read_text());assert report['failed']==0
    shutil.copytree(base/'source',out/'source',dirs_exist_ok=True)
    spec=importlib.util.spec_from_file_location('p',ROOT/'platform/graphics/reduced_texture_patches.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);p.apply(out/'source')
    flags=[flag.replace(str(base/'source'),str(out/'source')).replace((base/'source').as_posix(),(out/'source').as_posix()) for flag in report['flags']]
    toolchain=ROOT.parent/'.tools/toolchain/bin';cxx=toolchain/'mipsel-none-elf-g++.exe';dump=toolchain/'mipsel-none-elf-objdump.exe'
    def sections(path):
        value=subprocess.check_output([str(dump),'-h',str(path)],text=True)
        return sum(int(m.group(1),16) for m in re.finditer(r'^\s*\d+\s+\.text(?:\.[^\s]*)?\s+([0-9a-f]+)',value,re.M))
    def compile(pair):
        name,src=pair;obj=out/(name+'.o');r=subprocess.run([str(cxx),*flags,'-c',str(src),'-o',str(obj)],text=True,capture_output=True);(out/(name+'.compile.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
        rel=subprocess.check_output([str(dump),'-r','-C',str(obj)],text=True);(out/(name+'.relocations.txt')).write_text(rel)
        original=next(Path(row['object']) for row in report['records'] if Path(row['source']).name==src.name)
        return dict(name=name,source_sha256=sha(src),object_sha256=sha(obj),text_bytes=sections(obj),original_text_bytes=sections(original),fast_span_references=rel.count('R_MIPS_26         Sexy::H1FastReducedSpan'),relocations=rel)
    sources=[('SWTri',out/'source/SexyAppFramework/graphics/SWTri.cpp'),('SoftwareRaster',ROOT/'platform/graphics/SoftwareRaster.cpp'),('EffectSystem',out/'source/PvzpLib/EffectSystem.cpp')]
    with ThreadPoolExecutor(max_workers=3) as pool:records=list(pool.map(compile,sources))
    swtri=records[0]['relocations'];effect=records[2]['relocations'];normal=[];oldhelper=[]
    for part in swtri.split('RELOCATION RECORDS FOR [')[1:]:
        name=part.split(']:',1)[0]
        if '.text.' in name and 'DrawTriangle_0888_TEX1_' in name:
            normal.append(name)
            if re.search(r'R_MIPS_26\s+.*(?:ReadClamped|Read\(|FixedFloor)',part):oldhelper.append(name)
    assert normal and not oldhelper,oldhelper
    assert not records[2]['fast_span_references'],'Effect custom shader must not use normal row span'
    assert 'ReadClamped' in effect and 'FixedFloor' in effect,'Effect original sampler must remain'
    delta=sum(row['text_bytes']-row['original_text_bytes'] for row in records);assert delta<60*1024,delta
    for row in records:row.pop('relocations')
    result=dict(passed=True,production_compiler_flags_sha256=hashlib.sha256('\n'.join(flags).encode()).hexdigest(),normal_screen_textured_variants=len(normal),normal_screen_out_of_line_fetch_or_floor_calls=0,effect_custom_shader_original_sampler=True,unlinked_text_delta_bytes=delta,within_60KiB_delta_budget=True,records=records,production_inputs={str(x.relative_to(ROOT)):sha(x) for x in (ROOT/'platform/graphics/reduced_texture_patches.py',ROOT/'platform/graphics/SoftwareRaster.cpp')})
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
