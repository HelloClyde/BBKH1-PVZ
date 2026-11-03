"""Test the disabled paged prototype against the exact pinned DataArray."""
from pathlib import Path
import importlib.util,os,shutil,subprocess,json,hashlib
ROOT=Path(__file__).resolve().parents[3]
BUILD=ROOT/'build/data-array-tests';BUILD.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('paged',ROOT/'platform/graphics/data_array_patches.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
original=(ROOT/'upstream/src/PvzpLib/DataArray.h').read_text()
(BUILD/'original.h').write_text(original)
(BUILD/'PvzpDebug.h').write_text('#pragma once\n#include <cassert>\n#define PVZP_ASSERT(condition, ...) assert(condition)\n')
(BUILD/'PvzpCommon.h').write_text('#pragma once\n')
cc=shutil.which('g++') or 'g++'
records=[]
for page_size in (32,64):
    header=BUILD/'paged.h';header.write_text(original);patch.apply_to_header(header,page_size)
    text=header.read_text();start=text.index('enum\n{');end=text.index('\n};',start)+3
    header.write_text(text[:start]+text[end:])
    exe=BUILD/f'test-pages-{page_size}.exe'
    subprocess.run([cc,'-std=c++20','-O1','-g','-static-libstdc++','-static-libgcc','-I',str(BUILD),
        str(ROOT/'platform/graphics/tests/test_data_array.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    records.append({'page_size':page_size,'passed':True,'header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),
                    'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
    print('page_size='+str(page_size))
(BUILD/'verification.json').write_text(json.dumps({'scope':'Generic DataArray differential tests against pinned original header; complete-game integration is tested separately.',
    'original_header_sha256':hashlib.sha256(original.encode()).hexdigest(),'logical_capacity':1024,
    'random_alloc_free_operations':100000,'explicit_id_wrap_operations':70000,
    'checks':['IDs','free-list','high-water','iterator order','stable live pointers','inactive slot save access','restored slot access','bad_alloc before metadata mutation','pages retained until dispose'],
    'runs':records},indent=2)+'\n')
