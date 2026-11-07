#include "SaveFile.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <cerrno>
#include <vector>
extern "C" void pvzh1_cache_complete_draw_batch(){}
extern "C" int __real_write(int,const void*,unsigned);
extern "C" int __real_close(int);
extern "C" int __real_rename(const char*,const char*);
extern "C" int __real_read(int,void*,unsigned);
namespace {int fault=0,calls=0,reads=0,closes=0;bool committed=false;
void Check(bool v){if(!v)throw std::runtime_error("Save transaction lost original bytes or failed recovery");}
std::string Read(const std::string& n){std::ifstream f(n,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Put(const std::string& n,const std::string& s){std::ofstream f(n,std::ios::binary);f.write(s.data(),s.size());}
}
extern "C" int __wrap_write(int fd,const void* p,unsigned n){
    ++calls;
    if((fault==1||fault==2)&&calls==1)return __real_write(fd,p,n/2);
    if(fault==2){errno=EIO;return -1;}
    if(fault==3)return 0;
    return __real_write(fd,p,n);
}
extern "C" int __wrap_close(int fd){++closes;int r=__real_close(fd);if(fault==4||(fault==8&&closes==1)){errno=EIO;return -1;}return r;}
extern "C" int __wrap_read(int fd,void* p,unsigned n){++reads;if(reads==1&&(fault==9||fault==10)){errno=fault==10?EINTR:EIO;return -1;}return __real_read(fd,p,fault==11&&n>1?n/2:n);}
extern "C" int __wrap_rename(const char* a,const char* b){
    const std::string from(a);
    if((fault==5||fault==7)&&from.ends_with(".h1tmp")){committed=true;errno=EIO;return -1;}
    if(fault==7&&committed){errno=EIO;return -1;}
    if(fault==6&&!from.ends_with(".h1tmp")){errno=EIO;return -1;}
    return __real_rename(a,b);
}
int main(int argc,char** argv){
    Check(argc==2);const std::string path=(std::filesystem::path(argv[1])/"user1.dat").string();
    const std::string old="verified original profile\n";
    std::string bytes;for(unsigned i=0;i<20017;++i)bytes.push_back(char(i*37));
    unsigned cases=0;
    for(int f=0;f<=7;++f){
        fault=0;for(const auto& s:{path,path+".h1tmp",path+".h1bak"})std::filesystem::remove(s);
        Put(path,old);fault=f;calls=0;committed=false;
        const bool ok=H1WriteSaveFile(path,bytes.data(),bytes.size());fault=0;
        Check(calls>0);Check(ok==(f<=1));
        if(f==7){Check(!std::filesystem::exists(path));H1RecoverSaveFile(path);}
        Check(Read(path)==(ok?bytes:old));Check(!std::filesystem::exists(path+".h1tmp"));++cases;
    }
    // A power interruption between the two non-overwriting renames leaves
    // only the previously closed backup, which the original reader recovers.
    Put(path+".h1bak",old);std::filesystem::remove(path);H1RecoverSaveFile(path);Check(Read(path)==old);++cases;
    std::filesystem::remove(path);Check(H1WriteSaveFile(path,bytes.data(),bytes.size()));Check(Read(path)==bytes);++cases;
    // Repeated unchanged saves must perform no writes or rename transaction;
    // failed comparison reads/closes must fall back to checked replacement.
    for(int f:{0,8,9,10,11}){
        fault=0;Put(path,bytes);fault=f;calls=reads=closes=0;
        Check(H1WriteSaveFile(path,bytes.data(),bytes.size()));fault=0;
        Check((calls==0)==(f==0||f==10||f==11));Check(Read(path)==bytes);++cases;
    }
    // Equal size and long common prefix are not evidence of equal content.
    auto changed=bytes;changed.back()^=1;calls=0;Check(H1WriteSaveFile(path,changed.data(),changed.size()));Check(calls>0&&Read(path)==changed);++cases;
    Put(path,"");calls=0;Check(H1WriteSaveFile(path,nullptr,0));Check(calls==0&&Read(path).empty());++cases;
    std::ofstream out(std::filesystem::path(argv[1])/"verification.json");
    out<<"{\"passed\":true,\"cases\":"<<cases<<",\"binary_bytes\":20017,\"partial_write\":true,\"zero_write\":true,\"write_error\":true,\"close_error\":true,\"rename_error\":true,\"rollback_failure_recovery\":true,\"old_bytes_preserved_on_failure\":true,\"identical_save_no_writes\":true,\"whole_file_compared\":true,\"comparison_read_and_close_errors_rewrite\":true,\"interrupted_and_short_reads\":true,\"empty_file\":true}\n";
}
