"""Bake the full first-player opening into final-size native frame deltas."""
import argparse,hashlib,json,os,struct,subprocess,zlib,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PIXELS=480*272

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def export(host,out,cxx,replay=False):
 report=json.loads((host/'compile-report.json').read_text());assert report['linked'] and not report['failed']
 out.mkdir(parents=True,exist_ok=True)
 for name in ('tmp','save','run'):(out/name).mkdir(exist_ok=True)
 env=os.environ.copy();env['PATH']=str(Path(cxx).parent)+os.pathsep+env.get('PATH','');env['TEMP']=env['TMP']=str(out/'tmp')
 objects=[]
 for row in report['records']:
  name=row['source'];assert sha(ROOT/name)==row['sha256'],name
  if name!='platform/host/host_io.cpp':objects.append(host/(hashlib.sha1(name.encode()).hexdigest()[:10]+'-'+Path(name).stem+'.o'))
 io=(ROOT/'platform/host/host_io.cpp').read_text(encoding='utf-8')
 anchor='if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}'
 assert io.count(anchor)==1
 io='extern "C" void pvzh1_first_intro_export();\n'+io.replace(anchor,'if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_first_intro_export();std::exit(0);}')
 observer=out/'observer.cpp';observer.write_text(io,encoding='utf-8')
 def run(command,log):
  with (out/log).open('w',encoding='utf-8') as stream:r=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=stream,stderr=stream)
  if r.returncode:raise RuntimeError(str(out/log)+' exit '+str(r.returncode))
 for source in (observer,ROOT/('tools/tests/first_intro_playback.cpp' if replay else 'tools/tests/first_intro_export.cpp')):
  obj=out/(source.stem+'.o');run([cxx,*report['flags'],'-std=c++20','-c',source,'-o',obj],source.stem+'.compile.log');objects.append(obj)
 exe=out/'export.exe';run([cxx,'-Wl,--gc-sections','-static-libstdc++','-static-libgcc','-o',exe,*objects,'-lws2_32','-luser32','-lgdi32','-lwinmm','-limm32','-lshlwapi','-lshell32','-lpsapi'],'link.log')
 for name in list(env):
  if name.startswith('PVZ_HOST_'):del env[name]
 assert out.is_relative_to(ROOT/'build'), 'Generated files must stay under build/'
 with tempfile.TemporaryDirectory(prefix='session-',dir=out/'save') as fresh_save:
  env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources-h1-wide').as_posix()+'/',PVZ_SAVE_DIR=Path(fresh_save).as_posix()+'/',PVZ_HOST_OUTPUT=(out/'run').as_posix(),PVZ_HOST_MAX_FRAMES='600',PVZ_HOST_SPEED='100',PVZ_HOST_SELECTOR_BUNDLE=(ROOT/'private/resources-h1-wide/h1menu/selector-wide.h1mb').as_posix())
  run([exe],'console.log')
 proof=json.loads((out/'run/first-intro-export.json').read_text());assert proof['passed'] and proof['frames']==348
 print(json.dumps(proof),flush=True)

