"""Actual C frame decoder: bounds, overlap, color precision and complete movie."""
from pathlib import Path
import array,ctypes,hashlib,json,os,shutil,struct,subprocess,tempfile,unittest,zlib
ROOT=Path(__file__).resolve().parents[2]
PIXELS=480*272

def extension(n):
 result=bytearray()
 while n>=255:result.append(255);n-=255
 result.append(n);return bytes(result)

def colour(c):
 r=c>>11;g=(c>>5)&63;b=c&31
 return 0xff000000|(((r<<3)|(r>>2))<<16)|(((g<<2)|(g>>4))<<8)|((b<<3)|(b>>2))

class IntroFrames(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  compiler=shutil.which('gcc') or 'C:/msys64/ucrt64/bin/gcc.exe'
  cls.directory=tempfile.TemporaryDirectory(prefix='pvz-intro-test-')
  library=Path(cls.directory.name)/('frames.dll' if os.name=='nt' else 'frames.so')
  env=os.environ.copy();env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
  subprocess.run([compiler,'-shared','-O2','-fPIC',str(ROOT/'platform/video/IntroFrames.c'),'-o',str(library)],check=True,env=env)
  cls.lib=ctypes.CDLL(str(library));cls.lib.H1IntroUnpack.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
  cls.lib.H1IntroUnpack.restype=ctypes.c_int
  cls.lib.H1IntroReconstruct.argtypes=[ctypes.c_int,*[ctypes.c_void_p]*3]
  cls.lib.H1IntroExpand.argtypes=[ctypes.c_void_p]*2
  cls.lib.H1IntroPresent565.argtypes=[ctypes.c_void_p]*2
  cls.lib.H1IntroReconstructExpand.argtypes=[ctypes.c_int,*[ctypes.c_void_p]*4]
 @classmethod
 def tearDownClass(cls):
  if os.name=='nt':
   import _ctypes
   _ctypes.FreeLibrary(cls.lib._handle);cls.lib=None
  cls.directory.cleanup()

 def unpack(self,data):
  guard=32;output=ctypes.create_string_buffer(b'\xa5'*(PIXELS*2+guard*2));source=ctypes.create_string_buffer(data)
  result=self.lib.H1IntroUnpack(source,len(data),ctypes.byref(output,guard))
  self.assertEqual(output.raw[:guard],b'\xa5'*guard);self.assertEqual(output.raw[guard+PIXELS*2:guard+PIXELS*2+guard],b'\xa5'*guard)
  return result,output.raw[guard:guard+PIXELS*2]

 def test_complete_asset(self):
  proof=json.loads((ROOT/'provenance/offline-first-intro.json').read_text(encoding='utf-8'))
  data=(ROOT/'private/resources-h1-wide/first-intro.h1if').read_bytes();header=struct.unpack_from('<16I',data)
  self.assertEqual(header[:7],(0x46493148,2,480,272,348,40,13890));self.assertEqual(zlib.crc32(data[64:]),header[9])
  previous=ctypes.create_string_buffer(PIXELS*2);current=ctypes.create_string_buffer(PIXELS*2);expanded=ctypes.create_string_buffer(PIXELS*4)
  decoded=hashlib.sha256();at=64
  for frame in range(348):
   shift,n=struct.unpack_from('<iI',data,at);at+=8;self.assertTrue(-3<=shift<=3);okay,delta=self.unpack(data[at:at+n]);at+=n;self.assertEqual(okay,1,frame)
   self.lib.H1IntroReconstructExpand(shift,ctypes.create_string_buffer(delta),previous,current,expanded)
   decoded.update(expanded.raw[:PIXELS*4]);previous,current=current,previous
  self.assertEqual(at,len(data));self.assertEqual(decoded.hexdigest(),proof['expanded_bgra_sha256'])

 def test_every_native_colour(self):
  source=ctypes.create_string_buffer(struct.pack('<65536H',*range(65536))+bytes((PIXELS-65536)*2));output=ctypes.create_string_buffer(PIXELS*4)
  self.lib.H1IntroExpand(source,output);self.assertEqual(output.raw[:65536*4],array.array('I',(colour(c) for c in range(65536))).tobytes())
  self.lib.H1IntroPresent565(source,output);self.assertEqual(output.raw[:65536*4],array.array('I',(colour(c)&0xffffff for c in range(65536))).tobytes())

 def test_overlapping_matches(self):
  for distance in (1,2,3,4,5,7,16,31,960):
   literal=bytes(i%256 for i in range(distance));match=PIXELS*2-distance
   block=bytes([(min(distance,15)<<4)|15])+(extension(distance-15) if distance>=15 else b'')+literal+struct.pack('<H',distance)+extension(match-19)
   okay,data=self.unpack(block);self.assertEqual(okay,1);self.assertEqual(data,(literal*((PIXELS*2+distance-1)//distance))[:PIXELS*2])

 def test_bad_blocks_keep_guards(self):
  almost=b'\x1f\0\x01\0'+extension(PIXELS*2-21)
  cases=[b'',b'\xf0',b'\x10',b'\x10x\0\0',b'\x10x\x02\0',b'\xf0'+extension(PIXELS*2)+b'x',almost+b'\0\x01\0']
  for data in cases:self.assertEqual(self.unpack(data)[0],0)

 def test_motion_edges_and_fusion(self):
  before=array.array('H',((i*7919+17)&65535 for i in range(PIXELS)));delta=array.array('H',((i*127+123)&65535 for i in range(PIXELS)))
  source=ctypes.create_string_buffer(before.tobytes());changes=ctypes.create_string_buffer(delta.tobytes())
  for shift in range(-3,4):
   current=ctypes.create_string_buffer(PIXELS*2);out=ctypes.create_string_buffer(PIXELS*4)
   self.lib.H1IntroReconstructExpand(shift,changes,source,current,out)
   expected=array.array('H',(delta[y*480+x]^(before[y*480+x-shift] if 0<=x-shift<480 else 0) for y in range(272) for x in range(480)))
   self.assertEqual(current.raw[:PIXELS*2],expected.tobytes());self.assertEqual(out.raw[:PIXELS*4],array.array('I',(colour(c) for c in expected)).tobytes())
   self.lib.H1IntroReconstruct(shift,changes,source,current);self.assertEqual(current.raw[:PIXELS*2],expected.tobytes())

if __name__=='__main__':unittest.main()
