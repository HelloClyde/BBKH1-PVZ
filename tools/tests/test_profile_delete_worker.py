"""Real files plus production matching, FIFO selection and deletion code.

Only SDK find/stat/open/remove are replaced; RTOS scheduling is tested on H1.
"""
from pathlib import Path
import hashlib,json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[2];text=(ROOT/'runtime/async_save.h').read_text(encoding='utf8')
out=ROOT/'build/profile-delete-worker';out.mkdir(exist_ok=True)
job=re.search(r'typedef struct SaveJob.*?} SaveJob;',text,re.S).group()
commit=text[text.index('static int save_identical('):text.index('static unsigned save_profile_delete(')]
worker=text[text.index('static unsigned save_profile_delete('):text.index('static void save_worker(')]
affects=text[text.index('static int save_affects_path('):text.index('void pvzh1_save_wait(')]
program=r'''
#include <filesystem>
#include <vector>
#include <string>
#include <fstream>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "profile_files.h"
#define SAVE_SLOTS 4
#define PROFILE_NAME_BYTES 48u
'''+job+r'''
static SaveJob *volatile save_jobs[SAVE_SLOTS];
static unsigned pvzh1_async_save_diagnostic[16],pvzh1_delete_diagnostic[8];
using h1_file=FILE;namespace fs=std::filesystem;
static std::vector<std::string> listing;static unsigned cursor;static bool opened=false;
static std::string native(const char*p){std::string s=p;for(auto&c:s)if(c=='\\')c='/';return s;}
static int first(const char*p,unsigned mask,void*r){assert(mask==6);listing.clear();auto s=native(p);s.resize(s.size()-4);for(const auto&e:fs::directory_iterator(s))if(!e.is_directory())listing.push_back(e.path().filename().string());cursor=0;if(listing.empty())return -1;opened=true;((unsigned*)r)[0]=1;std::strcpy((char*)r+0x12,listing[0].c_str());return 0;}
static int next(void*r){if(++cursor==listing.size())return -1;std::strcpy((char*)r+0x12,listing[cursor].c_str());return 0;}
static int close_find(void*r){opened=false;((unsigned*)r)[0]=0;return 0;}
static uintptr_t h1_runtime_table(unsigned){return 0;}
static uintptr_t h1_runtime_entry(uintptr_t,unsigned o){return o==0x3c?(uintptr_t)first:o==0x40?(uintptr_t)next:(uintptr_t)close_find;}
#define H1_RUNTIME_FS_TABLE_SLOT 0
static int h1_remove(const char*p){assert(!opened);return std::remove(native(p).c_str());}
static int save_stat(const char*p,unsigned*n){std::error_code e;auto z=fs::file_size(native(p),e);if(e)return -1;if(n)*n=(unsigned)z;return 0;}
static int save_rename(const char*a,const char*b){std::error_code e;if(fs::exists(native(b)))return -1;fs::rename(native(a),native(b),e);return e?-1:0;}
static h1_file*h1_fopen(const char*p,const char*m){return std::fopen(native(p).c_str(),m);}
static unsigned h1_fread(void*p,unsigned a,unsigned n,h1_file*f){return std::fread(p,a,n,f);}
static unsigned h1_fwrite(const void*p,unsigned a,unsigned n,h1_file*f){return std::fwrite(p,a,n,f);}
static int h1_fclose(h1_file*f){return std::fclose(f);}
'''+commit+worker+affects+r'''
static SaveJob* write_job(const char*path,const char*value,unsigned seq){unsigned n=std::strlen(value);auto*j=(SaveJob*)std::calloc(1,sizeof(SaveJob)+n);std::strcpy(j->path,path);std::strcpy(j->temporary,(std::string(path)+".h1tmp").c_str());std::strcpy(j->backup,(std::string(path)+".h1bak").c_str());std::memcpy(j->data,value,n);j->size=n;j->sequence=seq;j->state=1;return j;}
int main(){
 assert(pvz_profile_file_matches("USER1.DAT","user1.dat","game1_",4));
 assert(pvz_profile_file_matches("game1_0.V4.h1BAK","user1.dat","game1_",4));
 for(const char*n:{"user10.dat","user1.dat.extra","game10_0.v4","game1_4.v4","game1_00.v4","game1_-1.v4","game1_9999999999999.v4","game1_0.v4x","users.dat"})assert(!pvz_profile_file_matches(n,"user1.dat","game1_",4));
 fs::create_directory("files");fs::current_path("files");
 const std::vector<std::string> owned={"user1.dat","game1_0.v4","game1_1.dat","GAME1_2.DAT.h1bak","game1_3.v4.h1tmp","game1_0.v4.h1bak"};
 const std::vector<std::string> untouched={"user10.dat","game10_0.v4","game1_4.v4","game1_0.v4x","user1.dat.extra"};
 for(auto&n:owned){std::ofstream f(n);f<<"owned";}for(auto&n:untouched){std::ofstream f(n);f<<"untouched";}
 auto*d=(SaveJob*)std::calloc(1,sizeof(SaveJob)+27*PROFILE_NAME_BYTES);d->operation=1;d->modes=4;d->size=27*PROFILE_NAME_BYTES;d->sequence=12;d->state=1;std::strcpy(d->path,".");std::strcpy(d->temporary,"user1.dat");std::strcpy(d->backup,"game1_");
 assert(save_affects_path(d,".\\user1.dat"));assert(save_affects_path(d,".\\GAME1_3.v4.h1tmp"));
 assert(!save_affects_path(d,".\\game10_0.v4"));assert(!save_affects_path(d,".\\users.dat"));assert(!save_affects_path(d,".\\nested\\user1.dat"));
 save_jobs[0]=d;save_jobs[1]=write_job(".\\users.dat","new roster",13);save_jobs[2]=write_job(".\\users.dat","old roster",10);save_jobs[3]=write_job(".\\user1.dat","last pending details",11);
 unsigned expected=10;while(auto*j=save_next_job()){assert(j->sequence==expected++);assert(!(j->operation?save_profile_delete(j):save_commit(j)));j->state=3;}assert(expected==14);
 d->state=0;d->sequence=20;save_jobs[1]->state=1;save_jobs[1]->sequence=21;
 assert(!save_next_job());d->state=1;assert(save_next_job()==d);d->state=3;assert(save_next_job()==save_jobs[1]);save_jobs[1]->state=3;
 for(auto&n:owned)assert(!fs::exists(n));for(auto&n:untouched){std::ifstream f(n);std::string s;std::getline(f,s);assert(s=="untouched");}
 {std::ifstream f("users.dat");std::string s;std::getline(f,s);assert(s=="new roster");}
 assert(pvzh1_delete_diagnostic[4]==owned.size()&&!pvzh1_delete_diagnostic[5]);
 for(unsigned i=0;i<SAVE_SLOTS;++i){std::free(save_jobs[i]);save_jobs[i]=nullptr;}
 std::puts("PASS: pending save -> exact-profile delete -> latest roster; no resurrection; uppercase/legacy/temp files removed; other IDs preserved");
}
'''
(out/'test.cpp').write_text(program,encoding='utf8');env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
c=subprocess.run(['C:/msys64/ucrt64/bin/g++.exe','-std=c++20','-O2','-I',str(ROOT/'runtime'),str(out/'test.cpp'),'-o',str(out/'test.exe')],env=env,capture_output=True,text=True,encoding='utf8',errors='replace');(out/'compile.log').write_text(c.stdout+c.stderr,encoding='utf8');assert c.returncode==0,c.stderr
r=subprocess.run([str(out/'test.exe')],cwd=out,env=env,capture_output=True,text=True);proof=dict(passed=r.returncode==0,stdout=r.stdout,stderr=r.stderr,source_sha256=hashlib.sha256(text.encode()).hexdigest(),scope='Host real files and production SDK-worker functions; no RTOS performance claim')
(out/'result.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(json.dumps(proof));assert not r.returncode
