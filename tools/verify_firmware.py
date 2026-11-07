"""Prepare/control only this project's private complete-firmware H1 guest.

The emulator currently exposes NAND/A:, not an SD/B: block image. Firmware
checks install assets under A:\\应用\\数据\\游戏\\PVZ, matching the product's
A/B resource selection. They do not verify physical B: access.
GDB reads code/diagnostics only. Every game action uses native key/touch input.
"""
from __future__ import annotations
import argparse
from datetime import datetime,timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from types import ModuleType
import uuid
import zlib

ROOT=Path(__file__).resolve().parents[1]
WORKSPACE=ROOT.parent
BUILD=ROOT/'build'
OWNER='h1-pvz-portable-private-firmware-v1'
RESOURCE_PATH='应用/数据/游戏/PVZ'
SAVE_PATH=RESOURCE_PATH+'/userdata'


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(4*1024*1024),b''):h.update(block)
    return h.hexdigest()


def write_json(path,value):
    Path(path).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')


def saved_file_evidence(path,data):
    """Describe bytes read from an idle owned NAND, including original v4 CRC."""
    item=dict(path=path,bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
    if data[:12]==b'PVZP_SAVE4\0\0':
        save=dict(header_complete=len(data)>=24)
        if save['header_complete']:
            version=int.from_bytes(data[12:16],'little');size=int.from_bytes(data[16:20],'little')
            expected=int.from_bytes(data[20:24],'little');payload=data[24:24+size]
            save.update(version=version,payload_bytes=size,payload_complete=len(payload)==size,
                        exact_file_length=len(data)==24+size,stored_crc32=expected,
                        actual_crc32=zlib.crc32(payload)&0xffffffff)
            save['valid_original_v4_header_and_crc']=(version==1 and save['payload_complete']
                and save['exact_file_length'] and save['actual_crc32']==expected)
        item['original_v4_save']=save
    return item


def read_cutscene_clock(read,symbols,runtime_stage,scene):
    """Read the original clock, never call or modify the running program.

    Target offsetof values were compiled by build/input-offsets/cutscene.cpp.o
    for the current MIPS headers: App.board6cc, Board.cutScene1c8,
    CutScene.time8, Board.updateCounter55dc. No C++ object is followed after
    runtime abort/exit; repeated pointer reads reject a changing lifetime.
    """
    if runtime_stage!=2 or scene not in (2,3) or 'gLawnApp' not in symbols:return {}
    def u32(address):return int.from_bytes(read(address,4),'little')
    def heap(address,span):return 0x81c00000<=address and address+span<=0x83c00000
    app=u32(symbols['gLawnApp'][0])
    if not heap(app,0x6d0):return {}
    board=u32(app+0x6cc)
    if not heap(board,0x55e0):return {}
    cutscene=u32(board+0x1c8)
    if not heap(cutscene,12):return {}
    clock=int.from_bytes(read(cutscene+8,4),'little',signed=True)
    updates=u32(board+0x55dc)
    if u32(app+0x6cc)!=board or u32(board+0x1c8)!=cutscene:return dict(pointer_lifetime_changed=True)
    return dict(app_pointer=hex(app),board_pointer=hex(board),cutscene_pointer=hex(cutscene),
                cutscene_time=clock,board_update_counter=updates,
                offset_source='MIPS build/input-offsets/cutscene.cpp.o')


def load_reference():
    path=WORKSPACE/'h1-mota/tools/verify_firmware.py'
    spec=importlib.util.spec_from_file_location('pvz_owned_firmware_reference',path)
    module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
    return module


def load_volume(work):
    path=WORKSPACE/'h1-joey/tools/emulator_volume.py'
    source=path.read_text(encoding='utf8')
    replacements={"from pe_probe import ROOT":"ROOT = Path("+repr(str(WORKSPACE))+")",
                  "folder=ROOT/'build/h1-joey/nand-tools'":"folder=Path("+repr(str(work/'nand-tools'))+")",
                  "'C:/msys64/ucrt64/bin/gcc.exe'":repr(shutil.which('gcc'))}
    for old,new in replacements.items():
        if source.count(old)!=1:raise ValueError('Reviewed private FTL adapter changed: '+old)
        source=source.replace(old,new)
    module=ModuleType('pvz_private_volume');module.__file__=str(path);sys.modules[module.__name__]=module
    exec(compile(source,str(path),'exec'),module.__dict__)
    sdk,installer=module.sdk_tools()
    return module,sdk,installer,dict(path=str(path),sha256=digest(path))


def owned_work(path):
    path=Path(path).resolve()
    if not path.is_relative_to(BUILD.resolve()) or not path.name.startswith('firmware-'):
        raise ValueError('Private work must be h1-pvz-portable/build/firmware-*')
    record=json.loads((path/'owner.json').read_text(encoding='utf8'))
    if record.get('owner')!=OWNER or Path(record['work']).resolve()!=path:
        raise ValueError('Private firmware ownership record mismatch')
    return path,record


def tree_inventory(root):
    output={}
    for p in sorted(Path(root).rglob('*')):
        if p.is_file():output[p.relative_to(root).as_posix()]=dict(bytes=p.stat().st_size,sha256=digest(p))
    return output


def archive_current(work):
    """Copy prior evidence before changing the deployed program or reports.

    The NAND is deliberately excluded: this archives observations, not a
    restorable guest state. Any failure propagates and prevents deployment.
    """
    previous=work/'verification.json'
    if not previous.exists():return None
    report=json.loads(previous.read_text(encoding='utf8'))
    if report.get('owner_id')!=owned_work(work)[1]['id']:
        raise ValueError('Cannot archive evidence with a different owner')
    destination=work/('run-archive-'+datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f'))
    destination.mkdir()
    sources=[p for p in sorted(work.iterdir()) if p.is_file() and p.suffix.lower() in ['.png','.log','.json']]
    for name in ['loaded-build']:
        if (work/name).is_dir():sources.extend(p for p in sorted((work/name).rglob('*')) if p.is_file())
    expected={}
    for source in sources:
        relative=source.relative_to(work);target=destination/relative
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        identity=dict(bytes=source.stat().st_size,sha256=digest(source))
        if target.stat().st_size!=identity['bytes'] or digest(target)!=identity['sha256']:
            raise ValueError('Evidence archive failed verification: '+str(relative))
        expected[relative.as_posix()]=identity
    manifest=dict(schema='h1-pvz-private-run-archive-1',owner_id=report['owner_id'],
                  bda_sha256=report['bda_sha256'],elf_sha256=report['elf_sha256'],
                  prior_result=report['result'],files=expected,NAND_snapshot_included=False,
                  all_copied_files_readback_verified=True)
    write_json(destination/'archive-manifest.json',manifest)
    if json.loads((destination/'archive-manifest.json').read_text(encoding='utf8'))!=manifest:
        raise ValueError('Evidence archive manifest read-back failed')
    print('Prior evidence archived and verified: '+str(destination),flush=True)
    return destination


def build_logical(stage):
    sys.path.insert(0,str(WORKSPACE/'.tools/h1-emulator'))
    from tools import make_fat16_image as fat16
    fat16.RESERVED_SECTORS=480
    tree=fat16.build_tree(list(stage.iterdir()))
    logical=fat16.build_image(tree,'H1PVZPORT',0,32,4096,32,0x1e89e0)
    boot=logical[32*512:33*512]
    reserved=int.from_bytes(boot[14:16],'little');copies=boot[16]
    sectors_per_fat=int.from_bytes(boot[22:24],'little')
    root_sectors=(int.from_bytes(boot[17:19],'little')*32+511)//512
    fat_start=(32+reserved)*512
    fat=logical[fat_start:fat_start+sectors_per_fat*512]
    last=max(i for i in range(2,len(fat)//2) if fat[i*2:i*2+2]!=b'\0\0')
    data_offset=(32+reserved+copies*sectors_per_fat+root_sectors)*512
    used_end=data_offset+(last-1)*boot[13]*512
    return logical,used_end


def volume_entries(volume):
    # Concatenate directory chains before parsing: valid FAT LFNs may cross a
    # 16KiB cluster boundary, especially in the complete 1900-file reanim dir.
    output=[];pending=[('',0)];visited=set()
    while pending:
        parent,first=pending.pop()
        if not first:
            data=volume.reader.read(volume.geometry.root_offset,volume.geometry.root_size)
            base=volume.geometry.root_offset
        else:
            if first in visited:continue
            visited.add(first);data=volume.read_chain(first);base=volume.cluster_offset(first)
        entries=volume._parse_directory(data,base,parent);output.extend(entries)
        pending.extend((e.path,e.first_cluster) for e in entries if e.is_directory and e.first_cluster>=2)
    return output


def verify_resource_readback(work,sdk,expected):
    with sdk.H1Fat16(work/'h1-system.raw') as volume:
        entries=volume_entries(volume)
        files={e.path.casefold():e for e in entries if not e.is_directory}
        for index,(name,meta) in enumerate(expected.items(),1):
            entry=files.get(('/'+RESOURCE_PATH+'/'+name).casefold())
            if entry is None:raise FileNotFoundError('FAT LFN missing: /'+RESOURCE_PATH+'/'+name)
            data=volume.read_file(entry)
            if len(data)!=meta['bytes'] or hashlib.sha256(data).hexdigest()!=meta['sha256']:
                raise ValueError('NAND resource read-back mismatch: '+name)
            if index%500==0:print(f'Read-back verified {index}/{len(expected)} original resource files',flush=True)
        return dict(files=len(expected),bytes=sum(v['bytes'] for v in expected.values()),
                    all_sha256_match=True,resource_drive='A',physical_B_drive_verified=False,
                    native_resource_path='A:/'+RESOURCE_PATH,
                    userdata_directory=any(e.is_directory and e.path.casefold()==('/'+SAVE_PATH).casefold() for e in entries))


def prepare(args):
    reference=load_reference()
    work=(args.work or BUILD/datetime.now(timezone.utc).strftime('firmware-%Y%m%d-%H%M%S-%f')).resolve()
    if not work.is_relative_to(BUILD.resolve()) or not work.name.startswith('firmware-') or work.exists():
        raise ValueError('Use a new h1-pvz-portable/build/firmware-* directory')
    for path in [args.template,args.base_files,args.kernel,args.resources]:
        if not path.exists():raise FileNotFoundError(path)
    reference.ensure_template_idle(args.template.resolve())
    free=shutil.disk_usage(BUILD.parent).free
    # The private image has a fixed raw size plus one staged file tree. Budget
    # actual inputs without relying on optional NTFS compression savings.
    staged_bytes=sum(p.stat().st_size for folder in (args.base_files,args.resources)
                     for p in folder.rglob('*') if p.is_file())
    required=4096*128*2112+staged_bytes+args.kernel.stat().st_size+512*1024**2
    if args.bda:required+=args.bda.stat().st_size
    if free<required:raise OSError(f'Private firmware preparation needs {required} free bytes; available {free}')
    work.mkdir(parents=True)
    if os.name=='nt':subprocess.run(['compact','/C','/I','/Q',str(work)],check=True,capture_output=True)
    write_json(work/'owner.json',dict(owner=OWNER,work=str(work),id=str(uuid.uuid4()),physical_device_used=False))
    report=dict(result='preparing',work=str(work),resource_drive='A',target_install_resource_drive='B',
                B_drive_emulator_supported=False,physical_device_used=False,game_RAM_written=False,
                template=str(args.template.resolve()),template_sha256=digest(args.template),
                kernel_source=str(args.kernel.resolve()),kernel_sha256=digest(args.kernel),
                reference_verifier_sha256=digest(WORKSPACE/'h1-mota/tools/verify_firmware.py'),
                script_sha256=digest(Path(__file__)),slot_alias='应用/程序/黑白子.bda',
                installed_program_name='应用/程序/H1PVZPortable.bda')
    write_json(work/'image-build.json',report)
    stage=work/'files';shutil.copytree(args.base_files,stage)
    calibration=stage/'系统/数据/SysTp.cfg'
    if calibration.exists():calibration.unlink() # Only this newly owned stage.
    if (stage/RESOURCE_PATH).exists():
        raise FileExistsError('Selected template already has PVZ data; refusing to overwrite it')
    shutil.copytree(args.resources,stage/RESOURCE_PATH);(stage/SAVE_PATH).mkdir()
    shutil.copyfile(args.kernel,work/'project.bin')
    if args.bda:
        shutil.copyfile(args.bda,stage/report['slot_alias']);report['bda_sha256']=digest(args.bda)
    else:report['bda_sha256']=None
    report['resources']=tree_inventory(stage/RESOURCE_PATH)
    write_json(work/'image-build.json',report)
    # Build once. Never transactionally rewrite an entire NAND per resource.
    logical,used_end=build_logical(stage)
    adapter,sdk,installer,provenance=load_volume(work)
    report['volume_adapter']=provenance
    raw_size=4096*128*2112
    with (work/'h1-system.raw').open('xb') as out,adapter.EccEncoder() as encoder:
        erased=b'\xff'*(16*1024*1024)
        for offset in range(0,raw_size,len(erased)):out.write(erased[:min(len(erased),raw_size-offset)])
        # Match the proven H1 128-page FTL and BBT commit metadata.
        unit=262144;physical=65;mapped=0
        class Record:
            def __init__(self,block):self.physical_block=block
        # The BBT has the special native logical tag and only two valid pages;
        # preserve it directly from the matched template's physical block 64.
        with args.template.open('rb') as source:
            source.seek(64*128*2112);bbt=source.read(128*2112)
        out.seek(64*128*2112);out.write(bbt)
        for logical_index,offset in enumerate(range(0,used_end,unit)):
            data=logical[offset:offset+unit].ljust(unit,b'\0')
            if physical>=4096-128:raise OSError('Private NAND FTL reserve would be exhausted')
            adapter.write_mapped_unit(out,Record(physical),logical_index,data,1,encoder)
            physical+=1;mapped+=1
            if mapped%200==0:print(f'Encoded {mapped} private 256KiB FTL units',flush=True)
        out.flush();os.fsync(out.fileno())
    del logical
    report['nand_bytes']=raw_size;report['mapped_units']=mapped
    report['readback']=verify_resource_readback(work,sdk,report['resources'])
    report['template_unchanged']=digest(args.template)==report['template_sha256']
    if not report['template_unchanged']:raise ValueError('Source template changed')
    report['result']='prepared-awaiting-BDA' if not args.bda else 'prepared'
    write_json(work/'image-build.json',report)
    print(json.dumps(dict(result=report['result'],work=str(work),mapped_units=mapped,
                          resources=report['readback']),ensure_ascii=False),flush=True)
    return work


def finish_prepare(args):
    """Resume a wholly owned, not-yet-launched preparation after a failed readback."""
    work,owner=owned_work(args.work)
    reference=load_reference();reference.ensure_template_idle(work/'h1-system.raw')
    report=json.loads((work/'image-build.json').read_text(encoding='utf8'))
    if report.get('result')!='preparing':raise ValueError('Only an incomplete owned preparation may be resumed')
    staged=tree_inventory(work/'files'/RESOURCE_PATH)
    if 'resources' not in report:
        if staged!=tree_inventory(ROOT/'private/resources'):
            raise ValueError('Cannot recover the interrupted manifest: source and staged resource identities differ')
        report['resources']=staged;report['bda_sha256']=None
        report['interrupted_manifest_recovered_from_identical_source_and_stage']=True
        write_json(work/'image-build.json',report)
    if staged!=report['resources']:raise ValueError('Frozen staged resources changed')
    adapter,sdk,installer,provenance=load_volume(work)
    logical,used_end=build_logical(work/'files')
    scan=adapter.scan_image(work/'h1-system.raw');unit=262144
    missing=[i for i in range((used_end+unit-1)//unit) if i not in scan.mapping]
    free=[r for r in scan.records if r.kind=='free' and r.physical_block>=65]
    if len(free)<len(missing)+128:raise OSError('Insufficient private FTL reserve')
    with (work/'h1-system.raw').open('r+b',buffering=0) as output,adapter.EccEncoder() as encoder:
        for i,record in zip(missing,free):
            adapter.write_mapped_unit(output,record,i,logical[i*unit:(i+1)*unit].ljust(unit,b'\0'),1,encoder)
        output.flush();os.fsync(output.fileno())
    del logical
    report['preparation_repairs']=[dict(reason='All allocated FAT data must be mapped even when a whole raw-pixel unit is zero',
                                        newly_mapped_units=missing,only_owned_unlaunched_NAND_modified=True)]
    report['readback']=verify_resource_readback(work,sdk,report['resources'])
    report['nand_bytes']=(work/'h1-system.raw').stat().st_size
    report['mapped_units']=len(adapter.scan_image(work/'h1-system.raw').mapping)
    report['template_unchanged']=digest(report['template'])==report['template_sha256']
    if not report['template_unchanged']:raise ValueError('Source template changed')
    report['result']='prepared-awaiting-BDA' if not report['bda_sha256'] else 'prepared'
    write_json(work/'image-build.json',report)
    print(json.dumps(dict(result=report['result'],work=str(work),repaired_units=len(missing),readback=report['readback']),ensure_ascii=False),flush=True)


def add_metadata(args):
    """Install only the optional index and its updated receipt in an idle NAND."""
    work,owner=owned_work(args.work);reference=load_reference()
    reference.ensure_template_idle(work/'h1-system.raw')
    archive_current(work)
    source=ROOT/'private/resources-h1-wide/image-metadata.h1x'
    evidence=json.loads((ROOT/'assets/image-metadata-verification.json').read_text(encoding='utf8'))
    if digest(source)!=evidence['index_sha256']:raise ValueError('Metadata index differs from its verified fixture')
    adapter,sdk,installer,provenance=load_volume(work)
    sys.path.insert(0,str(WORKSPACE/'.tools/h1-emulator'))
    from tools import make_fat16_image as fat16
    # The public data installer is limited to one LFN slot. For this one
    # explicitly selected file, use the same tested multi-slot FAT encoder
    # used by the full image builder and reserve all its directory slots.
    installer.deployment=sdk
    original_free=installer.free_directory_slots
    installer.one_lfn_entry=lambda name,short:b''.join(fat16.make_lfn_entries(name,fat16.lfn_checksum(short)))
    installer.free_directory_slots=lambda volume,directory,count:original_free(volume,directory,count+1)
    with sdk.H1Fat16(work/'h1-system.raw') as volume:
        existing=[e for e in volume_entries(volume) if e.path.casefold()==('/'+RESOURCE_PATH+'/image-metadata.h1x').casefold()]
        if existing and volume.read_file(existing[0])!=source.read_bytes():
            raise ValueError('A different index already exists; prepare a reviewed replacement')
    changes=[]
    if not existing:
        changes.append(installer.install(work/'h1-system.raw',source,'/应用/数据/游戏','PVZ',source.name,'IMGMETA.H1X',Path(),reuse_directory=True))
    original_find=sdk.find_target
    def selected_receipt(volume,requested,required_size,*,allow_growth=False):
        if requested.casefold()==('/'+RESOURCE_PATH+'/resource-index.json').casefold():
            matches=[e for e in volume_entries(volume) if e.path.casefold()==requested.casefold() and not e.is_directory]
            if len(matches)!=1:raise ValueError('Resource receipt did not resolve uniquely')
            return matches[0]
        return original_find(volume,requested,required_size,allow_growth=allow_growth)
    sdk.find_target=selected_receipt
    try:
        changes.append(sdk.deploy(work/'h1-system.raw',ROOT/'private/resources-h1-wide/resource-index.json','/'+RESOURCE_PATH+'/resource-index.json',Path(),allow_growth=True))
    finally:sdk.find_target=original_find
    image=json.loads((work/'image-build.json').read_text(encoding='utf8'))
    for name in ['image-metadata.h1x','resource-index.json']:
        path=ROOT/'private/resources-h1-wide'/name
        shutil.copyfile(path,work/'files'/RESOURCE_PATH/name)
        image['resources'][name]=dict(bytes=path.stat().st_size,sha256=digest(path))
    image['metadata_deployment']=dict(changes=changes,index_sha256=digest(source),only_owned_idle_NAND_modified=True)
    image['readback']=verify_resource_readback(work,sdk,image['resources'])
    write_json(work/'image-build.json',image)
    write_json(work/'metadata-deployment.json',dict(owner_id=owner['id'],**image['metadata_deployment'],readback=image['readback']))
    print(json.dumps(image['readback']),flush=True)


def run(args):
    work,owner=owned_work(args.work)
    reference=load_reference();reference.ensure_template_idle(work/'h1-system.raw')
    for path in [args.bda,args.elf,args.qemu]:
        if not path.is_file():raise FileNotFoundError(path)
    selected_bda_sha=digest(args.bda);selected_elf_sha=digest(args.elf)
    if args.expected_bda_sha256 and selected_bda_sha!=args.expected_bda_sha256.lower():
        raise ValueError('BDA does not match the explicitly approved candidate SHA-256')
    receipt_path=args.bda.with_suffix('.build.json');receipt_bytes=None
    if receipt_path.exists():
        receipt_bytes=receipt_path.read_bytes();receipt=json.loads(receipt_bytes)
        if receipt.get('sha256')!=selected_bda_sha or receipt.get('native_elf_sha256')!=selected_elf_sha:
            raise ValueError('BDA/ELF do not match the frozen native build receipt')
    archive_current(work)
    image=json.loads((work/'image-build.json').read_text(encoding='utf8'))
    adapter,sdk,installer,provenance=load_volume(work)
    loaded=work/'loaded-build';loaded.mkdir(exist_ok=True)
    shutil.copyfile(args.bda,loaded/'H1PVZPortable.bda');shutil.copyfile(args.elf,loaded/'H1PVZPortable.elf')
    if digest(loaded/'H1PVZPortable.bda')!=selected_bda_sha or digest(loaded/'H1PVZPortable.elf')!=selected_elf_sha:
        raise ValueError('A selected build changed while copying; nothing was deployed')
    if receipt_bytes is not None:(loaded/'H1PVZPortable.build.json').write_bytes(receipt_bytes)
    if image.get('bda_sha256')!=selected_bda_sha:
        image['program_deployment']=sdk.deploy(work/'h1-system.raw',loaded/'H1PVZPortable.bda','黑白子.bda',Path(),allow_growth=True)
        image['bda_sha256']=selected_bda_sha;write_json(work/'image-build.json',image)
    report=dict(result='running',owner_id=owner['id'],physical_device_used=False,game_RAM_written=False,
                inputs=[],states=[],screenshots=[],resource_drive='A',B_drive_verified=False,
                bda_sha256=selected_bda_sha,elf_sha256=selected_elf_sha,
                build_receipt_sha256=hashlib.sha256(receipt_bytes).hexdigest() if receipt_bytes is not None else None,
                normal_application_exit=False)
    args.nand_parent=None
    class Guest(reference.Verification):
        def snapshot(self):
            self.owned();remote=reference.GdbRemote('127.0.0.1',self.gdb_port,10)
            try:
                remote.command('qSupported')
                def read(address,count):
                    data=bytearray()
                    for offset in range(0,count,512):
                        reply=remote.command(f'm{address+offset:x},{min(512,count-offset):x}')
                        if reply.startswith('E'):raise RuntimeError('GDB read failed: '+reply)
                        data.extend(bytes.fromhex(reply))
                    return bytes(data)
                registers={}
                for name,index in [('pc',37),('sp',29),('ra',31)]:
                    reply=remote.command(f'p{index:x}')
                    if len(reply)==8 and not reply.startswith('E') and 'x' not in reply:
                        registers[name]='0x'+format(int.from_bytes(bytes.fromhex(reply),'little'),'08x')
                if read(self.code_address,len(self.code_sample))!=self.code_sample:return dict(native_code_loaded=False,registers=registers)
                runtime={}
                if 'pvz_runtime_diagnostic' in self.symbols:
                    runtime_address,runtime_size=self.symbols['pvz_runtime_diagnostic']
                    runtime_raw=read(runtime_address,min(runtime_size,64))
                    runtime_values=[int.from_bytes(runtime_raw[i:i+4],'little') for i in range(0,len(runtime_raw),4)]
                    names={0:'magic',1:'stage',2:'abort_status',3:'heap_current',4:'heap_peak',5:'file_opens',6:'rtc_valid',7:'resource_drive',8:'filesystem_stage',9:'filesystem_fd_or_bytes',10:'ctor_count',11:'current_ctor',12:'failed_malloc_bytes',13:'failed_malloc_caller',14:'failed_malloc_native_result',15:'version'}
                    runtime=dict(raw=runtime_values,**{name:runtime_values[i] for i,name in names.items() if i<len(runtime_values)})
                    if runtime.get('resource_drive') in [65,66]:runtime['resource_drive_name']=chr(runtime['resource_drive'])
                address,size=self.symbols['pvzp_diagnostic'];raw=read(address,min(size,128))
                values=[int.from_bytes(raw[i:i+4],'little') for i in range(0,len(raw),4)]
                labels={0:'magic',1:'window_status',2:'frames',3:'display_error',4:'log_lines',5:'keys',6:'touches',
                        7:'scene',8:'updates',9:'level',10:'sun',11:'wave',12:'main_counter',13:'profile_level',
                        20:'image_resident_bytes',21:'image_peak_bytes',22:'image_load_failures'}
                music={}
                if 'pvzp_music_diagnostic' in self.symbols:
                    address,size=self.symbols['pvzp_music_diagnostic'];raw=read(address,min(size,32))
                    fields=[int.from_bytes(raw[i:i+4],'little') for i in range(0,len(raw),4)]
                    names=['owner_count','original_track_bytes','unique_track_bytes','sample_bytes','player_count','module_load_count']
                    music=dict(raw=fields,**dict(zip(names,fields)))
                pressure={}
                for symbol,names in [
                    ('pvzh1_plant_pose_diagnostic',['loaded_banks','failed_banks','resident_entries','reserved','attempts','replays','signature_guards','missing_poses','signature_reuses','signature_changes','pressure_releases','released_bytes']),
                    ('pvzh1_board_replay_retirement_diagnostic',['calls','last_before_bytes','last_after_bytes']),
                    ('pvz_runtime_new_failure_caller',['last_throwing_caller']),
                    ('pvz_runtime_pressure_diagnostic',['callbacks','retries','successful_retries','freed_bytes','last_request','final_failures']),
                    ('pvzh1_cache_pressure_diagnostic',['calls','evictions','total_freed','last_freed']),
                    ('pvzh1_cache_diagnostic',['resident_bytes','peak_bytes','loads','evictions','frames','loaded_pixel_bytes_low32']),
                    ('pvz_runtime_hole_diagnostic',['magic','attempts','successes','last_status','user_request','raw_request',
                     'rounded_raw_request','actual_native_span','cumulative_waste','record_index','semaphore_error','tail_bytes']),
                    ('pvz_runtime_native_charge',['current_native_span','peak_native_span']),
                    ('pvz_runtime_native_charge_failure',['native_span_at_first_final_failure']),
                    ('pvzh1_definition_compaction_diagnostic',['calls','transform_arrays_trimmed','track_arrays_trimmed',
                     'actual_target_bytes_saved','optional_allocation_failures','largest_new_array_bytes','frames_examined','tracks_examined']),
                    ('pvzh1_paged_rgb_diagnostic',['resident_bytes','resident_pages','cumulative_page_allocations','source_loads',
                     'read_bytes_low32','failed_loads','large_rgb_argb_attempts','max_page_bytes']),
                    ('pvzh1_allocator_cache_diagnostic',['calls','pools_trimmed','blocks_freed','cumulative_requested_slab_bytes',
                     'max_requested_slab_bytes','latest_trim_bytes','live_pools_skipped','spare']),
                    ('pvzh1_selector_definition_diagnostic',['load_begins','ready_generations','collections','captured_strings',
                     'freed_strings','current_owned_strings','loading','ready','disabled','latest_freed_logical_payload']),
                    ('pvzh1_alpha_palette_diagnostic',['resident_index_and_table_bytes','source_count','loads','read_bytes_low32',
                     'failed_loads','successful_prepare_uses','generic_cache_argb_source_loads','reserved']),
                    ('pvzh1_rgba_font_palette_diagnostic',['resident_index_and_table_bytes','source_count','loads','read_bytes_low32',
                     'failed_loads','successful_prepare_uses','memoized_over_256_rejects','last_accepted_color_count']),
                    ('pvzh1_rgba_font_page_diagnostic',['resident_bytes','resident_pages','source_loads','read_bytes_low32',
                     'failed_loads','glyph_tile_blits','generic_argb_source_loads','max_page_bytes']),
                    ('pvzh1_rgba_raster_page_diagnostic',['resident_bytes','resident_pages','source_loads','read_bytes_low32',
                     'failed_loads','raster_views','generic_argb_source_loads','max_page_bytes']),
                    ('pvzh1_storage_texture_diagnostic',['resident_bytes','source_count','source_loads','read_bytes_low32',
                     'failed_loads','storage_views','logical_materializations','max_dense_allocation']),
                    ('pvzh1_physical_screen_diagnostic',['active','physical_width','physical_height','logical_width',
                     'logical_height','raw_fallbacks','presents','routed_calls'])]:
                    if symbol not in self.symbols:continue
                    address,size=self.symbols[symbol];raw=read(address,min(size,len(names)*4))
                    fields=[int.from_bytes(raw[i:i+4],'little') for i in range(0,len(raw),4)]
                    pressure[symbol]=dict(zip(names,fields))
                heap_failure={}
                if 'pvz_runtime_heap_failure' in self.symbols:
                    address,size=self.symbols['pvz_runtime_heap_failure'];raw=read(address,min(size,80))
                    fields=[int.from_bytes(raw[i:i+4],'little') for i in range(0,len(raw),4)]
                    names=['magic','status','user_current','user_peak','user_request','native_raw_request',
                           'heap_base','heap_end','current_top','descriptor_bottom','records','total_free',
                           'largest_free_region','max_native_alloc_request','allocated_raw','free_records',
                           'tail_bytes','descriptor_bytes','holes_bytes','visited_records']
                    heap_failure=dict(raw=fields,**dict(zip(names,fields)))
                clock=read_cutscene_clock(read,self.symbols,runtime.get('stage'),values[7] if len(values)>7 else None)
                return dict(native_code_loaded=True,registers=registers,runtime=runtime,music=music,pressure=pressure,
                            original_cutscene_clock=clock,
                            heap_failure=heap_failure,raw_diagnostic=values,**{name:values[i] for i,name in labels.items() if i<len(values)})
            finally:
                try:remote.command('D')
                finally:remote.close()
        def launch(self):
            with (loaded/'H1PVZPortable.elf').open('rb') as stream:
                elf=reference.ELFFile(stream)
                self.symbols={s.name:(s['st_value'],s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
                sections=[s for s in elf.iter_sections() if s['sh_flags']&4 and s.data_size>=128]
                executable=min(sections,key=lambda s:s['sh_addr']);self.code_address=executable['sh_addr'];self.code_sample=executable.data()[:128]
            if 'pvzp_diagnostic' not in self.symbols:raise ValueError('ELF must retain native diagnostic symbol')
            command=[sys.executable,'-u',str(WORKSPACE/'.tools/h1-emulator/h1/h1_emulator.py'),'--qemu',str(args.qemu.resolve()),
                     '--kernel',str(work/'project.bin'),'--nand',str(self.nand),'--dll-dir','C:/msys64/ucrt64/bin',
                     '--port',str(self.port),'--gdb-port',str(self.gdb_port),'--ram-mib','64','--touch-profile','v1',
                     '--no-browser','--no-charger','--writable']
            self.streams=[(work/name).open('w',encoding='utf8') for name in ['frontend.stdout.log','frontend.stderr.log']]
            self.process=subprocess.Popen(command,cwd=WORKSPACE,stdout=self.streams[0],stderr=self.streams[1],creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            report['emulator']=dict(frontend_pid=self.process.pid,server=self.server,gdb_port=self.gdb_port,nand=str(self.nand))
            self.flush();deadline=time.monotonic()+100
            while time.monotonic()<deadline:
                if self.process.poll() is not None:raise RuntimeError('Owned frontend exited')
                try:
                    status=self.owned()
                    if status.get('running') and status.get('calibration_status') in ['complete','not-required'] and float(status.get('uptime') or 0)>=15:break
                except OSError:pass
                time.sleep(.3)
            else:raise TimeoutError('Firmware boot did not complete')
            self.screenshot('00-boot')
        def observe(self,label):
            state=self.snapshot();report['states'].append(dict(name=label,**state));self.screenshot(label);self.flush();return state
        def actions(self,items):
            for index,item in enumerate(items):
                if 'wait' in item:time.sleep(min(float(item['wait']),60))
                elif 'key' in item:self.key(int(item['key']))
                elif 'tap' in item:self.tap(*item['tap'])
                elif 'observe' in item:self.observe(item['observe'])
                elif 'expect' in item:
                    expected=item['expect'];deadline=time.monotonic()+item.get('timeout',30)
                    while time.monotonic()<deadline:
                        state=self.snapshot()
                        if state.get('runtime',{}).get('stage')==255:
                            raise RuntimeError('Native application aborted before expected state: '+str(state['runtime']))
                        def value(key):
                            result=state
                            for part in key.split('.'):
                                if not isinstance(result,dict):return None
                                result=result.get(part)
                            return result
                        if all(value(k)==v for k,v in expected.items()):break
                        time.sleep(.25)
                    else:raise AssertionError(f'Observed {state}, expected {expected}')
                    report['states'].append(dict(name=item.get('name','expect-'+str(index)),**state));self.flush()
                elif 'swipe_left' in item:
                    self.owned();self.log('desktop-swipe-left');self.api('/api/touch',dict(x=420,y=150,down=True));time.sleep(.2)
                    for x in [380,340,300,260,220,180,140,100,60]:self.api('/api/touch',dict(x=x,y=150,down=True));time.sleep(.065)
                    self.api('/api/touch',dict(x=60,y=150,down=False));time.sleep(1)
                else:raise ValueError('Only native input/wait/read observation actions are supported')
    guest=Guest(args,work,report)
    try:
        guest.launch()
        guest.actions([{'key':24},{'wait':3},{'key':25},{'wait':2},{'key':24},{'wait':1},
                       {'tap':[360,14]},{'wait':.7},{'tap':[358,args.category_y]},{'wait':1.2},
                       {'swipe_left':True},{'swipe_left':True},{'observe':'01-desktop-slot'},{'tap':[42,51]}])
        deadline=time.monotonic()+args.load_timeout
        observed_at=0
        while time.monotonic()<deadline:
            state=guest.snapshot()
            if time.monotonic()-observed_at>=10:
                observed_at=time.monotonic();report['states'].append(dict(name='loading',**state));guest.flush()
                runtime=state.get('runtime',{})
                progress=dict(loading=True,frames=state.get('frames'),updates=state.get('updates'),
                    scene=state.get('scene'),stage=runtime.get('stage'),heap=runtime.get('heap_current'),
                    peak=runtime.get('heap_peak'),file_opens=runtime.get('file_opens'),
                    drive=runtime.get('resource_drive_name'),pc=state.get('registers',{}).get('pc'))
                print(json.dumps(progress,ensure_ascii=False),flush=True)
                guest.screenshot('loading-'+str(len(report['states'])))
            if state.get('runtime',{}).get('stage')==255:
                raise RuntimeError('Native application aborted during loading: '+str(state['runtime']))
            # A cleared framebuffer plus pointer is an early frame, not a
            # responsive title/menu. Wait until the real app loop advances.
            if state.get('frames',0)>1 and state.get('updates',0)>1:break
            time.sleep(5)
        else:raise TimeoutError('BDA did not reach its updating title/menu within the loading window')
        guest.observe('02-native-start')
        if args.actions:guest.actions(json.loads(args.actions.read_text(encoding='utf8')))
        else:
            for i in range(max(1,args.observe_seconds//10)):
                time.sleep(10);guest.observe('observe-'+str(i))
        if args.continue_input:
            state=guest.observe('native-title-continue-input')
            if state.get('runtime',{}).get('stage')==255:
                raise RuntimeError('Native application aborted: '+str(state['runtime']))
            report['result']='observed-updating-title-awaiting-native-input'
            return
        if args.exit_actions:guest.actions(json.loads(args.exit_actions.read_text(encoding='utf8')))
        elif guest.snapshot().get('scene')==1:
            guest.key(24);time.sleep(.7);guest.key(25)
        deadline=time.monotonic()+25
        while time.monotonic()<deadline:
            final=guest.snapshot()
            if final.get('runtime',{}).get('stage')==255:
                raise RuntimeError('Native application aborted: '+str(final['runtime']))
            if final.get('window_status')==9:report['normal_application_exit']=True;break
            time.sleep(.5)
        guest.observe('99-final')
        if not report['normal_application_exit']:raise AssertionError('Normal game exit was not observed; do not treat forced guest stop as a successful exit')
        report['result']='passed-requested-scenario'
    except BaseException as error:
        report['result']='failed';report['error']=type(error).__name__+': '+str(error)
        try:guest.observe('failure')
        except Exception as detail:report['failure_observation_error']=str(detail)
        raise
    finally:
        keep=bool(guest.process and (args.continue_input or (args.keep_running_on_failure and report['result']=='failed')))
        if keep:report['owned_emulator_left_running']=True
        else:guest.stop()
        report['elapsed_seconds']=round(time.monotonic()-guest.started,3);guest.flush()
        if not keep:
          with sdk.H1Fat16(work/'h1-system.raw') as volume:
            saves=[]
            for entry in volume_entries(volume):
                if not entry.is_directory and entry.path.casefold().startswith(('/'+SAVE_PATH+'/').casefold()):
                    data=volume.read_file(entry);saves.append(saved_file_evidence(entry.path,data))
                    if entry.path.casefold().endswith('/pvz.log'):(work/'application.log').write_bytes(data)
            report['observed_save_files']=saves
        guest.flush();print(json.dumps(dict(result=report['result'],report=str(work/'verification.json')),ensure_ascii=False),flush=True)


def watch(args):
    """Attach only read-only observations to this already owned live guest."""
    import urllib.request
    work,owner=owned_work(args.work);reference=load_reference()
    prior=json.loads((work/'verification.json').read_text(encoding='utf8'))
    emulator=prior['emulator'];server=emulator['server']
    with (work/'loaded-build/H1PVZPortable.elf').open('rb') as f:
        elf=reference.ELFFile(f);symbols={s.name:(s['st_value'],s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
    entity_decoder=None;entity_evidence=None
    if args.entities:
        decoder=ROOT/'tools/tests/native_day_entities.py';layout=BUILD/'input-offsets/day_entities.json'
        identity=json.loads(layout.read_text(encoding='utf8'))
        if identity['elf_sha256']!=prior['elf_sha256']:raise ValueError('Entity offsets do not match the loaded ELF')
        spec=importlib.util.spec_from_file_location('pvz_readonly_day_entities',decoder)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        entity_decoder=module.read_day_entities
        entity_evidence=dict(decoder=str(decoder),decoder_sha256=digest(decoder),layout=str(layout),
                             layout_sha256=digest(layout),elf_sha256=identity['elf_sha256'])
    name=datetime.now(timezone.utc).strftime('watch-%Y%m%d-%H%M%S')
    destination=work/(name+'.json')
    report=dict(result='observing',owner_id=owner['id'],emulator=emulator,
                bda_sha256=prior['bda_sha256'],elf_sha256=prior['elf_sha256'],
                game_RAM_written=False,physical_device_used=False,states=[],screenshots=[])
    if entity_evidence:report['entity_decoder_evidence']=entity_evidence
    def status():
        with urllib.request.urlopen(server+'/api/status',timeout=12) as f:value=json.load(f)
        if Path(value.get('nand','')).resolve()!=work/'h1-system.raw':raise ValueError('Live guest ownership changed')
        return value
    started=time.monotonic();last_screen=0
    while time.monotonic()-started<args.seconds:
        current=status()
        if not current.get('running'):raise RuntimeError('Owned guest is no longer running')
        remote=reference.GdbRemote('127.0.0.1',emulator['gdb_port'],10)
        try:
            remote.command('qSupported');values={}
            for symbol in ['pvzp_diagnostic','pvz_runtime_diagnostic','pvzp_music_diagnostic',
                           'pvz_runtime_pressure_diagnostic','pvzh1_cache_pressure_diagnostic','pvzh1_cache_diagnostic',
                           'pvz_runtime_hole_diagnostic','pvz_runtime_native_charge',
                           'pvz_runtime_native_charge_failure','pvzh1_definition_compaction_diagnostic',
                           'pvzh1_paged_rgb_diagnostic','pvzh1_allocator_cache_diagnostic',
                           'pvzh1_selector_definition_diagnostic','pvzh1_alpha_palette_diagnostic',
                           'pvzh1_rgba_font_palette_diagnostic','pvzh1_rgba_font_page_diagnostic',
                           'pvzh1_rgba_raster_page_diagnostic','pvzh1_storage_texture_diagnostic',
                           'pvzh1_physical_screen_diagnostic','pvz_runtime_heap_failure']:
                if symbol not in symbols:continue
                address,size=symbols[symbol];reply=remote.command(f'm{address:x},{size:x}')
                if reply.startswith('E'):raise ValueError(reply)
                raw=bytes.fromhex(reply);values[symbol]=[int.from_bytes(raw[i:i+4],'little') for i in range(0,len(raw),4)]
            for symbol in ['pvz_runtime_last_path','pvz_runtime_new_failure_caller']:
                if symbol not in symbols:continue
                address,size=symbols[symbol];reply=remote.command(f'm{address:x},{min(size,512):x}')
                if reply.startswith('E'):continue
                raw=bytes.fromhex(reply)
                if symbol.endswith('path'):
                    path_bytes=raw.split(b'\0',1)[0]
                    values[symbol+'_raw_hex']=path_bytes.hex()
                    for encoding in ['utf8','gbk','latin1']:
                        try:values[symbol]=path_bytes.decode(encoding);values[symbol+'_encoding']=encoding;break
                        except UnicodeDecodeError:continue
                else:values[symbol]='0x'+format(int.from_bytes(raw[:4],'little'),'08x')
            for field,index in [('pc',37),('sp',29),('ra',31)]:
                reply=remote.command(f'p{index:x}')
                if len(reply)==8 and 'x' not in reply and not reply.startswith('E'):values[field]='0x'+format(int.from_bytes(bytes.fromhex(reply),'little'),'08x')
            def read_clock(address,count):
                reply=remote.command(f'm{address:x},{count:x}')
                if reply.startswith('E'):raise ValueError(reply)
                return bytes.fromhex(reply)
            values['original_cutscene_clock']=read_cutscene_clock(read_clock,symbols,
                values['pvz_runtime_diagnostic'][1],values['pvzp_diagnostic'][7])
            if entity_decoder:
                values['original_entities']=entity_decoder(read_clock,symbols,
                    values['pvz_runtime_diagnostic'][1],values['pvzp_diagnostic'][7])
        finally:
            try:remote.command('D')
            finally:remote.close()
        values['elapsed']=round(time.monotonic()-started,3)
        values['wall_monotonic']=round(time.monotonic(),6)
        values['observed_at_utc']=datetime.now(timezone.utc).isoformat(timespec='milliseconds')
        report['states'].append(values)
        r=values['pvz_runtime_diagnostic'];p=values['pvzp_diagnostic']
        print(json.dumps(dict(seconds=values['elapsed'],frames=p[2],updates=p[8],scene=p[7],
                              stage=r[1],heap=r[3],peak=r[4],file_opens=r[5],drive=r[7],pc=values.get('pc'),
                              cutscene_time=values.get('original_cutscene_clock',{}).get('cutscene_time'))),flush=True)
        if time.monotonic()-last_screen>=30:
            last_screen=time.monotonic()
            with urllib.request.urlopen(server+'/api/debug/frame',timeout=12) as f:w,h,pixels=reference.convert_frame(f.read())
            screenshot=work/(name+'-'+str(len(report['states']))+'.png');screenshot.write_bytes(reference.encode_png(w,h,pixels))
            report['screenshots'].append(dict(path=str(screenshot),sha256=digest(screenshot)))
        write_json(destination,report)
        if args.until_scene is not None and p[7]==args.until_scene and p[2]>1:
            report['result']='observed-requested-scene';break
        if r[1] in [9,255]:report['result']='observed-runtime-exit';break
        time.sleep(args.interval)
    else:report['result']='observation-window-ended-guest-left-running'
    write_json(destination,report);print(str(destination),flush=True)
    if report['states'] and report['states'][-1]['pvz_runtime_diagnostic'][1]==255:
        current=json.loads((work/'verification.json').read_text(encoding='utf8'))
        if current.get('bda_sha256')==report['bda_sha256'] and current.get('result')=='observed-updating-title-awaiting-native-input':
            current['driver_result']=current['result'];current['result']='failed-during-native-input'
            current['continuation_failure']=dict(watch_report=str(destination),
                native_abort_status=report['states'][-1]['pvz_runtime_diagnostic'][2],
                last_observation=report['states'][-1])
            write_json(work/'verification.json',current)


def native_input(args):
    """Continue an already owned guest using only firmware key/touch events."""
    import urllib.request
    work,owner=owned_work(args.work);reference=load_reference()
    prior=json.loads((work/'verification.json').read_text(encoding='utf8'))
    emulator=prior['emulator'];server=emulator['server']
    record=dict(owner_id=owner['id'],bda_sha256=prior['bda_sha256'],emulator=emulator,
                game_RAM_written=False,physical_device_used=False,inputs=[],screenshots=[])
    stamp=datetime.now(timezone.utc).strftime('native-input-%Y%m%d-%H%M%S-%f')
    def api(path,payload=None):
        request=urllib.request.Request(server+path,data=None if payload is None else json.dumps(payload).encode(),
                                       headers={'Content-Type':'application/json'})
        with urllib.request.urlopen(request,timeout=12) as response:return json.load(response)
    def owned():
        status=api('/api/status')
        if Path(status.get('nand','')).resolve()!=work/'h1-system.raw' or not status.get('running'):
            raise ValueError('Live guest ownership/running state changed')
    started=time.monotonic()
    for number,action in enumerate(json.loads(args.actions.read_text(encoding='utf8'))):
        owned();record['inputs'].append(dict(elapsed=round(time.monotonic()-started,3),**action))
        if 'tap' in action:
            x,y=map(int,action['tap'])
            if not 0<=x<480 or not 0<=y<272:raise ValueError('Native LCD touch out of bounds')
            api('/api/touch',dict(x=x,y=y,down=True))
            try:time.sleep(max(.05,min(60,float(action.get('hold',.24)))))
            finally:
                owned();api('/api/touch',dict(x=x,y=y,down=False))
            time.sleep(max(0,min(60,float(action.get('release_wait',.28)))))
        elif 'key' in action:
            code=int(action['key'])
            if not 1<=code<=44:raise ValueError('Use a native H1 firmware keycode')
            api('/api/key',dict(code=code,down=True))
            try:time.sleep(max(.05,min(60,float(action.get('hold',.13)))))
            finally:
                owned();api('/api/key',dict(code=code,down=False))
            time.sleep(max(0,min(60,float(action.get('release_wait',.18)))))
        elif 'wait' in action:time.sleep(max(0,min(60,float(action['wait']))))
        elif 'screenshot' in action:
            with urllib.request.urlopen(server+'/api/debug/frame',timeout=12) as f:w,h,pixels=reference.convert_frame(f.read())
            path=work/(stamp+'-'+str(number)+'.png');path.write_bytes(reference.encode_png(w,h,pixels))
            record['screenshots'].append(dict(label=str(action['screenshot']),path=str(path),sha256=digest(path)))
        else:raise ValueError('Supported native actions: tap, key, wait, screenshot')
        write_json(work/(stamp+'.json'),record)
    print(str(work/(stamp+'.json')),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='command',required=True)
    p=sub.add_parser('prepare');p.add_argument('--work',type=Path);p.add_argument('--bda',type=Path)
    p.add_argument('--template',type=Path,default=WORKSPACE/'build/h1-emulator-test/h1-system.raw')
    p.add_argument('--base-files',type=Path,default=WORKSPACE/'build/h1-emulator-test/files')
    p.add_argument('--kernel',type=Path,default=WORKSPACE/'build/h1-emulator-test/project.bin')
    p.add_argument('--resources',type=Path,default=ROOT/'private/resources-h1-wide')
    p=sub.add_parser('finish-prepare');p.add_argument('--work',type=Path,required=True)
    p=sub.add_parser('run');p.add_argument('--work',type=Path,required=True);p.add_argument('--bda',type=Path,required=True);p.add_argument('--elf',type=Path,required=True)
    p.add_argument('--qemu',type=Path,default=Path('C:/temp/h1gba-qemu-build/qemu-system-mipsel.exe'))
    p.add_argument('--port',type=int,default=9057);p.add_argument('--category-y',type=int,default=190)
    p.add_argument('--expected-bda-sha256',help='Refuse to deploy any bytes other than this explicitly selected candidate')
    p.add_argument('--load-timeout',type=int,default=1200);p.add_argument('--observe-seconds',type=int,default=30)
    p.add_argument('--actions',type=Path);p.add_argument('--exit-actions',type=Path)
    p.add_argument('--keep-running-on-failure',action='store_true',help='Leave only this owned guest running for read-only diagnosis')
    p.add_argument('--continue-input',action='store_true',help='Leave the owned guest at its updating title for subsequent native input checks')
    p=sub.add_parser('watch');p.add_argument('--work',type=Path,required=True)
    p.add_argument('--seconds',type=int,default=600);p.add_argument('--interval',type=float,default=10)
    p.add_argument('--until-scene',type=int)
    p.add_argument('--entities',action='store_true',help='Read original entity arrays with an exact-ELF test-only MIPS layout')
    p=sub.add_parser('archive');p.add_argument('--work',type=Path,required=True)
    p=sub.add_parser('add-metadata');p.add_argument('--work',type=Path,required=True)
    p=sub.add_parser('input');p.add_argument('--work',type=Path,required=True);p.add_argument('--actions',type=Path,required=True)
    args=parser.parse_args()
    if args.command=='prepare':prepare(args)
    elif args.command=='finish-prepare':finish_prepare(args)
    elif args.command=='watch':watch(args)
    elif args.command=='archive':archive_current(owned_work(args.work)[0])
    elif args.command=='add-metadata':add_metadata(args)
    elif args.command=='input':native_input(args)
    else:run(args)


if __name__=='__main__':main()
