"""Check all offline names and the actual native open/stat integration bodies."""
from pathlib import Path
import ctypes,hashlib,json,os,struct,subprocess
ROOT=Path(__file__).resolve().parents[2]
def body(text,signature):
 start=text.index(signature);i=text.index('{',start)+1;depth=1
 while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]
def main():
 out=ROOT/'build/asset-index-test';out.mkdir(parents=True,exist_ok=True)
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','');cc='C:/msys64/ucrt64/bin/gcc.exe'
 def compile(args):
  p=subprocess.run([cc,'-std=gnu11','-O2','-I',str(ROOT/'runtime'),*map(str,args)],env=env,capture_output=True,text=True)
  assert p.returncode==0,p.stdout+p.stderr
 compile(['-shared',ROOT/'runtime/asset_index.c','-o',out/'index.dll'])
 lib=ctypes.CDLL(str(out/'index.dll'))
 class View(ctypes.Structure):_fields_=[('data',ctypes.c_void_p),('count',ctypes.c_uint32),('strings',ctypes.c_uint32),('bytes',ctypes.c_uint32)]
 lib.pvz_asset_index_parse.argtypes=[ctypes.POINTER(View),ctypes.c_void_p,ctypes.c_size_t]
 lib.pvz_asset_index_find.argtypes=[ctypes.POINTER(View),ctypes.c_char_p,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_int)]
 data=(ROOT/'private/resources-h1-wide/resource-files.h1fi').read_bytes();storage=ctypes.create_string_buffer(data);view=View()
 assert lib.pvz_asset_index_parse(ctypes.byref(view),storage,len(data))
 checks=0
 for i in range(view.count):
  offset,size,flags=struct.unpack_from('<3I',data,32+i*12);at=32+view.count*12+offset;name=data[at:data.index(b'\0',at)]
  n=ctypes.c_uint32();d=ctypes.c_int();assert lib.pvz_asset_index_find(ctypes.byref(view),name,ctypes.byref(n),ctypes.byref(d)) and (n.value,d.value)==(size,flags)
  assert not lib.pvz_asset_index_find(ctypes.byref(view),name+b'.absent',ctypes.byref(n),ctypes.byref(d));checks+=2
 def rejected(blob):
  b=ctypes.create_string_buffer(blob);v=View(123,7,8,9);assert not lib.pvz_asset_index_parse(ctypes.byref(v),b,len(blob));assert (v.data,v.count,v.strings,v.bytes)==(123,7,8,9)
 for length in (0,1,4,16,31,32,len(data)-1):rejected(data[:length]);checks+=1
 for at in (0,4,8,12,16,20,28):
  b=bytearray(data);b[at]^=0x80;rejected(bytes(b));checks+=1
 # Index bodies were checked offline; the device does not hash them again.
 b=bytearray(data);b[24]^=0x80;trusted=ctypes.create_string_buffer(bytes(b));trusted_view=View()
 assert lib.pvz_asset_index_parse(ctypes.byref(trusted_view),trusted,len(b));checks+=1
 text=(ROOT/'runtime/syscalls.c').read_text()
 prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include "asset_index.h"
#define NATIVE_PATH_MAX 512
#define FILE_COUNT 64
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_ACCMODE 3
#define O_CREAT 0x200
#define O_EXCL 0x800
#define O_TRUNC 0x400
#define O_APPEND 8
#define O_DIRECTORY 0x200000
#define S_IFREG 0x8000
#define S_IFDIR 0x4000
#define S_ISDIR(x) (((x)&0xf000)==S_IFDIR)
struct stat {unsigned st_mode,st_size,st_nlink,st_blksize,st_blocks;};
typedef struct {const unsigned char* data;size_t size,at;} h1_file;
static h1_file indexFile,regular,*files[FILE_COUNT];
static char* directory_paths[FILE_COUNT];static int descriptor_flags[FILE_COUNT];
static uint32_t pvz_runtime_diagnostic[32];static const char *resource_prefix="B:\\PVZ";
static int asset_bucket_state,openCalls,statCalls,indexMissing,inside;
static int ascii_prefix(const char* a,const char* b){while(*b){unsigned x=*a++,y=*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;}return 1;}
static h1_file* io_native_open(const char* path,const char* mode){assert(!inside);openCalls++;
 if(strstr(path,"resource-files.h1fi")){if(indexMissing)return 0;indexFile.at=0;return &indexFile;}
 if(strstr(path,"briannetod12.txt.h1fc")||strstr(path,"userdata")||strcmp(mode,"rb"))return &regular;return 0;}
