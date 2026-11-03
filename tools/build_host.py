"""Build the complete pinned PvZ-Portable game with the H1 platform on a host.

All Lawn, widget, resource, animation and save code is real upstream code.
Only the hardware I/O services are replaced with a host display/input/PCM sink.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor,as_completed
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import re
from prepare import prepare,COMMIT

ROOT=Path(__file__).resolve().parents[1]
SOURCE_DIRS=('','SexyAppFramework','SexyAppFramework/graphics','SexyAppFramework/imagelib',
 'SexyAppFramework/misc','SexyAppFramework/paklib','SexyAppFramework/sound',
 'SexyAppFramework/widget','SexyAppFramework/fcaseopen','PvzpLib','Lawn','Lawn/Widget','Lawn/System')
ZLIB_NAMES='adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil'.split()

def execute(command):
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True,encoding='utf8',errors='replace')
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return result.stdout

def dependencies_hash(path,fallback):
    if not path.exists():return fallback
    content=path.read_text(encoding='utf8',errors='replace').replace('\\\n',' ')
    if ': ' not in content:return fallback
    body=content.split(': ',1)[1]
    names=re.findall(r'(?:\\.|[^\s])+',body)
    digest=hashlib.sha256();count=0
    for name in names:
        file=Path(name.replace('\\ ', ' ').replace('\\:',':').replace('\\\\','\\'))
        if file.is_file():digest.update(str(file).encode());digest.update(file.read_bytes());count+=1
    return digest.digest() if count else fallback

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cxx',default=shutil.which('g++') or 'g++')
    ap.add_argument('--cc',default=shutil.which('gcc') or 'gcc')
    ap.add_argument('--jobs',type=int,default=4)
    ap.add_argument('--compile-only',action='store_true')
    ap.add_argument('--build-dir',type=Path,default=ROOT/'build/host',help='Isolated host objects and prepared-source directory')
    ap.add_argument('--no-prepare',action='store_true')
    ap.add_argument('--only',nargs='+')
    ap.add_argument('--run',action='store_true')
    ap.add_argument('--frames',type=int,default=300)
    ap.add_argument('--script',type=Path)
    ap.add_argument('--resources',type=Path,default=ROOT/'private/resources')
    ap.add_argument('--save',type=Path,default=ROOT/'build/host/save')
    ap.add_argument('--output',type=Path,default=ROOT/'build/host/run')
    ap.add_argument('--trace-pixel',help='Host-only renderer pixel trace, e.g. 700,2')
    ap.add_argument('--autoplay-first',action='store_true',help='Read real first-level entities and send ordinary clicks')
    ap.add_argument('--native-loading-progress',action='store_true',help='Exercise the native single-thread loading UI pump')
    ap.add_argument('--physical-screen',action='store_true',help='Use a 360x270 screen backing store with unchanged 800x600 logical game coordinates')
    ap.add_argument('--widescreen',action='store_true',help='Use the 480x272 raster and 1060x600 scene layout; implies physical-screen')
    ap.add_argument('--font-probe',action='store_true',help='Compare actual font define, glyph and rendered pixel data before/after AST compaction')
    ap.add_argument('--widget-probe',action='store_true',help='Test completed-widget epochs with original manager, pixels and fonts')
    ap.add_argument('--triangle-probe',action='store_true',help='Test original selector triangle and new-atlas epoch boundaries')
    ap.add_argument('--selector-probe',action='store_true',help='Test full original NO_ATLAS selector Reanimation matrix path')
    ap.add_argument('--atlas-entry-probe',action='store_true',help='Test root-only pre-draw unused atlas collection, holder guards and original pixel rebuild')
    ap.add_argument('--allocator-probe',action='store_true',help='Compare real plant pixels and portable v4 bytes around empty global-pool trim')
    ap.add_argument('--trim-unused-atlases',action='store_true',help='Audit unused reanimation atlas reclamation at complete frames')
    ap.add_argument('--stop-after-first-win',action='store_true',help='Close the host window after naturally reaching level 2')
    ap.add_argument('--disable-large-image-priority',action='store_true',help='Host-only comparison against plain completed-frame LRU')
    ap.add_argument('--speed',type=float,default=1.0)
    args=ap.parse_args()
    os.environ['PATH']=str(Path(args.cxx).resolve().parent)+os.pathsep+os.environ.get('PATH','')
    build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    host_source=build/'source'
    if args.no_prepare:
        source=host_source
        if not source.exists():raise SystemExit('No isolated host source tree; run once without --no-prepare')
        import importlib.util
        spec=importlib.util.spec_from_file_location('patches',ROOT/'platform/source_patches.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        replacements=module.REPLACEMENTS
    else:source,replacements=prepare(host_source)
    sources=[]
    for directory in SOURCE_DIRS:
        for path in sorted((source/directory).glob('*')):
            if path.suffix not in ('.cpp','.c'):continue
            name=path.relative_to(source).as_posix()
            replacement=replacements.get(name,name)
            if replacement is None:continue
            sources.append(ROOT/replacement if name in replacements else path)
    for path in sorted((ROOT/'platform').rglob('*.cpp')):
        if 'tests' in path.parts:continue
        if path.name in ('h1_host.cpp','h1_pcm.cpp'):continue
        sources.append(path)
    sources.extend(path for path in sorted((ROOT/'platform').rglob('*.c')) if 'tests' not in path.parts)
    music=json.loads((ROOT/'platform/music_sources.json').read_text())
    sources.extend(ROOT/name for name in music['sources'])
    zlib=ROOT/'third_party/zlib-1.3.1'
    sources.extend(zlib/(name+'.c') for name in ZLIB_NAMES)
    sources=list(dict.fromkeys(sources))
    if args.only:sources=[p for p in sources if any(q.lower() in p.as_posix().lower() for q in args.only)]
    includes=[ROOT/'platform/host/include',ROOT/'platform/include',ROOT/'platform',
              ROOT/'platform/graphics',ROOT/'platform/resources',source,source/'SexyAppFramework',zlib]
    includes.extend(ROOT/name for name in music['includes'])
    common=['-O1','-g','-fno-omit-frame-pointer','-ffunction-sections','-fdata-sections',
            '-fsigned-char','-fno-strict-aliasing','-DH1_PVZ_PORTABLE','-DLOW_MEMORY','-DPVZ_HOST_RUNNER']
    if args.native_loading_progress:common.append('-DPVZ_NATIVE_LOADING_PROGRESS')
    if args.physical_screen:common.append('-DPVZH1_PHYSICAL_SCREEN')
    if args.widescreen:common+=['-DPVZH1_PHYSICAL_SCREEN','-DPVZH1_WIDESCREEN']
    common+=['-D'+name for name in music['defines']]
    common+=[item for path in includes for item in ('-I',str(path))]
    headers=hashlib.sha256()
    for directory in (source,ROOT/'platform',ROOT/'vendor/libxmp/include',ROOT/'vendor/libxmp/src'):
        for path in sorted(directory.rglob('*')):
            if 'tests' in path.parts:continue
            if path.is_file() and path.suffix in ('.h','.inl','.inc'):headers.update(path.read_bytes())
    header_digest=headers.digest()
    records=[];objects=[];started=time.time()
    def compile_one(path):
        relative=path.relative_to(ROOT).as_posix()
        tag=hashlib.sha1(relative.encode()).hexdigest()[:10]
        obj=build/(tag+'-'+path.stem+'.o')
        dep=obj.with_suffix('.d')
        compiler=args.cxx if path.suffix=='.cpp' else args.cc
        flags=['-std=c++20'] if path.suffix=='.cpp' else ['-std=gnu99']
        command=[compiler,*common,*flags,'-MMD','-MF',dep,'-c',path,'-o',obj]
        key=hashlib.sha256(str(command).encode()+path.read_bytes()+dependencies_hash(dep,header_digest)).hexdigest()
        stamp=obj.with_suffix('.sha256')
        try:
            cached=obj.exists() and stamp.exists() and stamp.read_text()==key
            if not cached:
                execute(command)
                key=hashlib.sha256(str(command).encode()+path.read_bytes()+dependencies_hash(dep,header_digest)).hexdigest()
                stamp.write_text(key)
            return {'source':relative,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'ok':True,'cached':cached},obj
        except Exception as error:
            log=obj.with_suffix('.error.log');log.write_text(str(error),encoding='utf8')
            return {'source':relative,'ok':False,'error_log':str(log),'error':str(error)[:20000]},None
    with ThreadPoolExecutor(max_workers=max(1,args.jobs)) as pool:
        tasks=[pool.submit(compile_one,path) for path in sources]
        for i,future in enumerate(as_completed(tasks),1):
            record,obj=future.result();records.append(record)
            if obj:objects.append(obj)
            else:print('FAIL '+record['source']+': '+record['error'].splitlines()[0],flush=True)
            if i%20==0 or i==len(sources):print(f'Compiled {i}/{len(sources)}; failures {sum(not r["ok"] for r in records)}',flush=True)
    report={'upstream_commit':COMMIT,'target':'host integration of real complete game',
            'source_count':len(sources),'compiled':sum(r['ok'] for r in records),'failed':sum(not r['ok'] for r in records),
            'linked':False,'records':sorted(records,key=lambda r:r['source']),'flags':common,
            'elapsed_seconds':round(time.time()-started,3)}
    report_path=build/'compile-report.json';report_path.write_text(json.dumps(report,indent=2)+'\n')
    if report['failed']:raise SystemExit(1)
    if args.compile_only or args.only:return
    exe=build/('pvz-portable-host.exe' if os.name=='nt' else 'pvz-portable-host')
    command=[args.cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects]
    if os.name=='nt':command+=['-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi']
    try:execute(command)
    except Exception as error:
        (build/'link.error.log').write_text(str(error),encoding='utf8');raise
    report.update(linked=True,executable=str(exe),executable_bytes=exe.stat().st_size,elapsed_seconds=round(time.time()-started,3))
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print('LINKED '+str(exe),flush=True)
    if args.run:
        args.output.mkdir(parents=True,exist_ok=True);args.save.mkdir(parents=True,exist_ok=True)
        prepared=build/'prepared.json'
        metadata={'upstream_commit':COMMIT,'target':'host-only complete original game',
                  'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
                  'prepared_manifest_sha256':hashlib.sha256(prepared.read_bytes()).hexdigest(),
                  'compile_report_sha256':hashlib.sha256(report_path.read_bytes()).hexdigest(),
                  'frames_limit':args.frames,'clock_speed':args.speed,
                  'native_loading_progress':args.native_loading_progress,
                  'physical_screen':args.physical_screen or args.widescreen,
                  'widescreen':args.widescreen,
                  'font_probe':args.font_probe,
                  'atlas_entry_probe':args.atlas_entry_probe,
                  'allocator_probe':args.allocator_probe,
                  'widget_probe':args.widget_probe,
                  'triangle_probe':args.triangle_probe,
                  'selector_probe':args.selector_probe,
                  'trim_unused_atlases':args.trim_unused_atlases,
                  'stop_after_first_win':args.stop_after_first_win,
                  'prefer_large_images':not args.disable_large_image_priority,
                  'read_only_entity_input_controller':args.autoplay_first,
                  'compiled_cache_files_before':len(list(args.save.rglob('*.compiled'))),
                  'script_sha256':hashlib.sha256(args.script.read_bytes()).hexdigest() if args.script else None,
                  'resource_directory':str(args.resources.resolve()),'save_directory':str(args.save.resolve())}
        shutil.copyfile(prepared,args.output/'prepared.json')
        shutil.copyfile(report_path,args.output/'compile-report.json')
        if args.script:shutil.copyfile(args.script,args.output/'input.script')
        (args.output/'run-metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
        env=os.environ.copy()
        env['PATH']=str(Path(args.cxx).resolve().parent)+os.pathsep+env.get('PATH','')
        env.update(PVZ_RESOURCE_DIR=args.resources.resolve().as_posix()+'/',PVZ_SAVE_DIR=args.save.resolve().as_posix()+'/',
                   PVZ_HOST_OUTPUT=args.output.resolve().as_posix(),PVZ_HOST_MAX_FRAMES=str(args.frames))
        if args.script:env['PVZ_HOST_SCRIPT']=str(args.script.resolve())
        if args.trace_pixel:env['PVZ_HOST_TRACE_PIXEL']=args.trace_pixel
        if args.autoplay_first:env['PVZ_HOST_AUTOPLAY']='1'
        if args.font_probe:env['PVZ_HOST_FONT_PROBE']='1'
        if args.widget_probe:env['PVZ_HOST_WIDGET_PROBE']='1'
        if args.triangle_probe:env['PVZ_HOST_TRIANGLE_PROBE']='1'
        if args.selector_probe:env['PVZ_HOST_SELECTOR_PROBE']='1'
        if args.atlas_entry_probe:env['PVZ_HOST_ATLAS_ENTRY_PROBE']='1'
        if args.allocator_probe:env['PVZ_HOST_ALLOCATOR_PROBE']='1'
        if args.trim_unused_atlases:env['PVZ_HOST_TRIM_ATLASES']='1'
        if args.stop_after_first_win:env['PVZ_HOST_STOP_AFTER_WIN']='1'
        env['PVZ_HOST_SPEED']=str(args.speed)
        env['PVZ_HOST_CACHE_PREFER_LARGE']='0' if args.disable_large_image_priority else '1'
        result=subprocess.run([str(exe)],env=env,cwd=ROOT)
        save_files={path.name:{'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
                    for path in (args.save/'userdata').glob('*') if path.is_file()}
        (args.output/'save-manifest-after-exit.json').write_text(json.dumps(save_files,indent=2)+'\n')
        metadata['exit_code']=result.returncode
        metadata['compiled_cache_files_after']=len(list(args.save.rglob('*.compiled')))
        (args.output/'run-metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
        print('Host process exit='+str(result.returncode),flush=True)
        raise SystemExit(result.returncode)

if __name__=='__main__':main()
