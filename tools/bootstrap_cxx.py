"""Build pinned hosted C/C++ libraries for the existing bare-metal MIPS GCC.

The compiler installation is left untouched. All sources/builds/installations
live in an explicitly selected path without spaces, because GNU configure and
make do not support this workspace's space-containing path reliably.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request
import shutil

from build_paths import ROOT, TOOLCHAIN, RUNTIME_WORK, MSYS2
SOURCES={
    'newlib-4.6.0.20260123.tar.gz':'https://sourceware.org/pub/newlib/newlib-4.6.0.20260123.tar.gz',
    'gcc-15.2.0.tar.xz':'https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/gcc-15.2.0.tar.xz',
}
EXPECTED_SHA256={
    'newlib-4.6.0.20260123.tar.gz':'6ff27e3bf022666f43f7802255be680eeff722ac181b1725d21e2e8318604ee3',
    'gcc-15.2.0.tar.xz':'438fd996826b0c82485a29da03a72d71d6e3541a83ec702df4271f6fe025d24e',
}
FLAGS="-Os -EL -march=mips32 -mabi=32 -msoft-float -mno-abicalls -G0 -fno-pic -ffunction-sections -fdata-sections -U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__ -D__UINT32_TYPE__='unsigned int'"

def posix(path):
    path=Path(path).absolute().as_posix()
    return '/'+path[0].lower()+path[2:] if len(path)>1 and path[1]==':' else path

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--work',type=Path,default=RUNTIME_WORK)
    ap.add_argument('--toolchain',type=Path,default=TOOLCHAIN,help='Existing GCC15.2 MIPS installation, left unchanged')
    ap.add_argument('--jobs',type=int,default=6)
    ap.add_argument('--only',choices=('newlib','libstdc++','all'),default='all')
    ap.add_argument('--resume',action='store_true',help='Resume make with the already reviewed libstdc++ configuration')
    args=ap.parse_args();work=args.work.resolve()
    if ' ' in str(work):raise ValueError('GNU configure build directory must not contain spaces')
    work.mkdir(parents=True,exist_ok=True)
    def fetch(item):
        name,url=item;path=work/name
        if not path.exists():
            print('Downloading '+url,flush=True)
            # Windows curl uses the OS trust store; Conda's isolated CA bundle
            # does not include every intermediate served by sourceware.org.
            subprocess.run([shutil.which('curl.exe') or shutil.which('curl') or 'curl','--fail','--location','--retry','3',
                '--connect-timeout','30','--silent','--show-error','--output',str(path.with_suffix(path.suffix+'.partial')),url],check=True)
            path.with_suffix(path.suffix+'.partial').replace(path)
        digest=hashlib.sha256(path.read_bytes()).hexdigest()
        if digest!=EXPECTED_SHA256[name]:raise ValueError('Pinned archive checksum mismatch: '+name)
        return {'file':name,'url':url,'sha256':digest}
    with ThreadPoolExecutor(max_workers=2) as pool:provenance=list(pool.map(fetch,SOURCES.items()))
    for row in provenance:
        name=row['file'];folder=work/name.replace('.tar.gz','').replace('.tar.xz','')
        if not folder.exists():
            print('Extracting '+name,flush=True)
            with tarfile.open(work/name) as archive:
                if name.startswith('gcc'):
                    # Only directories used by standalone libstdc++ configure.
                    selected=[m for m in archive.getmembers() if '/libstdc++-v3/' in m.name or
                              m.name.count('/')<=1 or
                              m.name.startswith('gcc-15.2.0/config/') or m.name.startswith('gcc-15.2.0/include/') or
                              m.name.startswith('gcc-15.2.0/libgcc/') or m.name.startswith('gcc-15.2.0/libbacktrace/') or m.name.startswith('gcc-15.2.0/libiberty/') or m.name.startswith('gcc-15.2.0/contrib/') or
                              m.name in ('gcc-15.2.0/COPYING','gcc-15.2.0/COPYING3','gcc-15.2.0/COPYING.RUNTIME')]
                    archive.extractall(work,members=selected,filter='data')
                else:archive.extractall(work,filter='data')
    tc=work/'tc';toolchain=args.toolchain.resolve()
    if not (toolchain/'bin/mipsel-none-elf-g++.exe').is_file():raise ValueError('The selected MIPS GCC installation is incomplete')
    if tc.exists():
        if tc.resolve()!=toolchain:raise ValueError('Existing tc link/directory does not point at the selected toolchain; choose a new work directory')
    elif os.name=='nt':
        subprocess.run(['cmd.exe','/d','/c','mklink','/J',str(tc),str(toolchain)],check=True,capture_output=True,text=True)
    else:tc.symlink_to(toolchain,target_is_directory=True)
    support=['gcc/DATESTAMP','gcc/BASE-VER','gcc/DEV-PHASE','libgcc/gthr.h','libgcc/gthr-single.h','libgcc/gthr-posix.h','libgcc/unwind-pe.h','libiberty/cp-demangle.c','contrib/relpath.sh','mkinstalldirs']
    if any(not (work/'gcc-15.2.0'/name).exists() for name in support):
        with tarfile.open(work/'gcc-15.2.0.tar.xz') as archive:
            metadata=[member for member in archive.getmembers() if member.name in ['gcc-15.2.0/'+name for name in support] or
                      member.name.startswith('gcc-15.2.0/libgcc/') or member.name.startswith('gcc-15.2.0/libbacktrace/') or member.name.startswith('gcc-15.2.0/libiberty/') or member.name.startswith('gcc-15.2.0/contrib/')]
            archive.extractall(work,members=metadata,filter='data')
    # V1.41's IRQ entry resets $gp and does not restore an interrupted task's
    # value. All native code must therefore be -G0 without explicit .sdata
    # declarations, including Newlib's otherwise forced MIPS _impure_ptr.
    config=work/'newlib-4.6.0.20260123/newlib/libc/include/sys/config.h'
    text=config.read_text(encoding='utf8')
    old='#if defined(__mips__) && !defined(__rtems__)\n#define __ATTRIBUTE_IMPURE_PTR__ __attribute__((__section__(".sdata")))\n#endif'
    new='#if defined(__mips__) && !defined(__rtems__)\n#define __ATTRIBUTE_IMPURE_PTR__ /* H1_IRQ_CLOBBERS_GP: always address _impure_ptr with full relocations. */\n#endif'
    if old in text:config.write_text(text.replace(old,new),encoding='utf8',newline='\n')
    elif 'H1_IRQ_CLOBBERS_GP' not in text:raise ValueError('Newlib MIPS _impure_ptr attribute patch context changed')
    env=os.environ.copy()
    env['PATH']=str(tc/'bin')+os.pathsep+str(MSYS2/'usr/bin')+os.pathsep+env.get('PATH','')
    env['CFLAGS_FOR_TARGET']=FLAGS
    env['CXXFLAGS_FOR_TARGET']=FLAGS
    prefix=work/'install'
    def run(script,folder,name):
        folder.mkdir(exist_ok=True)
        log=work/(name+'.log')
        print(name+' -> '+str(log),flush=True)
        with log.open('w',encoding='utf8') as stream:
            subprocess.run([str(MSYS2/'usr/bin/bash.exe'),'-c',script],cwd=folder,env=env,stdout=stream,stderr=subprocess.STDOUT,check=True)
    prefixp=posix(prefix);tcp=posix(tc)
    # The bundled cross-toolchain also ships Cygwin cat/sh utilities. Keep
    # MSYS tools first so configure never mixes two POSIX runtime families.
    common=f'export PATH="{posix(MSYS2/"ucrt64/bin")}:/usr/bin:{tcp}/bin:$PATH"; '
    if args.only in ('newlib','all'):
        source=posix(work/'newlib-4.6.0.20260123')
        configure_newlib=(f'"{source}/configure" --target=mipsel-none-elf --prefix="{prefixp}" --disable-nls --disable-multilib '
            '--disable-newlib-supplied-syscalls --disable-libgloss --enable-newlib-io-c99-formats --enable-newlib-io-long-long '
            '--enable-newlib-io-float')
        if args.resume:configure_newlib='true'
        run(common+configure_newlib+f' && make -j{args.jobs} CFLAGS_FOR_TARGET="{FLAGS}" all-target-newlib && make install-target-newlib',work/'build-newlib','newlib')
    if args.only in ('libstdc++','all'):
        # H1 supplies real native directory enumeration through this POSIX ABI.
        # Newlib's default bare-metal sys/dirent.h is only an unsupported stub.
        shutil.copyfile(ROOT/'runtime/include/sys/dirent.h',prefix/'mipsel-none-elf/include/sys/dirent.h')
        source=posix(work/'gcc-15.2.0/libstdc++-v3')
        sysroot=posix(prefix/'mipsel-none-elf')
        cf=FLAGS+' -I'+sysroot+'/include'
        configure=(f'"{source}/configure" --host=mipsel-none-elf --target=mipsel-none-elf --with-target-subdir=mipsel-none-elf --prefix="{prefixp}" '
            f'--with-newlib --with-gxx-include-dir="{prefixp}/mipsel-none-elf/include/c++/15.2.0" '
            '--disable-multilib --disable-nls --disable-shared --disable-libstdcxx-pch '
            '--enable-libstdcxx-time=no --disable-libstdcxx-filesystem-ts --disable-libstdcxx-backtrace')
        if args.resume:configure='true'
        run(common+f'export CC=mipsel-none-elf-gcc CXX=mipsel-none-elf-g++ AR=mipsel-none-elf-ar RANLIB=mipsel-none-elf-ranlib; '
            'export ac_cv_header_dirent_h=yes ac_cv_member_struct_dirent_d_type=yes '
            'ac_cv_func_fcntl=yes ac_cv_func_getexecname=no glibcxx_cv_sys_filetype=elf32 '
            'lt_cv_path_LD=mipsel-none-elf-ld lt_cv_path_LDCXX=mipsel-none-elf-ld; '
            f'export CFLAGS="{cf}" CXXFLAGS="{cf}"; '+
            configure+f' && make -j{args.jobs} CFLAGS="{cf}" CXXFLAGS="{cf}" && make install',work/'build-libstdcxx','libstdcxx')
    report={'compiler':str(tc),'compiler_source':str(toolchain),'prefix':str(prefix),'target':'mipsel-none-elf',
            'flags':FLAGS,'sources':provenance,'newlib_h1_patch':'Remove forced MIPS _impure_ptr .sdata because the stock IRQ clobbers GP',
            'newlib_config_sha256':hashlib.sha256(config.read_bytes()).hexdigest()}
    (ROOT/'build').mkdir(exist_ok=True)
    (ROOT/'build/cxx-toolchain.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report,indent=2),flush=True)

if __name__=='__main__':main()
