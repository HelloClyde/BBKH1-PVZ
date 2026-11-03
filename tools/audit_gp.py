"""Audit the H1 IRQ's GP contract and every actual native link input.

The stock V1.41 interrupt entry sets the firmware GP without restoring a
foreground task's GP. The complete native program must therefore avoid all
GP-relative data relocations, including Newlib's MIPS _impure_ptr shortcut.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from build_paths import TOOLCHAIN, RUNTIME_PREFIX
from test_whole_hole import verify_kernel as verify_heap_kernel,expected_words as heap_expected_words

ROOT=Path(__file__).resolve().parents[1]
KERNEL_SHA256='d05786e442f9aad62a8d0a0cb4f6d786bdc7c2fa353a7a2b152c9ed9f01b40ef'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--prefix',type=Path,default=RUNTIME_PREFIX)
    ap.add_argument('--kernel',type=Path,help='Optional pinned firmware for independently rechecking the IRQ contract')
    ap.add_argument('--toolchain',type=Path,default=TOOLCHAIN/'bin')
    ap.add_argument('--report',type=Path,default=ROOT/'build/mips/compile-report.json')
    args=ap.parse_args()
    tc=args.toolchain
    build=json.loads(args.report.read_text(encoding='utf8'))
    if not build.get('linked') or build.get('failed') or len(build.get('records',[]))!=build.get('source_count'):
        raise ValueError('A complete successful native link report is required for the ABI audit')
    paths=[Path(row['object']) for row in build['records'] if row.get('ok')]
    for name in ('libc.a','libm.a'):paths.append(args.prefix/'mipsel-none-elf/lib'/name)
    for name in ('libstdc++.a','libsupc++.a'):paths.append(args.prefix/'lib'/name)
    paths.append(Path(subprocess.check_output([str(tc/'mipsel-none-elf-gcc.exe'),'-msoft-float','-print-libgcc-file-name'],text=True).strip()))
    findings=[]
    for path in paths:
        output=subprocess.check_output([str(tc/'mipsel-none-elf-readelf.exe'),'-r',str(path)],text=True,errors='replace')
        matches=[line.strip() for line in output.splitlines() if re.search(r'R_MIPS_(?:GPREL|GOT|CALL|LITERAL)',line)]
        if matches:findings.append({'input':str(path),'count':len(matches),'examples':matches[:12]})
    digest=None;data_access=None
    evidence='pinned-contract-not-rechecked'
    heap_evidence={'evidence':evidence,'expected_sha256':KERNEL_SHA256,'expected_words':len(heap_expected_words())}
    if args.kernel is not None:
        # An explicit missing file is an error, never a silent evidence downgrade.
        kernel=args.kernel.read_bytes();digest=hashlib.sha256(kernel).hexdigest()
        if digest!=KERNEL_SHA256:raise ValueError('The IRQ ABI evidence only applies to the pinned V1.41 firmware')
        heap_evidence=verify_heap_kernel(args.kernel)
        def word(address):return struct.unpack_from('<I',kernel,address-0x80004000)[0]
        gp_initialization=[word(0x800db264),word(0x800db268)]
        if gp_initialization!=[0x3c1c8058,0x279c2d58]:raise ValueError('Firmware IRQ GP initialization changed')
        restore_gp=[]
        for address in range(0x800db2a4,0x800db350,4):
            instruction=word(address)
            if instruction>>26==35 and (instruction>>21)&31==29 and (instruction>>16)&31==28:restore_gp.append(hex(address))
        if restore_gp:raise ValueError('Reviewed IRQ GP restore assumption changed')
        data_access=[]
        for offset in range(0,0x180000,4):
            instruction=struct.unpack_from('<I',kernel,offset)[0]
            if instruction>>26 in (32,33,34,35,36,37,38,40,41,42,43,46) and (instruction>>21)&31==28:
                data_access.append(hex(0x80004000+offset))
        evidence='verified-pinned-kernel'
    elf=Path(build.get('elf',args.report.parent/'H1PVZPortable.elf'))
    tls=[]
    if not elf.is_file():raise FileNotFoundError('Linked ELF required for native TLS audit: '+str(elf))
    sections=subprocess.check_output([str(tc/'mipsel-none-elf-readelf.exe'),'-S',str(elf)],text=True)
    tls=[line.strip() for line in sections.splitlines() if re.search(r'\.(?:tbss|tdata)(?:\s|\.)',line)]
    report={'firmware_evidence':evidence,'kernel_sha256':digest,'expected_kernel_sha256':KERNEL_SHA256,
            'heap_compatibility':heap_evidence,
            'expected_irq_instructions':{'0x800db264':'0x3c1c8058','0x800db268':'0x279c2d58'},
            'irq_sets_gp':'0x80582d58','irq_normal_return_restores_gp':False,
            'firmware_gp_data_accesses_first_1_5_mib':data_access,'native_link_inputs':len(paths),
            'forbidden_relocation_families':['GPREL*','GOT*','CALL*','LITERAL'],
            'gp_relative_relocations':findings,'native_tls_sections':tls,'ok':not findings and not tls and not data_access}
    path=args.report.parent/'gp-audit.json';path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report,indent=2))
    if not report['ok']:raise SystemExit(1)

if __name__=='__main__':main()
