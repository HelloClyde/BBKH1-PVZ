"""Package separate H1 program/A: and complete original resource/B: archives.

No save files, firmware, NAND images or host conversion executables are packed.
Every ZIP payload is read back and SHA-256 checked before success is reported.
"""
from __future__ import annotations
import argparse
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from resource_inputs import input_path
import zipfile

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(4*1024*1024),b''):h.update(block)
    return h.hexdigest()


def native_elf_path(receipt):
    candidates=[]
    if receipt.get('native_elf'):
        candidates.append(ROOT/receipt['native_elf'])
    if receipt.get('prepared_source'):
        candidates.append((ROOT/receipt['prepared_source']).parent/'H1PVZPortable.elf')
    candidates.append(ROOT/'build/mips/H1PVZPortable.elf')
    for path in candidates:
        if path.is_file() and digest(path)==receipt.get('native_elf_sha256'):
            return path
    raise ValueError('No matching native ELF for the selected build profile')


def build_identity(bda):
    """Reject program/source changes since the selected native compilation."""
    receipt_path=bda.with_suffix('.build.json')
    receipt=json.loads(receipt_path.read_text(encoding='utf8'))
    if receipt.get('sha256')!=digest(bda) or receipt.get('bytes')!=bda.stat().st_size:
        raise ValueError('Program bytes do not match their build receipt')
    runtime=json.loads((ROOT/'build/cxx-toolchain.json').read_text(encoding='utf8'))
    prefix=Path(runtime['prefix'])
    for group in ['source_inputs','header_inputs','build_inputs']:
        if not receipt.get(group):raise ValueError('Missing build identity group: '+group)
        for name,expected in receipt[group].items():
            source=prefix/name[len('cxx-runtime/'):] if name.startswith('cxx-runtime/') else ROOT/name
            if not source.is_file() or digest(source)!=expected:
                raise ValueError('Build input changed before packaging: '+name)
    elf=native_elf_path(receipt)
    if not elf.is_file() or digest(elf)!=receipt.get('native_elf_sha256'):
        raise ValueError('Native ELF does not match the program receipt')
    return receipt_path,receipt


