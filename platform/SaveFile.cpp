/* Write engine-produced save bytes without deferred C++ stream buffers.
 * H1 rename cannot overwrite, so keep the old file until the closed staging
 * file has its expected size. A failed replacement retains a recovery copy. */
#include "SaveFile.h"
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#ifdef PVZ_NATIVE_PROFILE
#include "profiler.h"
#endif
extern "C" {volatile uint32_t pvzh1_save_file_diagnostic[8];}
// Attempts, identical files, bytes compared, read failures, physical writes.
extern "C" {volatile uint32_t pvzh1_save_compare_diagnostic[5];}
#ifdef PVZ_NATIVE_PROFILE
extern "C" {volatile uint32_t pvzh1_save_compare_bypass;}
#endif
extern "C" void pvzh1_cache_complete_draw_batch();
#if defined(H1_PVZ_PORTABLE) && !defined(PVZ_HOST_RUNNER)
extern "C" int pvzh1_save_enqueue(const char*,const void*,size_t);
extern "C" void pvzh1_save_wait(const char*);
#endif
namespace {
bool Exists(const std::string& path,std::size_t* size=nullptr){
    struct stat info{};if(::stat(path.c_str(),&info))return false;
    if(size)*size=(std::size_t)info.st_size;return true;
}
bool SameBytes(const std::string& path,const void* data,std::size_t size){
#ifdef PVZ_NATIVE_PROFILE
    PVZProfileScope profile(PVZ_PROFILE_SAVE_COMPARE);
#endif
    ++pvzh1_save_compare_diagnostic[0];
    std::size_t actual=0;if(!Exists(path,&actual)||actual!=size)return false;
    int flags=O_RDONLY;
#ifdef O_BINARY
    flags|=O_BINARY;
#endif
    const int fd=::open(path.c_str(),flags);if(fd<0)return false;
    unsigned char buffer[1024];std::size_t done=0;bool equal=true;
    while(done<size){
        const std::size_t count=std::min(sizeof(buffer),size-done);
        const auto n=::read(fd,buffer,count);
        if(n<0&&errno==EINTR)continue;
        if(n<=0||(std::size_t)n>count){++pvzh1_save_compare_diagnostic[3];equal=false;break;}
        pvzh1_save_compare_diagnostic[2]+=(uint32_t)n;
        if(std::memcmp(buffer,(const unsigned char*)data+done,(std::size_t)n)){equal=false;break;}
        done+=(std::size_t)n;
    }
    // Size/stat alone is not enough: confirm all bytes and EOF, and do not
    // accept a failed read/close as a successful comparison.
    unsigned char extra;ssize_t end=0;
    if(equal){do{end=::read(fd,&extra,1);}while(end<0&&errno==EINTR);if(end!=0){equal=false;if(end<0)++pvzh1_save_compare_diagnostic[3];}}
    const int closed=::close(fd);if(closed){++pvzh1_save_compare_diagnostic[3];equal=false;}
    return equal;
}
}
void H1RecoverSaveFile(const std::string& path){
#if defined(H1_PVZ_PORTABLE) && !defined(PVZ_HOST_RUNNER)
    pvzh1_save_wait(path.c_str());
#endif
    const auto backup=path+".h1bak";
    if(Exists(backup)&&!Exists(path))::rename(backup.c_str(),path.c_str());
}
bool H1WriteSaveFile(const std::string& path,const void* data,std::size_t size){
#if defined(H1_PVZ_PORTABLE) && !defined(PVZ_HOST_RUNNER)
    if(pvzh1_save_enqueue(path.c_str(),data,size))return true;
#endif
    // Engine lifecycle writes run after the synchronous widget draw returned.
    // End its pixel pins so genuine allocation pressure may reclaim images;
    // this boundary itself neither allocates nor evicts any data.
    pvzh1_cache_complete_draw_batch();
    // Allocate path strings before opening anything for write. In particular
    // a later stream-buffer allocation cannot leave the original truncated.
    const auto temporary=path+".h1tmp",backup=path+".h1bak";
    H1RecoverSaveFile(path);
    auto& d=pvzh1_save_file_diagnostic;++d[0];d[1]=(uint32_t)size;d[2]=0;d[3]=1;d[4]=0;
    bool compare=true;
#ifdef PVZ_NATIVE_PROFILE
    compare=pvzh1_save_compare_bypass==0;
#endif
    if(compare&&SameBytes(path,data,size)){
        ++pvzh1_save_compare_diagnostic[1];d[2]=(uint32_t)size;d[3]=9;d[7]=0;++d[5];return true;
    }
    ++pvzh1_save_compare_diagnostic[4];
    auto fail=[&](unsigned phase){d[3]=phase;d[4]=(uint32_t)errno;++d[6];return false;};
    int flags=O_WRONLY|O_CREAT|O_TRUNC;
#ifdef O_BINARY
    flags|=O_BINARY;
#endif
    int fd=::open(temporary.c_str(),flags,0666);
    if(fd<0)return fail(2);
    std::size_t done=0;bool ok=true;
    {
#ifdef PVZ_NATIVE_PROFILE
    PVZProfileScope profile(PVZ_PROFILE_SAVE_WRITE);
#endif
    while(done<size){
        const auto n=::write(fd,(const char*)data+done,size-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0||(std::size_t)n>size-done){ok=false;break;}
        done+=(std::size_t)n;d[2]=(uint32_t)done;
    }
    }
    const int writeError=errno;int closed;
    {
#ifdef PVZ_NATIVE_PROFILE
    PVZProfileScope profile(PVZ_PROFILE_SAVE_CLOSE);
#endif
    closed=::close(fd);d[7]=(uint32_t)closed;
    }
    if(!ok||closed){if(!ok)errno=writeError;::unlink(temporary.c_str());return fail(!ok?3:4);}
    std::size_t actual=0;
    if(!Exists(temporary,&actual)||actual!=size){errno=EIO;::unlink(temporary.c_str());return fail(5);}
#ifdef PVZ_NATIVE_PROFILE
    PVZProfileScope commitProfile(PVZ_PROFILE_SAVE_COMMIT);
#endif
    if(Exists(backup)&&::unlink(backup.c_str())){::unlink(temporary.c_str());return fail(6);}
    const bool original=Exists(path);
    if(original&&::rename(path.c_str(),backup.c_str())){::unlink(temporary.c_str());return fail(7);}
    if(::rename(temporary.c_str(),path.c_str())){
        const int error=errno;if(original)::rename(backup.c_str(),path.c_str());
        ::unlink(temporary.c_str());errno=error;return fail(8);
    }
    if(original)::unlink(backup.c_str());
    d[3]=9;++d[5];return true;
}
