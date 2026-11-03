"""Strict MIPS compile and normal/custom shader isolation for render plans."""
from pathlib import Path
import hashlib,importlib.util,json,re,shutil,subprocess
from concurrent.futures import ThreadPoolExecutor
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 base=ROOT/'build/native-hardware-opt-raster';out=ROOT/'build/render-plan-mips';out.mkdir(exist_ok=True);report=json.loads((base/'compile-report.json').read_text());assert report['failed']==0
 shutil.copytree(base/'source',out/'source',dirs_exist_ok=True);spec=importlib.util.spec_from_file_location('p',ROOT/'platform/graphics/reduced_texture_patches.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);p.apply(out/'source')
 flags=[f.replace(str(base/'source'),str(out/'source')).replace((base/'source').as_posix(),(out/'source').as_posix()) for f in report['flags']];tools=ROOT.parent/'.tools/toolchain/bin';cxx=tools/'mipsel-none-elf-g++.exe';dump=tools/'mipsel-none-elf-objdump.exe'
 def size(path):
  text=subprocess.check_output([str(dump),'-h',str(path)],text=True);return sum(int(m.group(1),16) for m in re.finditer(r'^\s*\d+\s+\.text(?:\.[^\s]*)?\s+([0-9a-f]+)',text,re.M))
 def compile(pair):
  name,src=pair;obj=out/(name+'.o');r=subprocess.run([str(cxx),*flags,'-c',str(src),'-o',str(obj)],capture_output=True,text=True);(out/(name+'.compile.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr
  rel=subprocess.check_output([str(dump),'-r','-C',str(obj)],text=True);(out/(name+'.relocations.txt')).write_text(rel);old=next((Path(row['object']) for row in report['records'] if Path(row['source']).name==src.name),None)
  return dict(name=name,source_sha256=sha(src),object_sha256=sha(obj),text_bytes=size(obj),old_text_bytes=size(old) if old else 0,record_calls=rel.count('R_MIPS_26         Sexy::H1RenderPlanRecordSpan'),span_calls=rel.count('R_MIPS_26         Sexy::H1FastReducedSpan'))
 sources=[('SWTri',out/'source/SexyAppFramework/graphics/SWTri.cpp'),('EffectSystem',out/'source/PvzpLib/EffectSystem.cpp'),('SoftwareRaster',ROOT/'platform/graphics/SoftwareRaster.cpp'),('RenderPlanCache',ROOT/'platform/graphics/RenderPlanCache.cpp')]
 with ThreadPoolExecutor(max_workers=4) as pool:records=list(pool.map(compile,sources))
 assert records[0]['record_calls']>0 and records[1]['record_calls']==0 and records[1]['span_calls']==0
 result=dict(passed=True,strict_production_flags=True,EffectSystem_custom_shader_plan_calls=0,unlinked_text_delta=sum(r['text_bytes']-r['old_text_bytes'] for r in records),records=records,inputs={str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'platform/graphics/RenderPlanCache.cpp',ROOT/'platform/graphics/RenderPlanCache.h',ROOT/'platform/graphics/SoftwareRaster.cpp',ROOT/'platform/graphics/reduced_texture_patches.py')});(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
