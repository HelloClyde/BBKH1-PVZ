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
namespace {int fault=0,calls=0;bool committed=false;
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
extern "C" int __wrap_close(int fd){int r=__real_close(fd);if(fault==4){errno=EIO;return -1;}return r;}
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
    std::ofstream out(std::filesystem::path(argv[1])/"verification.json");
    out<<"{\"passed\":true,\"cases\":"<<cases<<",\"binary_bytes\":20017,\"partial_write\":true,\"zero_write\":true,\"write_error\":true,\"close_error\":true,\"rename_error\":true,\"rollback_failure_recovery\":true,\"old_bytes_preserved_on_failure\":true}\n";
}