static unsigned io_native_read(void* p,unsigned n,h1_file* f){assert(!inside);inside=1;if(n>f->size-f->at)n=f->size-f->at;memcpy(p,f->data+f->at,n);f->at+=n;inside=0;return n;}
static int h1_fclose(h1_file* f){(void)f;return 0;}
static void audio_io_yield(void){assert(!inside);}
static int pvz_map_native_path(const char* a,char* b,size_t n,int kind){(void)kind;assert(strlen(a)<n);strcpy(b,a);return 0;}
static int pvz_native_path(const char* a,char* b,size_t n){return pvz_map_native_path(a,b,n,1);}
static void trace_path(const char* p){(void)p;}
static int native_failure(int error){errno=error;return -1;}
static int stat_native(const char* p,struct stat* st){(void)p;statCalls++;memset(st,0,sizeof *st);st->st_size=17;return 0;}
'''
 segment=text[text.index('static PvzAssetIndex asset_index;'):text.index('uint32_t pvz_runtime_ticks(void)')]
 code=prefix+segment+'\n'+body(text,'int _open(')+'\n'+body(text,'int _stat(')
 code+=r'''
static void reset(void){free(asset_index_data);asset_index_data=0;memset(&asset_index,0,sizeof asset_index);asset_index_state=asset_index_disabled=0;memset(files,0,sizeof files);openCalls=statCalls=0;indexMissing=0;}
int main(int argc,char** argv){
 assert(argc==2);FILE* f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);unsigned char* bytes=malloc(n);assert(fread(bytes,1,n,f)==n);fclose(f);indexFile.data=bytes;indexFile.size=n;
 struct stat st;reset();
 assert(_open("B:\\PVZ\\IMAGES\\never_exists.h1a",O_RDONLY)==-1&&errno==ENOENT&&openCalls==1&&asset_index_state==2);
 assert(_open("b:\\pvz\\images\\never_exists.h1a",O_RDONLY)==-1&&openCalls==1);
 assert(_open("B:\\PVZ\\REANIM_SELECTORSCREEN_GHOST.h1a",O_RDONLY)==-1&&openCalls==1);
 assert(_open("B:\\PVZ\\REANIM_SELECTORSCREEN_GHOST.h1i",O_RDONLY)==-1&&openCalls==1);
 assert(_open("B:\\PVZ\\data\\briannetod12.txt.h1fc",O_RDONLY)==3&&openCalls==2);
 assert(_stat("B:\\PVZ\\data\\briannetod12.txt.h1fc",&st)==0&&st.st_size>0&&statCalls==0);
 assert(_stat("B:\\PVZ\\sounds",&st)==0&&S_ISDIR(st.st_mode)&&statCalls==0);
 assert(_stat("B:\\PVZ\\images\\never_exists.h1a",&st)==-1&&errno==ENOENT&&statCalls==0);
 assert(_stat("B:\\PVZ\\userdata\\user1.dat",&st)==0&&st.st_size==17&&statCalls==1);
 assert(_open("B:\\PVZ\\userdata\\game1_0.v4",O_WRONLY|O_CREAT|O_TRUNC)>=3&&!asset_index_disabled);
 int before=openCalls;assert(_open("A:\\PVZ\\images\\never_exists.h1a",O_RDONLY)==-1&&openCalls==before+1);
 asset_bucket_state=1;before=openCalls;assert(_open("B:\\PVZ\\H1DATA\\2f\\images\\never_exists.h1a",O_RDONLY)==-1&&openCalls==before);asset_bucket_state=0;
 assert(_open("B:\\PVZ\\images\\added.h1i",O_WRONLY|O_CREAT)>=3&&asset_index_disabled&&pvz_runtime_asset_index_diagnostic[0]==3);
 before=openCalls;assert(_open("B:\\PVZ\\images\\never_exists.h1a",O_RDONLY)==-1&&openCalls==before+1);
 reset();resource_prefix="A:\\PVZ";assert(_open("A:\\PVZ\\images\\never_exists.h1a",O_RDONLY)==-1&&openCalls==1);resource_prefix="B:\\PVZ";
 reset();indexMissing=1;assert(_open("B:\\PVZ\\images\\never_exists.h1a",O_RDONLY)==-1&&openCalls==2&&asset_index_state==1);
 reset();bytes[24]^=1;assert(_open("B:\\PVZ\\images\\never_exists.h1a",O_RDONLY)==-1&&openCalls==1&&asset_index_state==2);bytes[24]^=1;
 reset();free(bytes);return 0;
}
'''
 (out/'integration.c').write_text(code,encoding='utf8');compile([out/'integration.c',ROOT/'runtime/asset_index.c','-o',out/'integration.exe'])
 subprocess.run([str(out/'integration.exe'),str(ROOT/'private/resources-h1-wide/resource-files.h1fi')],env=env,check=True)
 proof=dict(passed=True,lookup_checks=checks,production_open_stat_bodies=True,save_paths_bypass=True,drive_a_and_b=True,bucket_layout=True,mutation_invalidates=True,missing_index_fallback=True,trusted_checksum_ignored=True,syscalls_sha256=hashlib.sha256((ROOT/'runtime/syscalls.c').read_bytes()).hexdigest(),parser_sha256=hashlib.sha256((ROOT/'runtime/asset_index.c').read_bytes()).hexdigest())
 (out/'verification.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
if __name__=='__main__':main()
