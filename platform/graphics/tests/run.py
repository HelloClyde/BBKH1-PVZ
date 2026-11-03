"""Compile unchanged upstream software drawing plus H1 replacements on a host.
The app/resource objects are isolated unit-test fixtures, not a game runtime.
"""
from pathlib import Path
import os
import shutil
import subprocess
import importlib.util
import hashlib,json

GRAPHICS = Path(__file__).resolve().parents[1]
ROOT = GRAPHICS.parents[1]
UP = ROOT / 'upstream/src/SexyAppFramework'
BUILD = ROOT / 'build/graphics-tests'
BUILD.mkdir(parents=True,exist_ok=True)
cc = os.environ.get('CXX') or shutil.which('g++') or 'g++'
os.environ['PATH']=str(Path(cc).resolve().parent)+os.pathsep+os.environ.get('PATH','')
compiler_stamp=BUILD/'compiler.txt'
compiler_changed=not compiler_stamp.exists() or compiler_stamp.read_text()!=cc
sources = [GRAPHICS / s for s in ('GLImage.cpp','GLInterface.cpp','SoftwareRaster.cpp','MemoryImageCache.cpp',
                                'PhysicalScreen.cpp','RenderPlanCache.cpp','TextReplay.cpp')]
sources += [UP / 'graphics' / s for s in ('Image.cpp','NativeDisplay.cpp','Quantize.cpp','Color.cpp','Font.cpp')]
sources += [UP / 'misc/SexyMatrix.cpp',GRAPHICS / 'tests/test_software.cpp']
spec=importlib.util.spec_from_file_location('memory_patches',GRAPHICS/'memory_patches.py')
patches=importlib.util.module_from_spec(spec);spec.loader.exec_module(patches)
generated=BUILD/'generated/MemoryImage.cpp';generated.parent.mkdir(exist_ok=True)
# This isolated raster test needs no Win32 service declarations. Omitting the
# unused Windows headers avoids GCC 16 intrinsic-header ICEs with -include;
# the real complete-game host build still uses its original Common.h.
common=(UP/'Common.h').read_text(encoding='utf8')
windows_headers='#include <windows.h>\n#include <shellapi.h>\n#include <mmsystem.h>'
if common.count(windows_headers)!=1:raise RuntimeError('Pinned Common.h Windows include block changed')
common=common.replace(windows_headers,'// Unit fixture: Win32 service headers unused by software raster tests.',1)
common_header=BUILD/'generated/Common.h'
if not common_header.exists() or common_header.read_text(encoding='utf8')!=common:common_header.write_text(common,encoding='utf8')
generated_header=BUILD/'generated/MemoryImage.h'
if not generated_header.exists() or generated_header.stat().st_mtime < max((UP/'graphics/MemoryImage.h').stat().st_mtime,(GRAPHICS/'memory_patches.py').stat().st_mtime):
    generated_header.write_text((UP/'graphics/MemoryImage.h').read_text(encoding='utf-8'),encoding='utf-8')
    patches.patch_memory_header(generated_header)
original=(UP/'graphics/MemoryImage.cpp').read_text(encoding='utf-8')
if not generated.exists() or generated.stat().st_mtime < max((UP/'graphics/MemoryImage.cpp').stat().st_mtime,(GRAPHICS/'memory_patches.py').stat().st_mtime):
    generated.write_text(original,encoding='utf-8');patches.patch_memory(generated);patches.patch_memory_raster(generated);patches.patch_memory_pressure(generated)
sources += [generated,ROOT/'platform/resources/h1_resources.c']
generated_graphics=BUILD/'generated/Graphics.cpp'
if not generated_graphics.exists() or generated_graphics.stat().st_mtime < max((UP/'graphics/Graphics.cpp').stat().st_mtime,(GRAPHICS/'memory_patches.py').stat().st_mtime):
    generated_graphics.write_text((UP/'graphics/Graphics.cpp').read_text(encoding='utf-8'),encoding='utf-8')
    patches.patch_graphics_scratch(generated_graphics)
sources.append(generated_graphics)
generated_tri=BUILD/'generated/SWTri.cpp'
if not generated_tri.exists() or generated_tri.stat().st_mtime < max((UP/'graphics/SWTri.cpp').stat().st_mtime,(GRAPHICS/'memory_patches.py').stat().st_mtime):
    generated_tri.write_text((UP/'graphics/SWTri.cpp').read_text(encoding='utf-8'),encoding='utf-8')
    shutil.copytree(UP/'graphics/SWTri',generated_tri.parent/'SWTri',dirs_exist_ok=True)
    for name in ('SWTri_Pixel8888.inc','SWTri_Pixel888.inc'):
        patches.patch_triangle_zero_alpha(generated_tri.parent/'SWTri'/name)
