"""Checked H1-only registration/pragma patch, reverse and idempotence proof."""
from pathlib import Path
import hashlib,importlib.util,json,re,shutil
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 patch=ROOT/'platform/graphics/raster_codegen_patches.py'
 spec=importlib.util.spec_from_file_location('hot32',patch);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 output=ROOT/'build/hot32-patch-test';output.mkdir(exist_ok=True)
 names=['SexyAppFramework/graphics/SWTri.cpp','PvzpLib/PvzpCommon.cpp','SexyAppFramework/graphics/SWTri/SWTri_DrawTriangle.inc']
 reports=[]
 for label,base in [('pinned',ROOT/'upstream/src'),('frozen-plan',ROOT/'build/native-hardware-opt-plan/source')]:
  out=output/label;original={}
  for name in names:
   p=out/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(base/name,p);original[name]=p.read_text()
  m.apply(out);first={name:(out/name).read_text() for name in names};m.apply(out)
  assert first=={name:(out/name).read_text() for name in names}
  # Removing only inserted conditional/preprocessor text restores every
  # non-H1 statement. H1 kernel implementations are not rewritten either.
  for name in names[:2]:
   text=re.sub(r'^#ifndef H1_PVZ_PORTABLE // PVZH1_UNREACHABLE_[A-Z0-9_]+\n','',first[name],flags=re.M)
   text=re.sub(r'^#endif // PVZH1_UNREACHABLE_[A-Z0-9_]+\n','',text,flags=re.M)
   assert text==original[name],name
  inc=first[names[2]];start=inc.index('#endif\n')+len('#endif\n');end=inc.rindex('\n#if defined(H1_PVZ_PORTABLE)')
  assert inc[start:end]==original[names[2]]
  before={name:sha(out/name) for name in names};(out/names[0]).write_text((out/names[0]).read_text().replace('DrawTriangle_0565_TEX0_TALPHA0_MOD0_GLOB0_BLEND0','BAD_FORMAT_ANCHOR',1))
  try:m.apply(out)
  except RuntimeError:pass
  else:raise AssertionError('Changed registration count was silently accepted')
  (out/names[0]).write_text(first[names[0]])
  reports.append({'source':label,'passed':True,'idempotent':True,'reverse_nonH1_statement_equality':True,'changed_anchor_rejected':True,'prepared_sha256':before})
 result={'passed':True,'patch_sha256':sha(patch),'source_patches_sha256':sha(ROOT/'platform/source_patches.py'),'cases':reports,'scope':'Patch guards/unchanged statements only. Pixel behavior verified by separate actual renderer oracle.'}
 (output/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
