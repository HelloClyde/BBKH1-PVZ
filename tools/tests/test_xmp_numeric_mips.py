"""Compile only the two numerical TUs with frozen target flags; no BDA/run."""
from pathlib import Path
import hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/xmp-numeric-candidate'
report=json.loads((ROOT/'build/native-hardware-opt-hot32/compile-report.json').read_text())
flags=report['flags'][:report['flags'].index('-std=gnu++20')]
flags+=['-std=gnu99','-isystem','C:/temp/h1-pvz-cxx/install/mipsel-none-elf/include','-I'+str(ROOT/'platform/audio'),'-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare']
tc=ROOT.parent/'.tools/toolchain/bin';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def run(command):
    r=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    return r.stdout
results=[]
for mode in ('pristine','candidate'):
    for name in ('period','mixer'):
        src=ROOT/'vendor/libxmp/src'/(name+'.c') if mode=='pristine' else OUT/'h1-xmp-numeric'/(name+'.c')
        obj=OUT/(mode+'-'+name+'-mips.o')
        result=run([tc/'mipsel-none-elf-gcc.exe',*flags,'-c',src,'-o',obj])
        (OUT/(mode+'-'+name+'-mips.log')).write_text(result)
        assembly=run([tc/'mipsel-none-elf-objdump.exe','-dr',obj]);(OUT/(mode+'-'+name+'-mips.asm')).write_text(assembly)
        nm=run([tc/'mipsel-none-elf-nm.exe','-S',obj]);undefined=run([tc/'mipsel-none-elf-nm.exe','-u',obj])
        rows=[]
        for line in nm.splitlines():
            m=re.match(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+(\w)\s+(.+)$',line)
            if m and m[4] in ('h1_note_cache','h1_step_cache','libxmp_note_to_period_mix','libxmp_mixer_softmixer','h1_xmp_note_read','h1_xmp_step_read'):
                rows.append(dict(symbol=m[4],bytes=int(m[2],16),type=m[3]))
        results.append(dict(mode=mode,translation_unit=name,source_sha256=sha(src),object_sha256=sha(obj),symbols=rows,undefined_imports=undefined.splitlines()))
cache_bytes=sum(s['bytes'] for r in results if r['mode']=='candidate' for s in r['symbols'] if s['symbol'] in ('h1_note_cache','h1_step_cache'))
assert cache_bytes==3624,cache_bytes
for name in ('period','mixer'):
    old=next(r for r in results if r['mode']=='pristine' and r['translation_unit']==name)
    new=next(r for r in results if r['mode']=='candidate' and r['translation_unit']==name)
    added={s.split()[-1] for s in new['undefined_imports']}-{s.split()[-1] for s in old['undefined_imports']}
    assert added<={'memset','memcpy'},added
proxy=json.loads((OUT/'report.json').read_text())
asm=(OUT/'candidate-period-mips.asm').read_text()
section=asm.split('00000000 <libxmp_note_to_period_mix>:',1)[1].split('Disassembly of section',1)[0]
instructions={int(m[1],16):(m[2],m[3]) for m in re.finditer(r'^[ \t]*([0-9a-f]+):[ \t]+[0-9a-f]{8}[ \t]+(\w+)[ \t]*([^\r\n]*)$',section,re.M)}
for pc,op,target in ((0xf4,'beqz','190'),(0x100,'bne','190'),(0x10c,'bne','190'),(0x13c,'beq','184'),(0x188,'b','148'),(0x198,'bne','d8')):
    assert instructions[pc][0]==op and target in instructions[pc][1],(pc,instructions.get(pc))
def addresses(a,b):return list(range(a,b+4,4))
paths={'way0':addresses(0,0x180)}
for why,last in (('empty_first_way',0xf8),('first_note_mismatch',0x104),('first_bend_mismatch',0x110)):
    paths['way1_'+why]=addresses(0,last)+addresses(0x190,0x19c)+addresses(0xd8,0x140)+addresses(0x184,0x18c)+addresses(0x148,0x180)
for path in paths.values():assert all(pc in instructions and instructions[pc][0] not in ('jal','jalr') for pc in path)
assert {name:len(path) for name,path in paths.items()}=={'way0':97,'way1_empty_first_way':112,'way1_first_note_mismatch':115,'way1_first_bend_mismatch':118}
menu=next(r for r in proxy['reports'] if r['kind']=='menu')
baseline=menu['pristine'][0]['pow_calls'];candidate=menu['candidate'][0]['pow_calls']
result=dict(passed=True,scope='Real target machine code size/import evidence and exact host call-count proxy; no target execution or FPS claim.',
    strict_MIPS_compile=True,production_stats_disabled=True,static_cache_bytes=cache_bytes,
    note_hit_instruction_proxy=dict(method='Explicitly enumerated, branch-delay-inclusive paths in this actual -Os MIPS object; no callee or cycle estimate.',
        instructions={name:len(path) for name,path in paths.items()},external_calls_on_hit=0,
        pristine_direct_instructions=51,pristine_external_calls={'__floatsidf':2,'__divdf3':3,'__adddf3':1,'pow':1}),
    menu_pow_calls=dict(pristine=baseline,candidate=candidate,removed=baseline-candidate,reduction_percent=100*(baseline-candidate)/baseline),
    menu_step_original_expression=dict(calls=menu['candidate'][0]['step_calls'],hits_skipping_original_expression=menu['candidate'][0]['step_hits']),
    target_objects=results,native_compile_flags=flags,
    limitations=['Call counts are an instruction-work proxy; they are not measured MIPS cycles or GUI time.',
        'Cache hits avoid pow plus the original period conversion expressions, and skip the original step multiply/divides. Integer hash/bit-key comparisons and replacement-bit updates remain.',
        'Position/ceil divisions, sample-phase progression, interpolation and every original mixer update remain unchanged.'])
(OUT/'mips-proxy.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(passed=True,static_cache_bytes=cache_bytes,menu_pow_calls=result['menu_pow_calls'],objects=[dict(mode=r['mode'],unit=r['translation_unit'],symbols=r['symbols']) for r in results],report_sha256=sha(OUT/'mips-proxy.json')),indent=2))
