/* Delete existing files of one player; no per-mode failed filesystem calls. */
#include "ProfileFiles.h"
#include "Common.h"
#include "SexyAppBase.h"
#include "../runtime/profile_files.h"
#include <filesystem>
#include <vector>
#include <cstdio>

void H1DeletePlayerFiles(int id,unsigned modes){
    const auto root=Sexy::GetAppDataPath("userdata");
#if defined(H1_PVZ_PORTABLE) && !defined(PVZ_HOST_RUNNER)
    extern int H1DeletePlayerFilesNative(const char*,int,unsigned);
    if(H1DeletePlayerFilesNative(root.c_str(),id,modes))return;
#endif
    char user[48],games[48];std::snprintf(user,sizeof(user),"user%d.dat",id);std::snprintf(games,sizeof(games),"game%d_",id);
    std::error_code error;std::vector<std::string> paths;
    for(auto it=std::filesystem::directory_iterator(Sexy::PathFromU8(root),error);!error&&it!=std::filesystem::directory_iterator();it.increment(error)){
        const auto name=Sexy::PathToU8(it->path().filename());
        if(pvz_profile_file_matches(name.c_str(),user,games,modes)&&!it->is_directory(error))paths.push_back(Sexy::PathToU8(it->path()));
    }
    // Finish enumeration before removing directory entries.
    for(const auto& path:paths)Sexy::gSexyAppBase->EraseFile(path);
}
#if defined(H1_PVZ_PORTABLE) && !defined(PVZ_HOST_RUNNER)
extern "C" int pvzh1_delete_profile_enqueue(const char*,int,unsigned);
int H1DeletePlayerFilesNative(const char* root,int id,unsigned modes){return pvzh1_delete_profile_enqueue(root,id,modes);}
#endif
