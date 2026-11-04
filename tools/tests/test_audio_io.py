"""Exercise actual native _read/_write code against bounded SDK byte streams."""
from pathlib import Path
import hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2]
def function(text,name):
 start=text.index('ssize_t '+name+'(');at=text.index('{',start);depth=1;i=at+1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[start:i]
def main():
 out=ROOT/'build/audio-io-test';out.mkdir(parents=True,exist_ok=True);text=(ROOT/'runtime/syscalls.c').read_text()
 prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#define FILE_COUNT 4
#define NATIVE_PATH_MAX 256
typedef unsigned h1_size_t;
typedef struct {unsigned char data[60000];size_t pos,length,limit;} h1_file;
static h1_file fixture,*files[FILE_COUNT],*log_file;
static uint32_t pvz_runtime_diagnostic[32];static unsigned calls,yields,max_request,inside,bad;
static int native_failure(int n){errno=n;return -1;}
static int pvz_native_path(const char* a,char* b,size_t n){return -1;}
static h1_file* io_native_open(const char* a,const char* b){return 0;}
static void audio_io_yield(void){assert(!inside);++yields;}
static unsigned io_native_read(void* b,unsigned n,h1_file* f){
 inside=1;++calls;if(n>max_request)max_request=n;if(bad){inside=0;return n+1;}
 if(n>f->length-f->pos)n=f->length-f->pos;memcpy(b,f->data+f->pos,n);f->pos+=n;inside=0;return n;
}
static unsigned h1_fwrite(const void* b,unsigned size,unsigned n,h1_file* f){
 assert(size==1);inside=1;++calls;if(n>max_request)max_request=n;if(bad){inside=0;return n+1;}
 if(n>f->limit-f->pos)n=f->limit-f->pos;memcpy(f->data+f->pos,b,n);f->pos+=n;inside=0;return n;
}
static void reset(void){memset(&fixture,0,sizeof fixture);files[0]=&fixture;calls=yields=max_request=bad=0;}
'''
 main=r'''
int main(void){unsigned char data[50000],result[50000];for(unsigned i=0;i<sizeof data;i++)data[i]=(i*47+3)&255;
 reset();fixture.length=sizeof data;memcpy(fixture.data,data,sizeof data);
 assert(_read(3,result,sizeof result)==sizeof result&&!memcmp(data,result,sizeof result));assert(calls==4&&yields==4&&max_request==16384);
 reset();fixture.length=30000;memcpy(fixture.data,data,30000);assert(_read(3,result,50000)==30000&&!memcmp(data,result,30000));assert(calls==2&&yields==2);
 reset();fixture.limit=50000;assert(_write(3,data,50000)==50000&&!memcmp(data,fixture.data,50000));assert(calls==13&&yields==13&&max_request==4096);
 reset();fixture.limit=9000;assert(_write(3,data,50000)==9000&&!memcmp(data,fixture.data,9000));assert(calls==3&&yields==3);
 reset();bad=1;assert(_read(3,result,100)==-1&&errno==EIO);assert(!yields);assert(_write(3,data,100)==-1&&errno==EIO);
 reset();assert(_read(0,result,10)==0);assert(_read(-1,result,1)==-1&&errno==EBADF);assert(_write(8,data,1)==-1&&errno==EBADF);
 assert(_read(3,result,0)==0&&_write(3,data,0)==0&&!calls&&!yields);
 puts("PASS exact bytes / EOF / partial write / bounds / invalid descriptors / service outside SDK calls");return 0;
}
'''
 source=out/'probe.c';source.write_text(prefix+function(text,'_read')+'\n'+function(text,'_write')+main,encoding='utf8');exe=out/'probe.exe'
 env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
 subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-std=gnu11','-O2',str(source),'-o',str(exe)],env=env,check=True)
 result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,check=True);print(result.stdout.strip())
 (out/'verification.json').write_text(json.dumps(dict(passed=True,scope='Actual native syscall bodies with deterministic SDK streams; no device file IO',production_sha256=hashlib.sha256((ROOT/'runtime/syscalls.c').read_bytes()).hexdigest(),result=result.stdout.strip()),indent=2)+'\n')
if __name__=='__main__':main()
