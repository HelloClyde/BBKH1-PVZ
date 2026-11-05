"""Exercise the real C loader, including interleaved packed image cursors."""
from pathlib import Path
import argparse,ctypes as C,hashlib,json,os,shutil,subprocess,struct
R=Path(__file__).resolve().parents[2]
class Info(C.Structure):_fields_=[(n,C.c_uint32) for n in ('width','height','channels','flags','pixel_bytes','storage_width','storage_height')]
class Image(C.Structure):_fields_=[('file',C.c_void_p),('info',Info)]+[(n,C.c_uint32) for n in ('alpha_color','rgb_source_bytes','rgb_next_byte','rgb_stream_valid','payload_offset','packed','file_position','file_end')]
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 dll=out/'loader.dll';source=R/'platform/resources/h1_resources.c'
 subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-std=c11','-O2','-shared','-static-libgcc','-Wl,--export-all-symbols','-o',str(dll),str(source)],check=True,env=env)
 lib=C.CDLL(str(dll));lib.h1_resources_set_root.argtypes=[C.c_char_p];lib.h1_image_open.argtypes=[C.POINTER(Image),C.c_char_p,C.c_int,C.c_uint32];lib.h1_image_close.argtypes=[C.POINTER(Image)]
 lib.h1_image_load_storage.argtypes=[C.POINTER(Image),C.POINTER(C.c_uint32),C.c_size_t]
 lib.h1_image_read_storage_pixels.argtypes=[C.POINTER(Image),C.c_uint32,C.c_uint32,C.POINTER(C.c_uint32)]
 root=R/'private/resources-h1-wide';names=json.loads((R/'provenance/common-image-paths.json').read_text())['files']
 packed=out/'packed';loose=out/'loose';packed.mkdir();loose.mkdir()
 for directory in (packed,loose):
  shutil.copy2(root/'image-metadata.h1x',directory/'image-metadata.h1x')
  for name in names:
   target=directory/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/name,target)
 shutil.copy2(root/'images-common.h1pk',packed/'images-common.h1pk')
 def select(directory):lib.h1_resources_set_root((directory.as_posix()+'/').encode())
 def open_image(name):
  image=Image();assert lib.h1_image_open(C.byref(image),name[:-4].encode(),name.endswith('.h1a'),0xffffff),name
  return image
 expected={};pixels=0
 for directory in (loose,packed):
  select(directory)
  for name in names:
   image=open_image(name);assert bool(image.packed)==(directory==packed)
   count=image.info.storage_width*image.info.storage_height;data=(C.c_uint32*count)()
   assert lib.h1_image_load_storage(C.byref(image),data,count),name
   if directory==loose:expected[name]=bytes(data)
   else:assert bytes(data)==expected[name],name;pixels+=count
   lib.h1_image_close(C.byref(image))
 select(packed);chosen=[next(n for n in names if n.endswith('.h1a')),next(n for n in names if n.startswith('reanim/')), 'images/background1unsodded.h1i']
 images=[open_image(n) for n in chosen];calls=0
 for round in range(4):
  for name,image in zip(chosen,images):
   count=image.info.storage_width*image.info.storage_height;offset=min(round*233,count-1);n=min(1024,count-offset);data=(C.c_uint32*n)()
   assert lib.h1_image_read_storage_pixels(C.byref(image),offset,n,data)
   assert bytes(data)==expected[name][offset*4:(offset+n)*4];calls+=1
 for image in images:lib.h1_image_close(C.byref(image))
 original=(packed/'images-common.h1pk').read_bytes();fallbacks=0
 for blob in (original[:20],original[:-1],original[:36]+bytes([original[36]^1])+original[37:]):
  lib.h1_resources_shutdown();(packed/'images-common.h1pk').write_bytes(blob);select(packed)
  image=open_image(chosen[-1]);assert not image.packed;count=image.info.storage_width*image.info.storage_height;data=(C.c_uint32*count)()
  assert lib.h1_image_load_storage(C.byref(image),data,count) and bytes(data)==expected[chosen[-1]];lib.h1_image_close(C.byref(image));fallbacks+=1
 lib.h1_resources_shutdown();diag=(C.c_uint32*4).in_dll(lib,'pvzh1_image_pack_diagnostic');assert list(diag)[:2]==[0,0]
 report=dict(passed=True,images_compared=len(names),physical_argb_pixels=pixels,interleaved_cursor_reads=calls,corrupt_pack_fallbacks=fallbacks,shutdown_resident_index_bytes=0,loader_source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),pack_sha256=hashlib.sha256(original).hexdigest(),scope='Actual production C loader on host; not native filesystem timing')
 (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
