"""Exercise the production atomic commit body against real host files.

Only the SDK filesystem boundary is substituted. RTOS scheduling/lifetime are
verified separately on H1, never inferred from this host check.
"""
from pathlib import Path
import hashlib,json,os,subprocess

ROOT=Path(__file__).resolve().parents[2]
source=ROOT/'runtime/async_save.h'
text=source.read_text(encoding='utf8')
body=text[text.index('static int save_identical('):text.index('static unsigned save_profile_delete(')]
out=ROOT/'build/async-save-commit';out.mkdir(exist_ok=True)
program=r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <assert.h>
typedef FILE h1_file;
typedef struct SaveJob {volatile unsigned state;unsigned size,result,ticks;
 char path[512],temporary[512],backup[512];unsigned char data[];} SaveJob;
static unsigned pvzh1_async_save_diagnostic[16];
static int fault=0,renames=0;
static int save_stat(const char*p,unsigned*s){struct stat st;int r=stat(p,&st);if(!r&&s)*s=(unsigned)st.st_size;return r;}
static int save_rename(const char*a,const char*b){++renames;if(fault==4&&renames==2)return -1;if(!save_stat(b,0))return -1;return rename(a,b);}
static h1_file*h1_fopen(const char*p,const char*m){if(fault==1&&m[0]=='w')return 0;return fopen(p,m);}
static unsigned h1_fread(void*p,unsigned z,unsigned n,h1_file*f){return (unsigned)fread(p,z,n,f);}
static unsigned h1_fwrite(const void*p,unsigned z,unsigned n,h1_file*f){return (unsigned)fwrite(p,z,fault==2?n/2:n,f);}
static int h1_fclose(h1_file*f){int writing=ftell(f)==36;int r=fclose(f);return fault==3&&writing?-1:r;}
static int h1_remove(const char*p){return remove(p);}
'''+body+r'''
static void original(SaveJob*j){fault=0;remove(j->path);remove(j->temporary);remove(j->backup);FILE*f=fopen(j->path,"wb");fwrite("old",1,3,f);fclose(f);renames=0;}
static void check_old(SaveJob*j){char s[8]={0};FILE*f=fopen(j->path,"rb");assert(f);assert(fread(s,1,8,f)==3);assert(!memcmp(s,"old",3));fclose(f);}
int main(void){
 SaveJob*j=calloc(1,sizeof(*j)+36);strcpy(j->path,"test.dat");strcpy(j->temporary,"test.dat.h1tmp");strcpy(j->backup,"test.dat.h1bak");
 j->size=36;memcpy(j->data,"0123456789abcdefghijklmnopqrstuvwxyz",36);
 for(int f=1;f<=4;++f){original(j);fault=f;assert(save_commit(j)!=0);check_old(j);}
 original(j);assert(!save_commit(j));unsigned n=0;assert(!save_stat(j->path,&n)&&n==36);assert(save_stat(j->backup,0));
 renames=0;assert(!save_commit(j));assert(!renames&&pvzh1_async_save_diagnostic[12]==1);
 original(j);assert(!rename(j->path,j->backup));fault=1;assert(save_commit(j)!=0);check_old(j);
 fault=0;assert(!save_commit(j));assert(save_stat(j->backup,0));
 remove(j->path);remove(j->temporary);remove(j->backup);free(j);
 puts("PASS: short write, open/close/install failure preserve original; interrupted backup recovered; replacement and identical skip");
}
'''
(out/'test.c').write_text(program,encoding='utf8')
env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-O2','-Wall','-Wextra',str(out/'test.c'),'-o',str(out/'test.exe')],check=True,env=env)
result=subprocess.run([str((out/'test.exe').resolve())],cwd=out,env=env,capture_output=True,text=True)
(out/'result.json').write_text(json.dumps(dict(passed=result.returncode==0,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),stdout=result.stdout,stderr=result.stderr,scope='Host real files; mocked SDK boundary. Native task separately tested.'),indent=2)+'\n')
print(result.stdout+result.stderr);assert not result.returncode