sources.append(generated_tri)

generated_helpers=BUILD/'generated/ApplicationImageHelpers.cpp'
if not generated_helpers.exists() or generated_helpers.stat().st_mtime < max((UP/'SexyAppBase.cpp').stat().st_mtime,(GRAPHICS/'memory_patches.py').stat().st_mtime):
    app_copy=BUILD/'generated/ApplicationPressureSource.cpp'
    app_copy.write_text((UP/'SexyAppBase.cpp').read_text(encoding='utf8'),encoding='utf8')
    patches.patch_app(app_copy);patches.patch_app_pressure(app_copy)
    app_text=app_copy.read_text(encoding='utf8')
    bodies=[]
    for signature in ('void SexyAppBase::ColorizeImage(', 'GLImage* SexyAppBase::CreateColorizedImage('):
        start=app_text.index(signature);left=app_text.index('{',start);depth=1;end=left+1
        while depth:
            if app_text[end]=='{':depth+=1
            elif app_text[end]=='}':depth-=1
            end+=1
        bodies.append(app_text[start:end])
    generated_helpers.write_text('#include "SexyAppBase.h"\n#include "graphics/GLImage.h"\n#include "graphics/MemoryImageCache.h"\nusing namespace Sexy;\n'+'\n'.join(bodies),encoding='utf8')
sources.append(generated_helpers)
exe = BUILD / ('test_software.exe' if os.name=='nt' else 'test_software')
flags = ['-std=c++20','-O1','-g','-ffunction-sections','-fdata-sections',
         '-I'+str(GRAPHICS/'tests/support'),'-I'+str(BUILD/'generated'),'-I'+str(ROOT/'platform'),
         '-I'+str(ROOT/'platform/resources'),'-I'+str(UP),'-I'+str(UP/'graphics'),'-include',str(generated_header)]
headers = [p for directory in (GRAPHICS,UP/'graphics',UP/'misc')
           for p in directory.rglob('*') if p.suffix in ('.h','.inc')]
header_time = max([p.stat().st_mtime for p in headers]+[generated_header.stat().st_mtime])
objects=[]
for index, source in enumerate(sources):
    obj=BUILD/(str(index)+'-'+source.stem+'.o')
    if compiler_changed or not obj.exists() or obj.stat().st_mtime < max(source.stat().st_mtime,header_time):
        print('Compile '+source.name,flush=True)
        subprocess.run([cc]+flags+['-c',str(source),'-o',str(obj)],check=True)
    objects.append(obj)
subprocess.run([cc,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc']+list(map(str,objects))+['-o',str(exe)],check=True)
compiler_stamp.write_text(cc)
env=os.environ.copy()
env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
subprocess.run([str(exe)],check=True,env=env,cwd=BUILD)
report={'target':'Isolated host original MemoryImage/SWTri/Graphics regression, not firmware or physical H1',
    'passed':True,'compiler':cc,'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
    'source_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
        (GRAPHICS/'MemoryImageCache.cpp',GRAPHICS/'memory_patches.py',GRAPHICS/'SoftwareRaster.cpp',GRAPHICS/'tests/test_software.cpp')},
    'allocation_pressure_cases':['uninitialized callback does not allocate','no allocations while reclaiming',
        'one eligible source per callback','cold sources before current-physical-frame completed sources',
        'smallest actual backing-buffer fit before LRU fallback','sum of disjoint buffers does not count as contiguous fit',
        '20-byte request retains large backgrounds when a small eligible source suffices',
        'volatile native source-cache counters match existing recount/frame/pressure statistics',
        'all eligible old-frame sources reclaimed','current GetBits/native/RL pointers readable',
        'dynamic pixels preserved','old sources reload exact original pixels','recursive callback returns zero',
        'draw-batch epoch callback allocates nothing, evicts nothing and does not present',
        'completed-batch sources reclaimable while current-batch pointers remain readable',
        'target pinned during alpha-row allocation','loaded target pinned during direct alpha update',
        'raw palette source pinned before destination allocation','original CreateColorizedImage source pinned before allocation'],
    'fixture_note':'Surrounding app/image fallback are test fixtures. The original color helper bodies are extracted from patched upstream. Unused Win32 service headers are omitted only in the isolated Common.h overlay to avoid GCC intrinsic-header ICEs.'}
(BUILD/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
