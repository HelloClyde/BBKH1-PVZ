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
extern "C" {volatile uint32_t pvzh1_save_file_diagnostic[8];}
extern "C" void pvzh1_cache_complete_draw_batch();
namespace {
bool Exists(const std::string& path,std::size_t* size=nullptr){
    struct stat info{};if(::stat(path.c_str(),&info))return false;
    if(size)*size=(std::size_t)info.st_size;return true;
}
}
void H1RecoverSaveFile(const std::string& path){
    const auto backup=path+".h1bak";
    if(Exists(backup)&&!Exists(path))::rename(backup.c_str(),path.c_str());
}
bool H1WriteSaveFile(const std::string& path,const void* data,std::size_t size){
    // Engine lifecycle writes run after the synchronous widget draw returned.
    // End its pixel pins so genuine allocation pressure may reclaim images;
    // this boundary itself neither allocates nor evicts any data.
    pvzh1_cache_complete_draw_batch();
    // Allocate path strings before opening anything for write. In particular
    // a later stream-buffer allocation cannot leave the original truncated.
    const auto temporary=path+".h1tmp",backup=path+".h1bak";
    H1RecoverSaveFile(path);
    auto& d=pvzh1_save_file_diagnostic;++d[0];d[1]=(uint32_t)size;d[2]=0;d[3]=1;d[4]=0;
    auto fail=[&](unsigned phase){d[3]=phase;d[4]=(uint32_t)errno;++d[6];return false;};
    int flags=O_WRONLY|O_CREAT|O_TRUNC;
#ifdef O_BINARY
    flags|=O_BINARY;
#endif
    int fd=::open(temporary.c_str(),flags,0666);
    if(fd<0)return fail(2);
    std::size_t done=0;bool ok=true;
    while(done<size){
        const auto n=::write(fd,(const char*)data+done,size-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0||(std::size_t)n>size-done){ok=false;break;}
        done+=(std::size_t)n;d[2]=(uint32_t)done;
    }
    const int writeError=errno,closed=::close(fd);d[7]=(uint32_t)closed;
    if(!ok||closed){if(!ok)errno=writeError;::unlink(temporary.c_str());return fail(!ok?3:4);}
    std::size_t actual=0;
    if(!Exists(temporary,&actual)||actual!=size){errno=EIO;::unlink(temporary.c_str());return fail(5);}
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
