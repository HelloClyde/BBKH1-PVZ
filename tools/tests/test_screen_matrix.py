"""Bit-exact host and isolated real MIPS/libgcc affine screen-scale oracle.

No firmware, device, emulator service, or game state is accessed.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,random,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/screen-matrix-probe'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def bits(f):return struct.unpack('<I',struct.pack('<f',f))[0]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native-only',action='store_true');p.add_argument('--header',type=Path,default=ROOT/'platform/graphics/PhysicalMatrixScale.h');a=p.parse_args()
 OUT.mkdir(exist_ok=True)
 source=Path(__file__).with_name('screen_matrix_probe.cpp');generic=ROOT/'upstream/src/SexyAppFramework/misc/SexyMatrix.cpp'
 rng=random.Random(0x20261102);cases=[]
 def add(m,sx=bits(360/800),sy=bits(270/600)):cases.append([sx,sy,*m])
 edges=[0,0x80000000,0x3f800000,0xbf800000,0x3ee66666,0xbee66666,0x00800000,0x80800000,0x007fffff,1,0x80000001,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345]
 for slot in range(9):
  for value in edges:
   m=[bits(1),0,bits(480),0,bits(1),bits(-272),0,0,bits(1)];m[slot]=value;add(m)
 for sx in edges:
  for sy in edges:add([bits(-1),0,bits(800),0x80000000,bits(2),bits(-600),0,0,bits(1)],sx,sy)
 # Real logical canvas scales, translation/clip edges, mirrors, rotation/shear.
 import math
 for deg in (-180,-90,-45,-1,0,1,30,45,90,180):
  c,s=math.cos(math.radians(deg)),math.sin(math.radians(deg))
  for x in (-801,-800,-1,-0.0,0.0,0.5,359.5,360,799.5,800,801):
   for y in (-601,-600,-1,-0.0,0.0,0.5,269.5,270,599.5,600,601):add([bits(c),bits(-s),bits(x),bits(s),bits(c),bits(y),0,0,bits(1)])
 for i in range(5000):
  # All finite exponents, canonical affine bottom, with +/-0 mixed in.
  m=[rng.getrandbits(32) for _ in range(6)]+[0,0,bits(1)]
  for j in range(6):
   if (m[j]&0x7f800000)==0x7f800000:m[j]^=0x00800000
   if rng.randrange(12)==0:m[j]&=0x80000000
  add(m)
 native_cases=cases.copy()
 for i in range(50000):add([bits(rng.uniform(-2048,2048)) for _ in range(6)]+[0,0,bits(1)])
 def write(name,rows):
  file=OUT/name;file.write_bytes(b''.join(struct.pack('<11I',*r) for r in rows));return file
 input_path=write('host-input.bin',cases)
 def check(data,count):
  assert len(data)==count*76
  accepted=0
  for i in range(count):
   row=struct.unpack_from('<19I',data,i*76);accepted+=row[0]
   assert row[1:10]==row[10:19],(i,cases[i],row)
  return accepted
 report={'scope':'Actual upstream generic matrix versus guarded six-product candidate; host bit equality and isolated MIPS software-float execution, not a device benchmark','inputs':{str(v.relative_to(ROOT)):sha(v) for v in (source,generic,a.header)}}
 if not a.native_only:
  cxx=Path('C:/msys64/ucrt64/bin/g++.exe');env=dict(os.environ);env['PATH']=str(cxx.parent)+os.pathsep+env.get('PATH','')
  exe=OUT/'host.exe';cmd=[str(cxx),'-std=c++20','-Os','-fno-fast-math','-ffp-contract=off','-ffunction-sections','-fdata-sections','-DPVZH1_MATRIX_HOST','-I',str(a.header.parent),'-I',str(generic.parent),str(source),str(generic),'-Wl,--gc-sections','-o',str(exe)]
  subprocess.run(cmd,check=True,env=env);subprocess.run([str(exe),str(input_path),str(OUT/'host-output.bin')],check=True,env=env)
  report['host']={'passed':True,'cases':len(cases),'fast_accepts':check((OUT/'host-output.bin').read_bytes(),len(cases)),'exe_sha256':sha(exe),'command':cmd}
 # Build only this algorithm with actual o32 soft-float compiler and libgcc.
 spec=importlib.util.spec_from_file_location('pvzbuild',ROOT/'tools/build.py');b=importlib.util.module_from_spec(spec);sys.path.insert(0,str(ROOT/'tools'));spec.loader.exec_module(b)
 common,cpp=b.compiler_flags(Path('C:/temp/h1-pvz-cxx/install'),ROOT/'vendor/zlib',ROOT/'build/native-hardware-opt-raster/source')
 compiler=ROOT.parent/'.tools/toolchain/bin/mipsel-none-elf-g++.exe';elf=OUT/'native.elf'
 cmd=[str(compiler),*common,*cpp,'-fno-exceptions','-fno-rtti','-I',str(a.header.parent),'-I',str(generic.parent),str(source),str(generic),'-nostdlib','-Wl,-Ttext=0x10000000,-e,matrix_evaluate,--gc-sections','-LC:/temp/h1-pvz-cxx/install/mipsel-none-elf/lib','-Wl,--start-group','-lc','-lgcc','-Wl,--end-group','-o',str(elf)]
 subprocess.run(cmd,check=True)
 nm=ROOT.parent/'.tools/toolchain/bin/mipsel-none-elf-nm.exe';symbols={}
 for line in subprocess.check_output([str(nm),'-S',str(elf)],text=True).splitlines():
  f=line.split()
  if len(f)==4:symbols[f[3]]=(int(f[0],16),int(f[1],16))
 raw=elf.read_bytes();eh=struct.unpack_from('<16sHHIIIIIHHHHHH',raw)
 from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS32,UC_MODE_LITTLE_ENDIAN,UC_HOOK_CODE
 from unicorn.mips_const import UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_SP,UC_MIPS_REG_RA,UC_MIPS_REG_PC
 u=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_LITTLE_ENDIAN);u.mem_map(0x0fff0000,0x210000);u.mem_map(0x00400000,0x1000);u.mem_map(0x20000000,0x10000);u.mem_map(0x21000000,0x200000)
 for i in range(eh[10]):
  ph=struct.unpack_from('<IIIIIIII',raw,eh[5]+i*eh[9])
  if ph[0]==1:u.mem_write(ph[2],raw[ph[1]:ph[1]+ph[4]])
 native_input=write('native-input.bin',native_cases);u.mem_write(0x21000000,native_input.read_bytes())
 u.reg_write(UC_MIPS_REG_A0,0x21000000);u.reg_write(UC_MIPS_REG_A1,0x21100000);u.reg_write(UC_MIPS_REG_A2,len(native_cases));u.reg_write(UC_MIPS_REG_SP,0x2000fff0);u.reg_write(UC_MIPS_REG_RA,0x101f0000)
 started=time.monotonic();u.emu_start(symbols['matrix_evaluate'][0],0x101f0000,timeout=60000000);assert u.reg_read(UC_MIPS_REG_PC)==0x101f0000
 result=bytes(u.mem_read(0x21100000,len(native_cases)*76));(OUT/'native-output.bin').write_bytes(result)
 report['native']={'passed':True,'cases':len(native_cases),'fast_accepts':check(result,len(native_cases)),'elapsed_seconds_host_emulation':time.monotonic()-started,'elf_sha256':sha(elf),'command':cmd,'library':'actual toolchain libgcc o32 soft-float; no firmware or device access'}
 # Instruction and libgcc invocation count on one ordinary affine case, same VM.
 costs={};original=struct.pack('<9f',1,-0.0,400,0,1,300,0,0,1)
 for entry in ('original_scale','candidate_scale'):
  u=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_LITTLE_ENDIAN);u.mem_map(0x0fff0000,0x210000);u.mem_map(0x00400000,0x1000);u.mem_map(0x20000000,0x10000);u.mem_map(0x21000000,0x200000)
  for i in range(eh[10]):
   ph=struct.unpack_from('<IIIIIIII',raw,eh[5]+i*eh[9])
   if ph[0]==1:u.mem_write(ph[2],raw[ph[1]:ph[1]+ph[4]])
  u.mem_write(0x21000000,original);u.reg_write(UC_MIPS_REG_A0,0x21000000);u.reg_write(UC_MIPS_REG_A1,bits(.45));u.reg_write(UC_MIPS_REG_A2,bits(.45));u.reg_write(UC_MIPS_REG_SP,0x2000fff0);u.reg_write(UC_MIPS_REG_RA,0x101f0000)
  counts={'instructions':0,'mulsf3_calls':0,'addsf3_calls':0}
  def hook(uc,pc,size,user):
   counts['instructions']+=1
   for name,key in [('__mulsf3','mulsf3_calls'),('__addsf3','addsf3_calls')]:
    if pc==symbols[name][0]:counts[key]+=1
  h=u.hook_add(UC_HOOK_CODE,hook);u.emu_start(symbols[entry][0],0x101f0000);u.hook_del(h);assert counts['instructions']>0;costs[entry]=counts
 report['native_single_call_instructions_not_hardware_cycles']=costs
 # Compile the real platform TU with the same full native profile flags too.
 platform_source=ROOT/'platform/graphics/PhysicalScreen.cpp';platform_object=OUT/'PhysicalScreen.o'
 command=[str(compiler),*common,*cpp,'-DPVZH1_PHYSICAL_SCREEN','-DPVZ_H1_RAW_TICK_HZ=40','-DPVZ_NATIVE_PROFILE','-DPVZ_NATIVE_PROFILE_RAW_TICKS','-c',str(platform_source),'-o',str(platform_object)]
 subprocess.run(command,check=True)
 prior=json.loads((ROOT/'build/native-hardware-opt-raster/compile-report.json').read_text())
 old_object=Path(next(r['object'] for r in prior['records'] if r['source']=='platform/graphics/PhysicalScreen.cpp'))
 size_tool=ROOT.parent/'.tools/toolchain/bin/mipsel-none-elf-size.exe'
 def text_size(obj):
  return sum(int(f[1]) for line in subprocess.check_output([str(size_tool),'-A',str(obj)],text=True).splitlines() if (f:=line.split()) and len(f)>=2 and f[0].startswith('.text'))
 old_size,new_size=text_size(old_object),text_size(platform_object)
 report['actual_platform_tu']={'passed':True,'source_sha256':sha(platform_source),'object_sha256':sha(platform_object),'previous_text_bytes':old_size,'text_bytes':new_size,'unlinked_text_delta':new_size-old_size,'command':command}
 report['passed']=True;(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k not in ('inputs',)}))
if __name__=='__main__':main()
