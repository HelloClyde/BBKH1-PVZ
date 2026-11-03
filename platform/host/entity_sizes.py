"""Read-only ABI size probe; never prepares or changes either game source tree."""
from pathlib import Path
import importlib.util,json,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/data-array-tests';BUILD.mkdir(parents=True,exist_ok=True)
types={'Zombie':1024,'Plant':1024,'Projectile':1024,'Coin':1024,'LawnMower':32,'GridItem':128,
       'Reanimation':1024,'PvzpParticleSystem':1024,'PvzpParticleEmitter':1024,'PvzpParticle':1024,
       'Trail':1024,'Attachment':1024}
includes='''#include "Lawn/Zombie.h"
#include "Lawn/Plant.h"
#include "Lawn/Projectile.h"
#include "Lawn/Coin.h"
#include "Lawn/LawnMower.h"
#include "Lawn/GridItem.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpParticle.h"
#include "PvzpLib/Trail.h"
#include "PvzpLib/Attachment.h"
'''
source=BUILD/'entity-sizes.cpp'
source.write_text(includes+'\nextern "C" {\n'+''.join(f'char pvzh1_size_{name}[sizeof({name})]={{0}};\n' for name in types)+'}\n')
spec=importlib.util.spec_from_file_location('native_build',ROOT/'tools/build.py')
sys.path.insert(0,str(ROOT/'tools'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
prefix=Path('C:/temp/h1-pvz-cxx/install');tc=ROOT.parent/'.tools/toolchain/bin'
common,cpp=module.compiler_flags(prefix,ROOT/'third_party/zlib-1.3.1',ROOT/'build/source')
obj=BUILD/'entity-sizes-native.o'
subprocess.run([str(tc/'mipsel-none-elf-g++.exe'),*common,*cpp,'-c',str(source),'-o',str(obj)],check=True)
symbols=subprocess.check_output([str(tc/'mipsel-none-elf-nm.exe'),'-S',str(obj)],text=True)
sizes={}
for line in symbols.splitlines():
    words=line.split()
    if len(words)==4 and words[-1].startswith('pvzh1_size_'):sizes[words[-1][11:]]=int(words[1],16)
assert set(sizes)==set(types)
report={'target':'native MIPS o32 exact sizeof(T), derived DataArrayItem adds no members',
    'types':{name:{'sizeof':sizes[name],'logical_capacity':capacity,'original_payload_bytes':sizes[name]*capacity,
        'first_32_slot_page_bytes':sizes[name]*min(32,capacity),'first_64_slot_page_bytes':sizes[name]*min(64,capacity)}
        for name,capacity in types.items()}}
report['original_payload_bytes']=sum(row['original_payload_bytes'] for row in report['types'].values())
report['all_one_32_slot_page_bytes']=sum(row['first_32_slot_page_bytes'] for row in report['types'].values())
report['all_one_64_slot_page_bytes']=sum(row['first_64_slot_page_bytes'] for row in report['types'].values())
(BUILD/'entity-sizes.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='types'}))
