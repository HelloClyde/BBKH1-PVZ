"""Exercise the production crash writer without C++ allocation or stdio."""
import json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
    out=ROOT/'build/native-fault-writer-test';out.mkdir(exist_ok=True)
    source=(ROOT/'runtime/syscalls.c').read_text(encoding='utf8')
    start=source.index('void pvz_runtime_report_failure(int code){');end=source.index('\nstatic PvzAssetIndex',start)
    body=source[start:end]
    stub=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#define NATIVE_PATH_MAX 512
typedef struct {int unused;} h1_file;
volatile uint32_t pvz_runtime_diagnostic[16],pvz_runtime_termination_diagnostic[4],pvz_runtime_native_charge[2],pvz_runtime_native_charge_fault[4],pvz_runtime_heap_failure[20],pvzp_diagnostic[32];
char pvz_runtime_termination_message[256],pvz_runtime_last_path[512];
static unsigned mode,opens,writes,closes,maps;static unsigned char saved[1100];
static uint32_t h1_raw_tick_80hz(void){return 2468;}
static int pvz_native_path(const char*p,char*out,size_t n){assert(!strcmp(p,"/PVZSAVE/H1FAULT.BIN"));++maps;strcpy(pvz_runtime_last_path,"changed by path mapping");strcpy(out,"B:\\userdata\\H1FAULT.BIN");return mode==4?-1:0;}
static h1_file* io_native_open(const char*p,const char*m){assert(!strcmp(m,"wb"));++opens;return mode==2?NULL:(h1_file*)(uintptr_t)8;}
static unsigned h1_fwrite(const void*p,unsigned a,unsigned n,h1_file*f){assert(a==1&&n==sizeof(saved));++writes;memcpy(saved,p,n);return mode==3?n-1:n;}
static int h1_fclose(h1_file*f){++closes;return 0;}
'''
    checks=r'''
int main(int argc,char**argv){
 mode=argc>1?(unsigned)atoi(argv[1]):0;
 for(unsigned i=0;i<16;++i)pvz_runtime_diagnostic[i]=1000+i;
 for(unsigned i=0;i<4;++i){pvz_runtime_termination_diagnostic[i]=2000+i;pvz_runtime_native_charge_fault[i]=3000+i;}
 for(unsigned i=0;i<2;++i)pvz_runtime_native_charge[i]=4000+i;
 for(unsigned i=0;i<20;++i)pvz_runtime_heap_failure[i]=5000+i;
 for(unsigned i=0;i<32;++i)pvzp_diagnostic[i]=6000+i;
 strcpy(pvz_runtime_termination_message,"Failure before unloading");strcpy(pvz_runtime_last_path,"/PVZ/missing.asset");
 pvz_runtime_report_failure(mode?107:0);
 if(!mode){assert(!opens&&!writes&&!closes&&!maps);return 0;}
 pvz_runtime_report_failure(108);assert(maps==1);
 if(mode==4){assert(!opens&&!writes&&!closes);return 0;}
 assert(opens==1);if(mode==2){assert(!writes&&!closes);return 0;}
 assert(writes==1&&closes==1);uint32_t w[83];memcpy(w,saved,sizeof(w));
 assert(w[0]==0x58463148&&w[1]==1&&w[2]==1100&&w[3]==107&&w[4]==2468);
 for(unsigned i=0;i<16;++i)assert(w[5+i]==1000+i);
 for(unsigned i=0;i<4;++i){assert(w[21+i]==2000+i);assert(w[27+i]==3000+i);}
 for(unsigned i=0;i<2;++i)assert(w[25+i]==4000+i);
 for(unsigned i=0;i<20;++i)assert(w[31+i]==5000+i);
 for(unsigned i=0;i<32;++i)assert(w[51+i]==6000+i);
 assert(!strcmp((char*)saved+332,"Failure before unloading"));assert(!strcmp((char*)saved+588,"/PVZ/missing.asset"));
 if(mode==1){FILE*f=fopen(argv[2],"wb");assert(f);assert(fwrite(saved,1,sizeof(saved),f)==sizeof(saved));fclose(f);}return 0;
}
'''
    c=out/'probe.c';c.write_text(stub+body+checks,encoding='utf8');exe=out/'probe.exe'
    env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
    subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-std=c11','-O2',str(c),'-o',str(exe)],check=True,env=env)
    record=out/'fault.bin'
    for mode in range(5):subprocess.run([str(exe),str(mode),str(record)],check=True,env=env)
    sys.path.insert(0,str(ROOT/'tools'));from read_native_fault import decode
    decoded=decode(record.read_bytes());assert decoded['status']==107 and decoded['game'][7]==6007 and decoded['message']=='Failure before unloading'
    print(json.dumps(dict(passed=True,cases=5,normal_exit_writes=0,record_bytes=1100,native_heap_allocation=False)))
if __name__=='__main__':main()
