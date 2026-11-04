"""Compile pinned PvZ-Portable business sources and the native H1 backends.

Compilation reports distinguish real compiled sources from a linked/runnable
artifact. A BDA is only emitted after every source and the complete link pass.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor,as_completed
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from build_paths import SDK, TOOLCHAIN, RUNTIME_PREFIX
from bootstrap_deps import prepare_zlib
from prepare import prepare,COMMIT

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
SOURCE_DIRS=('','SexyAppFramework','SexyAppFramework/graphics','SexyAppFramework/imagelib',
    'SexyAppFramework/misc','SexyAppFramework/paklib','SexyAppFramework/sound','SexyAppFramework/widget',
    'SexyAppFramework/fcaseopen','PvzpLib','Lawn','Lawn/Widget','Lawn/System')
ZLIB_SOURCES='adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil'.split()

def run(command):
    result=subprocess.run([str(x) for x in command],capture_output=True,text=True,encoding='utf8',errors='replace')
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return result.stdout

def sha256_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def input_name(path,prefix):
    for base,label in ((ROOT,''),(WORKSPACE,'../'),(prefix,'cxx-runtime/')):
        try:return label+path.relative_to(base).as_posix()
        except ValueError:pass
    return path.as_posix()

def header_files(source,zlib,prefix):
    files=set()
    directories=(source,ROOT/'platform',ROOT/'runtime',zlib,ROOT/'vendor/libxmp/include',
                 ROOT/'vendor/libxmp/src',SDK/'sdk/include',prefix/'mipsel-none-elf/include')
    for directory in directories:
        for path in directory.rglob('*'):
            if {'host','tests'}.intersection(path.parts):continue
            if path.is_file() and (directory==prefix/'mipsel-none-elf/include' or
                path.suffix in ('.h','.inl','.inc') or path.name in ('mutex','thread')):files.add(path)
    return sorted(files)

def source_files(source,replacements,zlib,numeric_replacements=None):
    sources=[]
    for relative in SOURCE_DIRS:
        for path in sorted((source/relative).glob('*')):
            if path.suffix not in ('.cpp','.c'):continue
            name=path.relative_to(source).as_posix()
            if name in replacements:
                replacement=replacements[name]
                if replacement:sources.append(ROOT/replacement)
            else:sources.append(path)
    for path in sorted((ROOT/'platform').rglob('*')):
        if path.suffix in ('.c','.cpp') and not {'host','tests'}.intersection(path.parts) and path.name not in ('host_mock.cpp','host_main.cpp'):sources.append(path)
    sources.extend(sorted((ROOT/'runtime').glob('*.c')))
    sources.extend(sorted((ROOT/'runtime').glob('*.cpp')))
    sources.append(ROOT/'runtime/entry.S')
    sources.extend(zlib/(name+'.c') for name in ZLIB_SOURCES)
    music=ROOT/'platform/music_sources.json'
    if music.exists():
        numeric_replacements=numeric_replacements or {}
        sources.extend(numeric_replacements.get(ROOT/name,ROOT/name)
                       for name in json.loads(music.read_text(encoding='utf8'))['sources'])
    return list(dict.fromkeys(sources))

def compiler_flags(prefix,zlib,source,lto=False,optimization='Os'):
    if optimization not in ('Os','O2'):
        raise ValueError('Native optimization must be Os or O2')
    cpp=prefix/'mipsel-none-elf/include/c++/15.2.0'
    if not (cpp/'string').exists():cpp=prefix.parent/'build-libstdcxx/include'
    includes=[ROOT/'runtime/include',ROOT/'platform/include',ROOT/'platform',ROOT/'platform/graphics',
              ROOT/'platform/resources',ROOT/'runtime',source,source/'SexyAppFramework',zlib,SDK/'sdk/include']
    music=ROOT/'platform/music_sources.json'
    if music.exists():includes.extend(ROOT/name for name in json.loads(music.read_text(encoding='utf8'))['includes'])
    common=['-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic','-'+optimization,
        '-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections',
        '-fsigned-char','-fno-strict-aliasing','-DH1_PVZ_PORTABLE','-DPVZ_NATIVE_LOADING_PROGRESS','-DLOW_MEMORY','-D_LARGEFILE_SOURCE',
        '-fno-omit-frame-pointer',f'-ffile-prefix-map={ROOT}=h1-pvz-portable',f'-ffile-prefix-map={prefix}=h1-cxx-runtime']
    # Both int and long are 32 bits on o32, but upstream's APIs and STL calls
    # require exact-width game fields to use int as on its supported ports.
    common += ['-U__INT32_TYPE__','-D__INT32_TYPE__=int','-U__UINT32_TYPE__','-D__UINT32_TYPE__=unsigned int']
    if music.exists():common.extend('-D'+value for value in json.loads(music.read_text(encoding='utf8'))['defines'])
    common += [value for directory in includes for value in ('-I',str(directory))]
    if lto:common+=['-flto']
    cppflags=['-std=gnu++20','-fexceptions','-frtti','-fno-threadsafe-statics','-nostdinc++',
              '-fno-freestanding','-isystem',str(cpp),'-isystem',str(cpp/'mipsel-none-elf')]
    if cpp==prefix.parent/'build-libstdcxx/include':
        cppflags+=['-isystem',str(prefix.parent/'gcc-15.2.0/libstdc++-v3/libsupc++')]
    cppflags+=['-isystem',str(prefix/'mipsel-none-elf/include')]
    return common,cppflags

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--prefix',type=Path,default=RUNTIME_PREFIX)
    ap.add_argument('--toolchain',type=Path,default=TOOLCHAIN/'bin')
    ap.add_argument('--kernel',type=Path,help='Recheck the pinned firmware IRQ contract; uses local firmware when available')
    ap.add_argument('--compile-only',action='store_true')
    ap.add_argument('--only',nargs='+',help='Compile only source paths containing one of these strings')
    ap.add_argument('--jobs',type=int,default=6)
    ap.add_argument('--lto',action='store_true')
    ap.add_argument('--optimization',choices=('Os','O2'),default='Os',
                    help='Explicit native profile optimization; default Os. ABI, FP policy and strict arithmetic stay unchanged')
    ap.add_argument('--optimize-source',action='append',default=[],
                    help='Compile one exact source path or unique basename with O2; repeat for a controlled hot set, default others Os')
    ap.add_argument('--xmp-numeric-cache',action='store_true',
                    help='Explicitly apply the pinned exact libxmp period/mixer numerical-cache adaptation')
    ap.add_argument('--no-prepare',action='store_true',help='Use an already prepared immutable build/source tree')
    ap.add_argument('--build-dir',type=Path,help='Isolated project-relative output/prepared directory below build/')
    ap.add_argument('--output-dir',type=Path,help='Project-relative BDA output directory below build/ or dist/')
    ap.add_argument('--define',action='append',default=[],help='Additional compiler macro, e.g. NAME or NAME=value')
    args=ap.parse_args()
    mode='mips-lto' if args.lto else 'mips'
    build=(ROOT/args.build_dir).resolve() if args.build_dir else ROOT/'build'/mode
    out=(ROOT/args.output_dir).resolve() if args.output_dir else ROOT/'dist'
    if not build.is_relative_to((ROOT/'build').resolve()):
        ap.error('--build-dir must stay inside this project build directory')
    if not any(out.is_relative_to((ROOT/name).resolve()) for name in ('build','dist')):
        ap.error('--output-dir must stay inside this project build or dist directory')
    source=build/'source' if args.build_dir else ROOT/'build/source'
    build.mkdir(parents=True,exist_ok=True)
    if args.no_prepare:
        import importlib.util
        spec=importlib.util.spec_from_file_location('pvz_h1_patches',ROOT/'platform/source_patches.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);replacements=module.REPLACEMENTS
    else:source,replacements=prepare(source)
    zlib=prepare_zlib();prefix=args.prefix.resolve();tc=args.toolchain.resolve()
    if not (prefix/'mipsel-none-elf/include/c++/15.2.0/string').exists() and not (args.compile_only and (prefix.parent/'build-libstdcxx/include/string').exists()):
        raise SystemExit('Hosted C++ runtime is not installed; run tools/bootstrap_cxx.py first')
    numeric_replacements={}
    if args.xmp_numeric_cache:
        import importlib.util
        module_path=ROOT/'platform/audio/xmp_numeric_patches.py'
        spec=importlib.util.spec_from_file_location('pvz_h1_xmp_numeric',module_path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        numeric_replacements=module.apply(source)
        expected={ROOT/'vendor/libxmp/src'/name for name in ('period.c','mixer.c','scan.c','load.c')}
        if set(numeric_replacements)!=expected or not all(p.is_file() for p in numeric_replacements.values()):
            raise RuntimeError('Unexpected pinned libxmp numerical adaptation mapping')
    sources=source_files(source,replacements,zlib,numeric_replacements)
    if args.only:sources=[p for p in sources if any(pattern.lower() in p.as_posix().lower() for pattern in args.only)]
    optimized_sources=set()
    if args.optimize_source and args.optimization!='Os':
        ap.error('Use either global --optimization O2 or --optimize-source with default Os')
    for requested in args.optimize_source:
        requested=requested.replace('\\','/').lower()
        matches=[p for p in sources if requested in (p.name.lower(),p.relative_to(ROOT).as_posix().lower(),
                 p.relative_to(source).as_posix().lower() if p.is_relative_to(source) else '')]
        if len(matches)!=1:ap.error('--optimize-source must match exactly one source: '+requested)
        optimized_sources.add(matches[0])
    common,cppflags=compiler_flags(prefix,zlib,source,args.lto,args.optimization)
    if args.xmp_numeric_cache:common+=['-I',str(ROOT/'platform/audio')]
    for define in args.define:
        name=define.split('=',1)[0]
        if not name or not (name[0].isalpha() or name[0]=='_') or not all(c.isascii() and (c.isalnum() or c=='_') for c in name):
            ap.error('--define needs a C macro identifier followed by an optional value')
        common.append('-D'+define)
    header_paths=header_files(source,zlib,prefix)
    header_inputs={input_name(path,prefix):sha256_file(path) for path in header_paths}
    header_hash=hashlib.sha256(json.dumps(header_inputs,sort_keys=True).encode()).digest()
    source_inputs={path.relative_to(ROOT).as_posix():sha256_file(path) for path in sources}
    build_paths=[ROOT/'runtime/h1.ld',ROOT/'assets/icon.png',source.parent/'prepared.json',
                 ROOT/'tools/build.py',ROOT/'tools/prepare.py',ROOT/'tools/bootstrap_cxx.py',
                 ROOT/'tools/build_paths.py',ROOT/'tools/build_widescreen.py',ROOT/'tools/bootstrap_deps.py',ROOT/'tools/audit_gp.py',ROOT/'platform/source_patches.py',
                 ROOT/'tools/test_whole_hole.py',
                 ROOT/'platform/graphics/memory_patches.py',ROOT/'platform/music_sources.json']
    build_paths=list(dict.fromkeys([*build_paths,*numeric_replacements.keys(),
                                   *sorted((ROOT/'platform').rglob('*patches.py'))]))
    build_inputs={input_name(path,prefix):sha256_file(path) for path in build_paths if path.is_file()}
    records=[];objects=[]
    def compile_one(item):
        index,path=item;relative=path.relative_to(ROOT).as_posix();obj=build/(f'{index:03d}-'+path.stem+'.o')
        compiler=tc/('mipsel-none-elf-g++.exe' if path.suffix=='.cpp' else 'mipsel-none-elf-gcc.exe')
        source_common=['-O2' if value=='-Os' else value for value in common] if path in optimized_sources else common
        command=[compiler,*source_common,*(cppflags if path.suffix=='.cpp' else ['-std=gnu11','-isystem',str(prefix/'mipsel-none-elf/include')]),'-c',path,'-o',obj]
        key=hashlib.sha256(str(command).encode()+path.read_bytes()+header_hash).hexdigest();stamp=obj.with_suffix('.sha256')
        try:
            if not obj.exists() or not stamp.exists() or stamp.read_text()!=key:
                run(command);stamp.write_text(key)
            actual=sha256_file(path)
            if actual!=source_inputs[relative]:raise RuntimeError('Source changed during compilation: '+relative)
            return {'source':relative,'object':str(obj),'ok':True,'sha256':actual,
                    'optimization':'O2' if path in optimized_sources else args.optimization},obj
        except Exception as error:
            log=build/(f'{index:03d}-'+path.stem+'.error.log');log.write_text(str(error),encoding='utf8')
            return {'source':relative,'ok':False,'error_log':str(log),'error':str(error)[:12000]},None
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending=[pool.submit(compile_one,item) for item in enumerate(sources)]
        for future in as_completed(pending):
            record,obj=future.result();records.append(record)
            if obj:objects.append(obj)
            else:print('FAIL '+record['source']+': '+record['error'].splitlines()[-1],flush=True)
    report={'upstream_commit':COMMIT,'compiled':sum(r['ok'] for r in records),'failed':sum(not r['ok'] for r in records),
            'source_count':len(sources),'records':sorted(records,key=lambda row:row['source']),'linked':False,
            'flags':common+cppflags,'exception_handling':True,'lto':args.lto,'optimization':args.optimization,
            'source_optimization_overrides':{p.relative_to(ROOT).as_posix():'O2' for p in sorted(optimized_sources)},
            'xmp_numeric_cache':args.xmp_numeric_cache,
            'xmp_numeric_source_replacements':{p.relative_to(ROOT).as_posix():v.relative_to(ROOT).as_posix()
                                               for p,v in numeric_replacements.items()},
            'additional_defines':args.define,'prepared_source':source.relative_to(ROOT).as_posix(),
            'header_inputs':header_inputs,'build_inputs':build_inputs}
    report_path=build/('compile-report-partial.json' if args.only else 'compile-report.json')
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:report[k] for k in ('compiled','failed','source_count','linked')})+' '+str(report_path),flush=True)
    if report['failed']:raise SystemExit(1)
    if args.compile_only or args.only:return
    elf=build/'H1PVZPortable.elf';raw=build/'H1PVZPortable.bin'
    lib=prefix/'mipsel-none-elf/lib'
    command=[tc/'mipsel-none-elf-g++.exe',*common,*cppflags,'-nostdlib','-Wl,--gc-sections','-Wl,--build-id=none',
             '-Wl,-T,'+str(ROOT/'runtime/h1.ld'),'-Wl,-Map,'+str(build/'H1PVZPortable.map'),'-o',elf,*sorted(objects),
             '-L',lib,'-L',prefix/'lib','-Wl,--start-group','-lstdc++','-lsupc++','-lm','-lc','-lgcc','-Wl,--end-group']
    try:run(command)
    except Exception as error:
        (build/'link.error.log').write_text(str(error),encoding='utf8');raise
    symbols={fields[2]:int(fields[0],16) for line in run([tc/'mipsel-none-elf-nm.exe',elf]).splitlines()
             if len(fields:=line.split())==3}
    if symbols['__ctors_end']==symbols['__ctors_start'] and symbols['__init_array_end']==symbols['__init_array_start']:
        raise RuntimeError('The complete C++ application has no retained global initializer table')
    run([tc/'mipsel-none-elf-objcopy.exe','-O','binary',elf,raw])
    report.update(linked=True,elf=str(elf),payload_bytes=raw.stat().st_size,
                  size=run([tc/'mipsel-none-elf-size.exe',elf]))
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    kernel=args.kernel
    audit=[sys.executable,ROOT/'tools/audit_gp.py','--prefix',prefix,'--toolchain',tc,'--report',report_path]
    if kernel is not None:audit+=['--kernel',kernel]
    run(audit)
    report['gp_audit']=json.loads((build/'gp-audit.json').read_text(encoding='utf8'))
    library_paths=[lib/'libc.a',lib/'libm.a',prefix/'lib/libstdc++.a',prefix/'lib/libsupc++.a']
    report['runtime_libraries']={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in library_paths}
    report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    sys.path.insert(0,str(SDK))
    from h1_bda.header import HeaderFields,encode_header
    from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES,build_icon_resources,build_diagnostic_resources
    from h1_bda.validate import validate_bda
    payload=raw.read_bytes();padding=(-(PAYLOAD_OFFSET+len(payload)))&3
    fields=HeaderFields(category=0x48,file_size_minus_4=PAYLOAD_OFFSET+len(payload)+padding-4,payload_offset=PAYLOAD_OFFSET,
                        resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
    icon=ROOT/'assets/icon.png';resources=build_icon_resources(icon) if icon.exists() else build_diagnostic_resources()
    # Parallel host work uses a separate prepared tree. Reject changes to any
    # native inputs while compiling, rather than emitting ambiguous evidence.
    if header_files(source,zlib,prefix)!=header_paths:raise RuntimeError('Native header set changed during build')
    for path in [*sources,*header_paths,*build_paths]:
        name=input_name(path,prefix)
        expected=source_inputs.get(name,header_inputs.get(name,build_inputs.get(name)))
        if expected is not None and (not path.is_file() or sha256_file(path)!=expected):
            raise RuntimeError('Native input changed during build: '+name)
    data=encode_header(fields,title='植物僵尸',build_time='2026-10-31 00:00:00')+resources+payload+bytes(padding)
    out.mkdir(parents=True,exist_ok=True);bda=out/'H1PVZPortable.bda';bda.write_bytes(data)
    validation=validate_bda(bda)
    if not validation['ok']:raise RuntimeError(str(validation['errors']))
    digest=hashlib.sha256(data).hexdigest();(out/'H1PVZPortable.bda.sha256').write_text(digest+'  H1PVZPortable.bda\n')
    meta={'title':'植物僵尸','target':'BBK H1/Y100 V1.41','upstream_commit':COMMIT,'bytes':len(data),'sha256':digest,
          'native_elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'payload_bytes':len(payload),
          'compiled_sources':len(sources),'exception_handling':True,'private_stack_bytes':131072,
          'additional_defines':args.define,'optimization':args.optimization,'prepared_source':source.relative_to(ROOT).as_posix(),
          'source_optimization_overrides':report['source_optimization_overrides'],
          'xmp_numeric_cache':args.xmp_numeric_cache,
          'xmp_numeric_source_replacements':report['xmp_numeric_source_replacements'],
          'entry':'0x83c00020','address_limit':'0x83ff0000','gp_audit':report['gp_audit'],
          'runtime_libraries':report['runtime_libraries'],'bda_validation':validation,'hardware_verified':False,
          'source_inputs':source_inputs,'header_inputs':header_inputs,'build_inputs':build_inputs,
          'icon_sha256':sha256_file(icon) if icon.exists() else None}
    (out/'H1PVZPortable.build.json').write_text(json.dumps(meta,indent=2,ensure_ascii=False)+'\n',encoding='utf8')
    print(json.dumps({'bda':str(bda),'bytes':len(data),'sha256':digest,'size':report['size']}),flush=True)

if __name__=='__main__':main()