def encode_h264(out,output):
 from importlib.util import spec_from_file_location,module_from_spec
 import ctypes,re
 ffmpeg=Path(r'D:/Program Files/ffmpeg-5.1.2-essentials_build/bin/ffmpeg.exe')
 source=out/'run/first-intro.argb';proof=json.loads((out/'run/first-intro-export.json').read_text())
 assert source.stat().st_size==proof['frames']*PIXELS*4
 encoded=out/'first-intro.h264'
 params='aud=1:repeat-headers=1:keyint=50:scenecut=0:bframes=0:cabac=0:ref=1:weightp=0:no-deblock=1'
 subprocess.run([str(ffmpeg),'-v','error','-f','rawvideo','-pixel_format','bgra','-video_size','480x272','-framerate','25','-i',str(source),'-frames:v','348','-an','-c:v','libx264','-threads','2','-preset','ultrafast','-tune','fastdecode','-crf','20','-profile:v','baseline','-pix_fmt','yuv420p','-color_range','tv','-x264-params',params,'-f','h264','-y',str(encoded)],check=True)
 data=encoded.read_bytes();starts=[m.start() for m in re.finditer(b'\x00\x00\x00\x01\x09',data)]
 assert starts[0]==0 and len(starts)==348
 aus=[data[a:b] for a,b in zip(starts,starts[1:]+[len(data)])]
 reference=subprocess.run([str(ffmpeg),'-v','error','-i',str(encoded),'-pix_fmt','yuv420p','-f','framemd5','-'],check=True,capture_output=True).stdout
 hashes=[line.split(b',')[-1].strip().decode() for line in reference.splitlines() if line and not line.startswith(b'#')]
 libpath=out/'h264bsd.dll';env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-shared','-O2',*[str(p) for p in sorted((ROOT/'vendor/tinyh264/src').glob('*.c'))],'-o',str(libpath)],check=True,env=env)
 lib=ctypes.CDLL(str(libpath));lib.h264bsdAlloc.restype=ctypes.c_void_p
 lib.h264bsdInit.argtypes=[ctypes.c_void_p,ctypes.c_uint32];lib.h264bsdDecode.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint32,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint32)]
 lib.h264bsdShutdown.argtypes=lib.h264bsdFree.argtypes=[ctypes.c_void_p]
 decoder=lib.h264bsdAlloc();assert decoder and lib.h264bsdInit(decoder,1)==0
 decoded=hashlib.sha256();fnvs=[]
 try:
  for i,au in enumerate(aus):
   buf=ctypes.create_string_buffer(au+b'\0'*32);pic=ctypes.c_void_p();w=ctypes.c_uint32();h=ctypes.c_uint32()
   status=lib.h264bsdDecode(decoder,buf,len(au),ctypes.byref(pic),ctypes.byref(w),ctypes.byref(h))
   assert status==1 and (w.value,h.value)==(480,272),(i,status,w.value,h.value)
   yuv=ctypes.string_at(pic.value,PIXELS*3//2);assert hashlib.md5(yuv).hexdigest()==hashes[i],i;decoded.update(yuv)
   if 150<=i<166:
    fnv=2166136261
    for b in yuv:fnv=((fnv^b)*16777619)&0xffffffff
    fnvs.append(fnv)
 finally:lib.h264bsdShutdown(decoder);lib.h264bsdFree(decoder)
 body=b''.join(struct.pack('<I',len(au))+au for au in aus)
 header=struct.pack('<16I',0x56493148,1,480,272,len(aus),40,13890,max(map(len,aus)),len(body),zlib.crc32(body),0,0,0,0,0,0)
 output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(header+body)
 (out/'intro-benchmark.h1iv').write_bytes(b''.join(struct.pack('<I',len(au))+au for au in aus[150:166]))
 # Benchmark starts with a real IDR at frame150 and includes 15 dependent frames.
 result={**proof,'codec':'H.264 constrained baseline CAVLC one reference no deblock','crf':20,'encoder_parameters':params,'bytes':output.stat().st_size,'sha256':sha(output),'all_frames_match_ffmpeg':len(aus),'decoded_yuv_sha256':decoded.hexdigest(),'benchmark_frame_fnv1a':fnvs,'benchmark_frames':16,'encoder_sha256':sha(Path(__file__)),'exporter_sha256':sha(ROOT/'tools/tests/first_intro_export.cpp')}
 (out/'encoded-verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result),flush=True)

def colour_header():
 low=[((x>>5)<<10)|((x&31)<<3)|((x&31)>>2) for x in range(256)]
 high=[(((x&248)|(x>>5))<<16)|((((x&7)<<5)|((x&6)>>1))<<8) for x in range(256)]
 text='/* Generated offline by tools/bake_first_intro.py: exact RGB565 display words. */\n'
 for name,values in [('low',low),('high',high)]:
  text+='static const uint32_t '+name+'[256]={\n'
  for i in range(0,256,8):text+=' '+','.join('0x%08xu'%v for v in values[i:i+8])+',\n'
  text+='};\n'
 return text.encode('ascii')

def encode(out,output):
 import ctypes
 import lz4.block
 assert (ROOT/'platform/video/IntroColours.h').read_bytes()==colour_header()
 proof=json.loads((out/'run/first-intro-export.json').read_text());source=out/'run/first-intro.argb'
 assert source.stat().st_size==348*PIXELS*4
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 libpath=out/'frame-encoder.dll'
 subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-shared','-O3',str(ROOT/'tools/tests/intro_delta_encoder.c'),str(ROOT/'platform/video/IntroFrames.c'),'-o',str(libpath)],check=True,env=env)
 lib=ctypes.CDLL(str(libpath));lib.intro_delta.argtypes=[ctypes.c_void_p]*4;lib.intro_delta.restype=ctypes.c_int
 lib.H1IntroUnpack.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_void_p];lib.H1IntroUnpack.restype=ctypes.c_int
 lib.H1IntroReconstruct.argtypes=[ctypes.c_int,*[ctypes.c_void_p]*3];lib.H1IntroExpand.argtypes=[ctypes.c_void_p]*2
 before=ctypes.create_string_buffer(PIXELS*2);current=ctypes.create_string_buffer(PIXELS*2);delta=ctypes.create_string_buffer(PIXELS*2)
 restoredBefore=ctypes.create_string_buffer(PIXELS*2);restoredCurrent=ctypes.create_string_buffer(PIXELS*2);unpacked=ctypes.create_string_buffer(PIXELS*2)
 colours=ctypes.create_string_buffer(PIXELS*4);decoded=hashlib.sha256();expanded=hashlib.sha256();entries=[];maxPacket=0;shifts={}
 # Exhaustively check the final native-color expansion independently.
 values=ctypes.create_string_buffer(struct.pack('<65536H',*range(65536))+bytes((PIXELS-65536)*2));lib.H1IntroExpand(values,colours)
 colour_bytes=colours.raw
 for i in range(65536):
  r=(i>>11)&31;g=(i>>5)&63;b=i&31
  expected=0xff000000|(((r<<3)|(r>>2))<<16)|(((g<<2)|(g>>4))<<8)|((b<<3)|(b>>2))
  assert struct.unpack_from('<I',colour_bytes,i*4)[0]==expected
 with source.open('rb') as stream:
  for index in range(348):
   raw=ctypes.create_string_buffer(stream.read(PIXELS*4));shift=lib.intro_delta(raw,current,before,delta)
   packed=lz4.block.compress(delta.raw[:PIXELS*2],mode='high_compression',compression=12,store_size=False)
   buf=ctypes.create_string_buffer(packed);assert lib.H1IntroUnpack(buf,len(packed),unpacked)==1
   lib.H1IntroReconstruct(shift,unpacked,restoredBefore,restoredCurrent)
   assert restoredCurrent.raw[:PIXELS*2]==current.raw[:PIXELS*2],index
   decoded.update(restoredCurrent.raw[:PIXELS*2]);lib.H1IntroExpand(restoredCurrent,colours);expanded.update(colours.raw[:PIXELS*4])
   entries.append(struct.pack('<iI',shift,len(packed))+packed);maxPacket=max(maxPacket,len(packed));shifts[shift]=shifts.get(shift,0)+1
   before,current=current,before;restoredBefore,restoredCurrent=restoredCurrent,restoredBefore
 body=b''.join(entries);header=struct.pack('<16I',0x46493148,2,480,272,348,40,13890,maxPacket,len(body),zlib.crc32(body),0,0,0,0,0,0)
 output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(header+body)
 result={**proof,'runtime_path':output.name,'codec':'offline RGB565 horizontal motion XOR / bounded LZ4 blocks','bytes':output.stat().st_size,'sha256':sha(output),'max_compressed_frame_bytes':maxPacket,'shifts':shifts,'all_frames_roundtrip_rgb565':348,'rgb565_sha256':decoded.hexdigest(),'expanded_bgra_sha256':expanded.hexdigest(),'exhaustive_native_colours':65536,'encoder_sha256':sha(Path(__file__)),'exporter_sha256':sha(ROOT/'tools/tests/first_intro_export.cpp'),'predictor_source_sha256':sha(ROOT/'tools/tests/intro_delta_encoder.c'),'decoder_source_sha256':sha(ROOT/'platform/video/IntroFrames.c'),'lz4_version':'4.4.5','scope':'All original opening motion and timing; quantized once to native RGB565 offline, then lossless motion deltas; no runtime motion search, IDCT or YUV conversion'}
 (out/'frames-verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result),flush=True)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('export','encode','encode-h264','verify'));p.add_argument('--host',type=Path,default=ROOT/'build/host-first-level-fix');p.add_argument('--build-dir',type=Path,default=ROOT/'build/first-intro-bake');p.add_argument('--output',type=Path);p.add_argument('--cxx',default='C:/msys64/ucrt64/bin/g++.exe');a=p.parse_args()
 if a.output is None:a.output=(a.build_dir/'first-intro-reference.h1iv') if a.mode=='encode-h264' else ROOT/'private/resources-h1-wide/first-intro.h1if'
 if a.mode in ('export','verify'):export(a.host.resolve(),a.build_dir.resolve(),a.cxx,a.mode=='verify')
 elif a.mode=='encode-h264':encode_h264(a.build_dir.resolve(),a.output.resolve())
 else:encode(a.build_dir.resolve(),a.output.resolve())
if __name__=='__main__':main()
