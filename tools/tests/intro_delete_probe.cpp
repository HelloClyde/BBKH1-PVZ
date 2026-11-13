/* Bound movie allocations to fragmented-heap-sized blocks and exercise real
 * profile deletion through the game. No production game result is injected. */
#include "ui_flow_engine_probe.cpp"
#include "ProfileFiles.h"
#include <cerrno>
#include <cstring>
extern "C" volatile unsigned pvzh1_intro_memory_diagnostic[8];
namespace {bool insideMovie=false;unsigned largeCalls=0,denied=0,pages=0;}
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t n){
    if(insideMovie){
        if(n==65536&&++largeCalls>1){++denied;errno=ENOMEM;return nullptr;}
        if(n==16384)++pages;
    }
    return __real_malloc(n);
}
extern "C" bool __real__Z19H1IntroMoviePrepareP5Board(Board*);
extern "C" bool __wrap__Z19H1IntroMoviePrepareP5Board(Board* b){
    const bool previous=insideMovie;insideMovie=b&&b->mApp->mGameMode==GAMEMODE_INTRO;largeCalls=0;
    try{const bool result=__real__Z19H1IntroMoviePrepareP5Board(b);insideMovie=previous;return result;}
    catch(...){insideMovie=previous;throw;}
}
extern "C" void pvzh1_intro_delete_probe(){
    using namespace Sexy;const bool reference=getenv("PVZ_HOST_EXPECT_PAGE_OOM")!=nullptr;
    const auto root=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));
    try{pvzh1_ui_flow_probe();}
    catch(const std::exception& e){
        if(!reference||!denied||std::strstr(e.what(),"read-ahead page")==nullptr)throw;
        std::ofstream out(root/"intro-delete-verification.json");out<<"{\"passed\":true,\"reference_64k_page_allocation_failed_as_expected\":true,\"message\":\"read-ahead page\"}\n";return;
    }
    if(reference||denied||pages<576||pvzh1_intro_memory_diagnostic[0]!=16384)throw std::runtime_error("Movie page bound was not exercised");
    if(pvzh1_intro_memory_diagnostic[3]>pvzh1_intro_memory_diagnostic[2])throw std::runtime_error("Idle image retirement increased residency");
    auto* old=gLawnApp->mPlayerInfo;
    auto* doomed=gLawnApp->mProfileMgr->AddProfile("DELETE_TEST");
    const auto id=doomed->mId;
    const auto data=GetAppDataPath("userdata");std::filesystem::create_directories(PathFromU8(data));
    auto file=[&](const std::string& name){return PathFromU8(data+"/"+name);};
    const auto user="user"+std::to_string(id)+".dat",game="game"+std::to_string(id)+"_";
    const auto other="user"+std::to_string(id)+"0.dat";
    for(const auto& name:{user,game+"0.v4",game+"1.dat",game+"2.v4.h1tmp",game+"3.dat.h1bak",other}){std::ofstream f(file(name));f<<"fixture";}
    gLawnApp->mProfileMgr->DeleteProfile("DELETE_TEST");gLawnApp->mProfileMgr->Save();
    for(const auto& name:{user,game+"0.v4",game+"1.dat",game+"2.v4.h1tmp",game+"3.dat.h1bak"})if(std::filesystem::exists(file(name)))throw std::runtime_error("Profile file survived deletion");
    if(!std::filesystem::exists(file(other))||gLawnApp->mProfileMgr->GetProfile("DELETE_TEST"))throw std::runtime_error("Deletion crossed profile boundary");
    gLawnApp->mPlayerInfo=old;
    std::ofstream out(root/"intro-delete-verification.json");
    out<<"{\"passed\":true,\"opening_frames\":348,\"movie_page_bytes\":16384,\"movie_page_allocations\":"<<pages<<",\"forbidden_later_64k_allocations\":"<<denied<<",\"idle_image_bytes_before\":"<<pvzh1_intro_memory_diagnostic[2]<<",\"idle_image_bytes_after\":"<<pvzh1_intro_memory_diagnostic[3]<<",\"profile_and_legacy_temp_files_removed\":true,\"other_profile_preserved\":true,\"scope\":\"Host original engine; native timing and scheduling tested separately\"}\n";
}