def make_archive(path,items,manifest,empty_dirs=()):
    if path.exists():raise FileExistsError('Package output already exists: '+str(path))
    expected={}
    with zipfile.ZipFile(path,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for directory in empty_dirs:archive.writestr(directory.rstrip('/')+'/',b'')
        for i,(name,source) in enumerate(items,1):
            if source.is_symlink():raise ValueError('Do not package symlink inputs: '+str(source))
            if name.startswith('/') or '..' in Path(name).parts:raise ValueError('Invalid archive path')
            data=source.read_bytes();expected[name]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
            archive.writestr(name,data)
            if i%500==0:print(f'Packed {i}/{len(items)} payloads into {path.name}',flush=True)
        if manifest.get('bda_sha256'):
            for name,meta in expected.items():
                if name.lower().endswith('.bda') and meta['sha256']!=manifest['bda_sha256']:
                    raise ValueError('Program changed after the package manifest was selected')
        payload=json.dumps(dict(**manifest,files=expected),ensure_ascii=False,indent=2).encode('utf8')+b'\n'
        archive.writestr('package-manifest.json',payload)
        expected['package-manifest.json']=dict(bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())
    with zipfile.ZipFile(path) as archive:
        if len(archive.namelist())!=len(expected)+len(empty_dirs):raise ValueError('Unexpected or duplicate ZIP entries')
        for name,meta in expected.items():
            data=archive.read(name)
            if len(data)!=meta['bytes'] or hashlib.sha256(data).hexdigest()!=meta['sha256']:
                raise ValueError('ZIP read-back mismatch: '+name)
    identity=dict(path=str(path.resolve()),bytes=path.stat().st_size,sha256=digest(path),
                  payload_files=len(items),uncompressed_payload_bytes=sum(v['bytes'] for k,v in expected.items() if k!='package-manifest.json'),
                  all_payloads_readback_verified=True)
    path.with_suffix(path.suffix+'.sha256').write_text(identity['sha256']+'  '+path.name+'\n',encoding='ascii')
    return identity


def source_items():
    items=[]
    excluded={'.git','__pycache__','private','build','test-dev'}
    binary={'.pyc','.pyo','.o','.obj','.a','.dll','.exe','.so','.lib','.pdb','.dylib','.7z','.zip'}
    def add_tree(folder,prefix):
        if not folder.exists():return
        for source in sorted(folder.rglob('*')):
            relative=source.relative_to(folder)
            if source.is_file() and not excluded.intersection(relative.parts) and source.suffix.lower() not in binary:
                items.append((prefix+'/'+relative.as_posix(),source))
    for name in ['README.md','NOTICE.md','LICENSE','COPYING','UPSTREAM.json','.gitignore']:
        if (ROOT/name).is_file():items.append(('h1-pvz-portable/'+name,ROOT/name))
    for name in ['platform','runtime','tools','assets','licenses','third_party','docs']:
        add_tree(ROOT/name,'h1-pvz-portable/'+name)
    add_tree(ROOT/'upstream/src','h1-pvz-portable/upstream/src')
    add_tree(ROOT/'upstream/CMake','h1-pvz-portable/upstream/CMake')
    for name in ['README.md','LICENSE','COPYING','CMakeLists.txt']:
        if (ROOT/'upstream'/name).is_file():items.append(('h1-pvz-portable/upstream/'+name,ROOT/'upstream'/name))
    add_tree(ROOT/'vendor/libxmp','h1-pvz-portable/vendor/libxmp')
    sdk=ROOT/'sdk'
    for name in ['sdk','h1_bda','scripts']:
        add_tree(sdk/name,'vendor/h1-sdk/'+name)
    for name in ['README.md','README.en.md','LICENSE','NOTICE']:
        if (sdk/name).is_file():items.append(('vendor/h1-sdk/'+name,sdk/name))
    # Public host-only verification helpers currently referenced by the new
    # verifier are included as source, so no older project checkout is needed.
    for name in ['h1-mota/tools/verify_firmware.py','h1-joey/tools/emulator_volume.py']:
        source=ROOT.parent/name
        if source.is_file():items.append((name,source))
    if (ROOT.parent/'LICENSE').is_file():items.append(('licenses/Workspace-host-helpers-LICENSE',ROOT.parent/'LICENSE'))
    return items


def source_identity_manifest(items):
    """Describe exactly the pinned ordinary files carried by the source ZIP."""
    prefix='h1-pvz-portable/upstream/'
    files={name[len(prefix):]:digest(path) for name,path in items if name.startswith(prefix)}
    if not files:raise ValueError('The source archive has no pinned upstream inputs')
    pinned=json.loads((ROOT/'UPSTREAM.json').read_text(encoding='utf8'))
    if (ROOT/'upstream/.git').exists():
        git=['git','-c','safe.directory='+(ROOT/'upstream').resolve().as_posix(),'-C',(ROOT/'upstream').as_posix()]
        commit=subprocess.check_output(git+['rev-parse','HEAD'],text=True).strip()
        if commit!=pinned['commit']:raise ValueError('Source archive upstream HEAD differs from pinned commit')
        if subprocess.check_output(git+['status','--porcelain','--untracked-files=no'],text=True).strip():
            raise ValueError('Source archive upstream has tracked modifications')
        date=subprocess.check_output(git+['show','-s','--format=%cs',pinned['commit']],text=True).strip()
    else:
        previous=json.loads((ROOT/'pinned-source-manifest.json').read_text(encoding='utf8'))
        if previous.get('upstream_commit')!=pinned['commit'] or previous.get('files')!=files:
            raise ValueError('Repackaged source file identities differ from pinned source manifest')
        date=previous['commit_date']
    return dict(upstream_commit=pinned['commit'],commit_date=date,files=dict(sorted(files.items())))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bda',type=Path,default=ROOT/'dist/physical-paths/H1PVZPortable.bda')
    parser.add_argument('--resources',type=Path,default=ROOT/'private/resources-h1-full')
    parser.add_argument('--output-dir',type=Path,default=ROOT/'dist'/datetime.now(timezone.utc).strftime('package-%Y%m%d-%H%M%S'))
    parser.add_argument('--version',default='development')
    parser.add_argument('--resources-only',action='store_true')
    parser.add_argument('--verification',type=Path,help='A completed, truthful firmware verification JSON to include')
    args=parser.parse_args()
    if not args.resources.is_dir():raise FileNotFoundError(args.resources)
    if not args.resources_only and not args.bda.is_file():raise FileNotFoundError(args.bda)
    receipt_path=None
    if not args.resources_only:receipt_path,receipt=build_identity(args.bda)
    native_build=native_elf_path(receipt).parent if not args.resources_only else None
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    index=json.loads(input_path(args.resources,'resource-index.json').read_text(encoding='utf8'))
    audio=json.loads(input_path(args.resources,'audio-index.json').read_text(encoding='utf8'))
    modules=json.loads(input_path(args.resources,'module-index.json').read_text(encoding='utf8'))
    manifest=dict(schema='h1-pvz-portable-install-1',version=args.version,
                  target='BBK H1/Y100 V1.41',program_target='A:\\应用\\程序\\H1PVZPortable.bda',
                  resource_target='A:\\应用\\数据\\游戏\\PVZ\\',save_target='A:\\应用\\数据\\游戏\\PVZ\\',
                  resource_policy='A-first resource probe, B: same path fallback; resources, userdata, cache and logs share the selected root',
                  game='Original PvZ-Portable source port, original full reanim/particle/font resources',
                  source_asset_commit=index['source_commit'],all_modes_verified=False,physical_H1_verified=False,
                  known_resource_gaps=index['audit_summary'],
                  music_adaptation=modules['adaptation'],sfx_format=audio['header'],
                  savedata_included=False,firmware_included=False)
    scale_path=args.resources/'h1-scale-manifest.json'
    if not scale_path.is_file():
        try:scale_path=input_path(args.resources,'h1-scale-manifest.json')
        except FileNotFoundError:pass
    if scale_path.is_file():
        scaling=json.loads(scale_path.read_text(encoding='utf8'))
        manifest['offline_scaling']=dict(manifest_sha256=digest(scale_path),
            ratio=scaling.get('ratio'),profile=scaling.get('profile'),
            original_logical_coordinates_preserved=True)
    verification=None
    if args.verification:
        verification=json.loads(args.verification.read_text(encoding='utf8'))
        if not args.resources_only and verification.get('bda_sha256')!=digest(args.bda):
            raise ValueError('Firmware evidence belongs to a different BDA; select the actually verified program')
        if not args.resources_only and (verification.get('elf_sha256')!=receipt['native_elf_sha256'] or
                verification.get('build_receipt_sha256')!=digest(receipt_path)):
            raise ValueError('Firmware ELF/receipt identity differs from the selected native build')
        manifest['firmware_verification']=dict(result=verification.get('result'),resource_drive=verification.get('resource_drive'),
                                               B_drive_verified=verification.get('B_drive_verified',False),sha256=digest(args.verification))
    instructions=("H1 PvZ-Portable 开发包\n\n"
                  "程序包：把 A/应用/程序/H1PVZPortable.bda 合并到设备 A 盘。\n"
                  "资源包：把 A/应用/数据/游戏/PVZ 完整合并到设备 A 盘对应目录。\n"
                  "也支持 B:\\应用\\数据\\游戏\\PVZ；把同一个 PVZ 文件夹放到 B 盘对应目录即可。\n"
                  "程序先探测 A 盘新目录的 properties/resources.xml，找不到才选择 B 盘同目录。\n"
                  "存档、缓存和日志都在本次选中的 PVZ 目录内，userdata 子目录由原游戏管理。\n"
                  "旧存档迁移：把原 A:\\PVZSAVE 内的 userdata 文件夹复制到选中的新 PVZ 目录，\n"
                  "保留旧目录作备份；如新目录已有存档，请先备份并避免覆盖。不要复制测试存档。\n"
                  "两个ZIP应配套使用。资源齐全不等于已通过所有关卡或真机性能验证。\n"
                  "本包没有携带固件、NAND镜像、测试存档或转换工具EXE。\n")
    (out/'安装说明.txt').write_text(instructions,encoding='utf8')
    resource_items=[]
    for source in sorted(args.resources.rglob('*')):
        if source.is_file() and source.name!='h1-scale-owner.json':
            resource_items.append(('A/应用/数据/游戏/PVZ/'+source.relative_to(args.resources).as_posix(),source))
    resource_items.append(('安装说明.txt',out/'安装说明.txt'))
    if (ROOT/'NOTICE.md').is_file():resource_items.append(('NOTICE.md',ROOT/'NOTICE.md'))
    resources=make_archive(out/'H1PVZPortable-resources.zip',resource_items,manifest)
    program=None;source_archive=None
    if not args.resources_only:
        sys.path.insert(0,str(ROOT/'sdk'))
        from h1_bda.validate import validate_bda
        validation=validate_bda(args.bda)
        if not validation['ok']:raise ValueError('BDA validation failed: '+str(validation.get('errors')))
        program_items=[('A/应用/程序/H1PVZPortable.bda',args.bda),('安装说明.txt',out/'安装说明.txt')]
        for name in ['README.md','LICENSE','NOTICE.md','COPYING']:
            if (ROOT/name).is_file():program_items.append((name,ROOT/name))
        if (ROOT/'licenses').is_dir():
            for path in sorted((ROOT/'licenses').rglob('*')):
                if path.is_file():program_items.append(('licenses/'+path.relative_to(ROOT/'licenses').as_posix(),path))
        program_items.append(('assets/README.md',ROOT/'assets/README.md'))
        if (ROOT/'docs').is_dir():
            for path in sorted((ROOT/'docs').rglob('*')):
                if path.is_file() and not {'build','private','__pycache__'}.intersection(path.relative_to(ROOT/'docs').parts):
                    program_items.append(('docs/'+path.relative_to(ROOT/'docs').as_posix(),path))
        for name in ['loader-verification.json','image-metadata-verification.json','music-playback-verification.json',
                     'font-parser-verification.json','font-parser-native-layout.json','raw-rgb-verification.json',
                     'rgba-font-palette-inventory.json','h1-offline-scale-verification.json','resource-audit.json']:
            if (ROOT/'assets'/name).is_file():program_items.append(('docs/'+name,ROOT/'assets'/name))
        for name in ['LICENSE','COPYING']:
            if (ROOT/'upstream'/name).is_file():program_items.append(('licenses/PvZ-Portable-'+name,ROOT/'upstream'/name))
        if args.verification:program_items.append(('docs/firmware-verification.json',args.verification))
        if verification:
            shots=verification.get('selected_screenshots',verification.get('screenshots',[]))
            for number,entry in enumerate(shots,1):
                source=Path(entry['path']).resolve()
                if not source.is_relative_to(ROOT.resolve()) or source.suffix.lower()!='.png' or not source.is_file():
                    raise ValueError('Native screenshot must be an actual PNG inside this project')
                if entry.get('sha256') and digest(source)!=entry['sha256']:
                    raise ValueError('Selected native screenshot changed after verification')
                program_items.append((f'docs/native/{number:02d}-'+source.name,source))
        host_report=ROOT/'build/host/verification.json'
        if host_report.is_file():program_items.append(('docs/host-verification.json',host_report))
        host_ui_report=ROOT/'build/host/final-ui-verification.json'
        if host_ui_report.is_file():program_items.append(('docs/host-final-ui-verification.json',host_ui_report))
        for source,name in [(receipt_path,'H1PVZPortable.build.json'),
                            (native_build/'gp-audit.json','gp-audit.json'),
                            (native_build/'compile-report.json','compile-report.json'),
                            (ROOT/'build/xmp-shared-test/report.json','xmp-shared-test-report.json'),
                            (ROOT/'build/pressure-retry-test/report.json','pressure-retry-test-report.json'),
                            (ROOT/'build/whole-hole-test/report.json','whole-hole-test-report.json'),
                            (ROOT/'build/definition-compaction-probe/run/definition-compaction-verification.json','definition-compaction-verification.json'),
                            (ROOT/'build/paged-rgb-probe/run/paged-rgb-verification.json','paged-rgb-verification.json'),
                            (ROOT/'build/global-allocator-policy-test/report.json','global-allocator-policy-test-report.json'),
                            (ROOT/'build/global-empty-allocator-review.json','global-empty-allocator-review.json'),
                            (ROOT/'build/pool-epoch-probe/run/pool-epoch-verification.json','pool-epoch-verification.json'),
                            (ROOT/'build/selector-gc-probe/run/selector-definition-gc-verification.json','selector-definition-gc-verification.json'),
                            (ROOT/'build/alpha-palette-probe/run/alpha-palette-verification.json','alpha-palette-verification.json'),
                            (ROOT/'build/alpha-loader-probe/run/alpha-loader-verification.json','alpha-loader-verification.json'),
                            (ROOT/'build/rgba-font-palette-probe/run/rgba-font-palette-verification.json','rgba-font-palette-verification.json'),
                            (ROOT/'build/font-page-probe/run/font-page-verification.json','font-page-verification.json'),
                            (ROOT/'build/texture-page-probe/run/texture-page-verification.json','texture-page-verification.json'),
                            (ROOT/'build/scaled-reader-test/report.json','scaled-reader-verification.json'),
                            (ROOT/'build/physical-screen-probe/run/physical-screen-verification.json','physical-screen-verification.json'),
                            (ROOT/'build/reduced-render-probe/run/reduced-render-verification.json','reduced-render-verification.json'),
                            (ROOT/'build/reduced-opaque-probe/run/reduced-render-verification.json','reduced-opaque-verification.json'),
                            (ROOT/'build/host-physical/menu-verification.json','host-physical-menu-verification.json'),
                            (ROOT/'build/host-physical/game-verification.json','host-physical-game-verification.json'),
                            (ROOT/'build/performance-audit/native26-menu.json','baseline-native-menu-performance.json'),
                            (ROOT/'build/performance-audit/scaled-prototype-return-menu.json','scaled-prototype-native-menu-performance.json'),
                            (ROOT/'build/performance-audit/physical-initial-menu.json','physical-initial-native-menu-performance.json'),
                            (ROOT/'build/performance-audit/physical-return-menu.json','physical-return-native-menu-performance.json'),
                            (ROOT/'build/performance-audit/menu-comparison.json','native-menu-comparison.json'),
                            (ROOT/'build/performance-audit/memory-screen-audit.json','memory-screen-audit.json'),
                            (ROOT/'build/path-map-tests/report.json','native-path-mapping-verification.json'),
                            (ROOT/'build/alpha-palette-probe/identity.json','alpha-palette-identity.json'),
                            (ROOT/'build/alpha-loader-probe/identity.json','alpha-loader-identity.json'),
                            (ROOT/'build/rgba-font-palette-probe/identity.json','rgba-font-palette-identity.json'),
                            (ROOT/'build/font-page-probe/identity.json','font-page-identity.json'),
                            (ROOT/'build/texture-page-probe/identity.json','texture-page-identity.json'),
                            (ROOT/'build/paged-rgb-probe/identity.json','paged-rgb-identity.json'),
                            (ROOT/'build/definition-compaction-probe/identity.json','definition-compaction-identity.json'),
                            (ROOT/'build/pool-epoch-probe/identity.json','pool-epoch-identity.json'),
                            (ROOT/'build/selector-gc-probe/identity.json','selector-definition-gc-identity.json')]:
            if source.is_file():program_items.append(('docs/build/'+name,source))
        compile_path=native_build/'compile-report.json'
        if compile_path.is_file():
            original=json.loads(compile_path.read_text(encoding='utf8'))
            summary={key:len(value) if isinstance(value,(list,dict)) else value for key,value in original.items()}
            summary['full_report_sha256']=digest(compile_path)
            (out/'compile-report-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
            program_items.append(('docs/build/compile-report-summary.json',out/'compile-report-summary.json'))
        manifest['bda_sha256']=digest(args.bda)
        program=make_archive(out/'H1PVZPortable-program.zip',program_items,manifest)
        relink=("# Source and relinking\n\n"
                "This archive contains the pinned original engine sources, all reviewed H1 platform/runtime patches, "
                "libxmp and zlib sources, and the actual H1 SDK source modules. The build icon is included; game installation media are prepared separately. No firmware, "
                "NAND, save data or host conversion executables are included.\n\n"
                "The h1-pvz-portable/ and vendor/h1-sdk/ directories are siblings, matching the build scripts. "
                "Build instructions, toolchain bootstrap and pinned revisions are in h1-pvz-portable/README.md, "
                "UPSTREAM.json and tools/. Modify platform/runtime directly, or add checked engine patches to "
                "platform/source_patches.py and the graphics patch modules, then relink using tools/build.py "
                "with the documented MIPS C++ toolchain. prepare.py rejects direct edits to the pinned "
                "upstream tree and regenerates build/source from that tree and the patches. "
                "GNU runtime licensing and exceptions "
                "are retained under licenses/. Game assets are prepared separately and have separate rights.\n\n"
                "The public host-only firmware helper scripts and their workspace license are supplied as "
                "source for the optional verifier. Actual firmware/QEMU/template inputs must be separately supplied.\n")
        (out/'SOURCE-BUILD.md').write_text(relink,encoding='utf8')
        sources=source_items()+[('SOURCE-BUILD.md',out/'SOURCE-BUILD.md')]
        # prepare.py checks exact file-set and content hashes when this archive
        # is extracted without Git metadata. Do not weaken that identity check.
        identity=source_identity_manifest(sources)
        (out/'pinned-source-manifest.json').write_text(json.dumps(identity,indent=2)+'\n',encoding='utf8')
        sources.append(('h1-pvz-portable/pinned-source-manifest.json',out/'pinned-source-manifest.json'))
        source_archive=make_archive(out/'H1PVZPortable-source.zip',sources,
                                    dict(schema='h1-pvz-portable-source-1',version=args.version,
                                         bda_sha256=digest(args.bda),original_upstream=json.loads((ROOT/'UPSTREAM.json').read_text(encoding='utf8')),
                                         git_metadata_included=False,private_assets_included=False,static_relink_sources_included=True))
    if not args.resources_only:build_identity(args.bda)
    report=dict(version=args.version,resources=resources,program=program,
                source=source_archive,program_built=program is not None,physical_device_used=False,all_modes_verified=False)
    (out/'package-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report,ensure_ascii=False,indent=2),flush=True)


if __name__=='__main__':main()
