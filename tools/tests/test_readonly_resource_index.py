"""Exercise the production _open policy with a deterministic native-FS stub."""
from pathlib import Path
import argparse,json,subprocess,hashlib,os
ROOT=Path(__file__).resolve().parents[2]

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=False)
 source=(ROOT/'runtime/syscalls.c').read_text(encoding='utf8');start=source.index('static int trusted_definition_path(');end=source.index('\nint _close(',start)
 prefix=r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#define _open pvz_test_open
#define FILE_COUNT 64
#define NATIVE_PATH_MAX 256
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_ACCMODE 3
#define O_CREAT 256
#define O_TRUNC 512
#define O_APPEND 1024
#define O_EXCL 2048
#define O_DIRECTORY 4096
typedef struct {int dummy;} h1_file;
struct stat {unsigned st_mode;};
#define S_ISDIR(n) ((n)==1)
static h1_file file,*files[FILE_COUNT];static char*directory_paths[FILE_COUNT];static int descriptor_flags[FILE_COUNT];
static unsigned pvz_runtime_diagnostic[16],pvz_runtime_asset_index_diagnostic[8];
static int active=1,opens=0,queries=0,invalidations=0;
static int pvz_map_native_path(const char*p,char*out,size_t n,int ignored){(void)ignored;snprintf(out,n,"%s",p);return 0;}
static int stat_native(const char*p,struct stat*s){(void)p;s->st_mode=1;return 0;}
static int query_asset_index(const char*p,void*a,void*b){(void)a;(void)b;++queries;return !active?-1:strstr(p,"missing")?0:1;}
static void invalidate_asset_index(const char*p){(void)p;++invalidations;active=0;}
static h1_file*io_native_open(const char*p,const char*m){(void)m;++opens;return strstr(p,"missing")?0:&file;}
static int h1_fclose(h1_file*p){(void)p;return 0;}
static int native_failure(int e){errno=e;return -1;}
static void trace_path(const char*p){(void)p;}
static void trace_index_miss(const char*p){(void)p;}
static void audio_io_yield(void){}
'''
 suffix=r'''
static void reset(void){memset(files,0,sizeof files);memset(directory_paths,0,sizeof directory_paths);active=1;opens=queries=invalidations=0;errno=0;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 reset();CHECK(_open("reanim/zombie.h1d",O_RDONLY)>=3);CHECK(active&&invalidations==0&&queries==0&&opens==1);
 CHECK(_open("images/missing.h1i",O_RDONLY)==-1);CHECK(errno==ENOENT&&active&&opens==1&&queries==1);
 reset();CHECK(_open("images/background1.h1i",O_RDONLY)>=3);CHECK(active&&queries==1&&opens==1&&invalidations==0);
 reset();CHECK(_open("images/background1.h1i",O_WRONLY|O_TRUNC)>=3);CHECK(!active&&invalidations==1&&queries==0&&opens==1);
 reset();CHECK(_open("reanim/zombie.h1d",O_RDWR)>=3);CHECK(!active&&invalidations==1&&queries==0);
 reset();CHECK(_open("reanim/zombie.h1d",O_RDONLY|O_CREAT)>=3);CHECK(!active&&invalidations==1&&queries==0);
 reset();CHECK(_open("reanim/zombie.h1d",O_RDONLY|O_APPEND)>=3);CHECK(!active&&invalidations==1&&queries==0);
 reset();CHECK(_open("reanim",O_DIRECTORY)>=3);CHECK(active&&invalidations==0&&opens==0);
 puts("read-only definitions preserve authoritative index; writes invalidate; negative probes avoid native FS");return 0;
}
'''
 p=a.output/'probe.c';p.write_text(prefix+source[start:end]+suffix,encoding='utf8',newline='\n');exe=a.output/'probe.exe'
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 c=subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-std=gnu11','-O2',str(p),'-o',str(exe)],capture_output=True,text=True,env=env,timeout=30);(a.output/'compile.log').write_text(c.stdout+c.stderr);assert c.returncode==0,c.stderr
 r=subprocess.run([str(exe.resolve())],capture_output=True,text=True,env=env,timeout=30);(a.output/'run.log').write_text(r.stdout+r.stderr)
 result=dict(passed=r.returncode==0,source_sha256=hashlib.sha256((ROOT/'runtime/syscalls.c').read_bytes()).hexdigest(),native_filesystem_stub=True,cases=8,stdout=r.stdout,stderr=r.stderr)
 (a.output/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(r.returncode)
if __name__=='__main__':main()
