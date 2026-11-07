"""Reuse the frozen native runtime and codec objects for a small physical probe."""
from pathlib import Path
import json,sys,subprocess,argparse
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build import compiler_flags,run
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--all-o2',action='store_true');args=parser.parse_args()
 out=ROOT/('build/intro-probe-o2' if args.all_o2 else 'build/intro-probe');out.mkdir(exist_ok=True);tc=ROOT.parent/'.tools/toolchain/bin';prefix=Path('C:/temp/h1-pvz-cxx/install');build=ROOT/'build/native-exit-plants'
 common,cpp=compiler_flags(prefix,ROOT/'third_party/zlib-1.3.1',build/'source',optimization='O2')
 common+=['-DPVZ_H1_RAW_TICK_HZ=40','-isystem',str(prefix/'mipsel-none-elf/include')];obj=out/'probe.o';run([tc/'mipsel-none-elf-gcc.exe',*common,'-c',ROOT/'tools/tests/intro_native_probe.c','-o',obj])
 report=json.loads((build/'compile-report.json').read_text());objects=[Path(r['object']) for r in report['records'] if r['source'].startswith('runtime/')]
 # The final game does not link h264bsd; compile the optional reference here.
 for source in sorted((ROOT/'vendor/tinyh264/src').glob('*.c')):
  flags=[f for f in common if f not in ('-O2','-Os')]+['-O2' if args.all_o2 else '-Os']
  codec=out/(source.stem+'.o');run([tc/'mipsel-none-elf-gcc.exe',*flags,'-c',source,'-o',codec]);objects.append(codec)
 elf=out/'probe.elf';raw=out/'probe.bin';lib=prefix/'mipsel-none-elf/lib'
 run([tc/'mipsel-none-elf-g++.exe',*common,*cpp,'-nostdlib','-Wl,--gc-sections','-Wl,--build-id=none','-Wl,-T,'+str(ROOT/'runtime/h1.ld'),'-o',elf,*objects,obj,'-L',lib,'-L',prefix/'lib','-Wl,--start-group','-lstdc++','-lsupc++','-lm','-lc','-lgcc','-Wl,--end-group'])
 run([tc/'mipsel-none-elf-objcopy.exe','-O','binary',elf,raw]);sys.path.insert(0,str(ROOT/'sdk'))
 from h1_bda.header import HeaderFields,encode_header
 from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES,build_diagnostic_resources
 from h1_bda.validate import validate_bda
 payload=raw.read_bytes();pad=(-(PAYLOAD_OFFSET+len(payload)))&3;fields=HeaderFields(category=0x48,file_size_minus_4=PAYLOAD_OFFSET+len(payload)+pad-4,payload_offset=PAYLOAD_OFFSET,resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
 bda=out/'IntroProbe.bda';bda.write_bytes(encode_header(fields,title='开场测速')+build_diagnostic_resources()+payload+bytes(pad));assert validate_bda(bda)['ok'];print(run([tc/'mipsel-none-elf-size.exe',elf]))
if __name__=='__main__':main()
