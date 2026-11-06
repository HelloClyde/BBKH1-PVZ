"""Exercise the production write bridge: save batching and exact short writes."""
from pathlib import Path
import hashlib,json,subprocess,os
R=Path(__file__).resolve().parents[2];W=R/'build/native-write-test';W.mkdir(exist_ok=True)
s=(R/'runtime/syscalls.c').read_text();body=s[s.index('ssize_t _write('):s.index('off_t _lseek(')]
code=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
typedef long long ssize_t;
typedef void h1_file;
#define FILE_COUNT 64
#define NATIVE_PATH_MAX 512
static h1_file *files[FILE_COUNT],*log_file;
static volatile uint32_t pvz_runtime_diagnostic[16];
static volatile uint32_t pvz_runtime_write_diagnostic[10];
static uint32_t h1_raw_tick_80hz(void){return 1;}
static unsigned calls,counts[4],used,behavior;
static unsigned char copied[120000];
static int pvz_native_path(const char*a,char*b,size_t c){(void)a;(void)b;(void)c;return -1;}
static h1_file *io_native_open(const char*a,const char*b){(void)a;(void)b;return 0;}
static int native_failure(int e){errno=e;return -1;}
static void audio_io_yield(void){}
static unsigned h1_fwrite(const void*p,unsigned size,unsigned count,h1_file*f){
 assert(size==1&&f==files[2]&&calls<4);counts[calls++]=count;
 if(behavior==2)return count+1;
 unsigned n=behavior==1?count/2:count;memcpy(copied+used,p,n);used+=n;return n;
}
'''+body+r'''
int main(void){
 unsigned char data[110000];for(unsigned i=0;i<sizeof(data);++i)data[i]=(unsigned char)(i*37+(i>>8));files[2]=(void*)1;
 assert(_write(5,data,41909)==41909&&calls==1&&used==41909&&!memcmp(data,copied,used));
 calls=used=0;assert(_write(5,data,sizeof(data))==sizeof(data)&&calls==3&&used==sizeof(data)&&!memcmp(data,copied,used));
 calls=used=0;behavior=1;assert(_write(5,data,1000)==500&&calls==1&&used==500&&!memcmp(data,copied,500));
 calls=used=0;behavior=2;assert(_write(5,data,1000)==-1&&errno==EIO&&calls==1);
 calls=used=0;behavior=0;assert(_write(4,data,1000)==-1&&errno==EBADF&&!calls);
 assert(_write(5,data,0)==0&&!calls);
}
'''
p=W/'probe.c';p.write_text(code);exe=W/'probe.exe'
env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
result=subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-std=c11','-O2',str(p),'-o',str(exe)],env=env,capture_output=True,text=True)
assert not result.returncode,result.stdout+result.stderr
subprocess.run([str(exe)],env=env,check=True)
proof=dict(passed=True,production_source_sha256=hashlib.sha256((R/'runtime/syscalls.c').read_bytes()).hexdigest(),save_41909_bytes_native_calls=1,large_write_byte_exact=True,short_write_preserved=True,invalid_native_result_rejected=True,invalid_descriptor_and_empty_write=True)
(W/'verification.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof))
