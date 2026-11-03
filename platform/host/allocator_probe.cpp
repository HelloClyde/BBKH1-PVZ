/* Optional host fixture: real global pools, renderer and portable v4 writer. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/SaveGame.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/PvzpList.h"
#include "graphics/MemoryImage.h"
#include "graphics/Graphics.h"
#include "h1_host.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>

extern "C" void pvzh1_global_allocator_trim_empty(void*);
namespace {
void Check(bool value,const char *text){if(!value)throw std::runtime_error(text);}
uint64_t Hash(const void *data,size_t size){uint64_t h=1469598103934665603ull;auto p=(const unsigned char*)data;while(size--){h^=*p++;h*=1099511628211ull;}return h;}
std::vector<unsigned char> Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
std::vector<uint32_t> Render(Reanimation *animation){
    Sexy::MemoryImage image(gLawnApp);image.Create(800,600);Sexy::Graphics g(&image);
    g.SetColor(Sexy::Color(19,31,43));g.FillRect(0,0,800,600);animation->Draw(&g);
    const uint32_t *bits=image.GetBits();return {bits,bits+800*600};
}
}
extern "C" void pvzh1_host_probe_allocators(){
    static bool ran=false;const char *setting=getenv("PVZ_HOST_ALLOCATOR_PROBE");
    if(ran||!setting||setting[0]!='1'||!gLawnApp||!gLawnApp->mBoard)return;ran=true;
    Reanimation *real=nullptr;
    for(Plant *plant:gLawnApp->mBoard->mPlants)if(!plant->mDead){real=gLawnApp->ReanimationTryToGet(plant->mBodyReanimID);if(real)break;}
    Check(real&&real->mTrackInstances,"no real planted animation for allocator fixture");
    auto *holder=gLawnApp->mEffectSystem->mReanimationHolder.get();
    Reanimation *dead=holder->AllocReanimation(0,0,0,ReanimationType::REANIM_WALLNUT);dead->ReanimationDie();
    auto *realTracks=real->mTrackInstances,*deadTracks=dead->mTrackInstances;
    const size_t realSize=(size_t)real->mDefinition->mTracks.count*sizeof(ReanimatorTrackInstance);
    const size_t deadSize=(size_t)dead->mDefinition->mTracks.count*sizeof(ReanimatorTrackInstance);
    PvzpAllocator *realPool=FindGlobalAllocator((int)realSize),*deadPool=FindGlobalAllocator((int)deadSize);
    Check(realPool->mTotalItems>0&&deadPool->mTotalItems>0,"live/dead borrowers are not counted");
    PvzpAllocator *empty=FindGlobalAllocator(1536);const int grow=empty->mGrowCount,itemSize=empty->mItemSize;
    void *temporary=empty->Calloc(1536);for(size_t i=0;i<1536;++i)Check(((unsigned char*)temporary)[i]==0,"Calloc was not zero");empty->Free(temporary,1536);
    const int poolCount=gNumGlobalAllocators;auto before=Render(real);
    pvzh1_global_allocator_trim_empty(nullptr);
    Check(real->mTrackInstances==realTracks&&dead->mTrackInstances==deadTracks,"trim moved live or dead track storage");
    Check(realPool->IsPointerFromAllocator(realTracks)&&deadPool->IsPointerFromAllocator(deadTracks),"live/dead ownership lost");
    Check(!empty->mBlockList&&!empty->mFreeList&&!empty->mTotalItems&&empty->mItemSize==itemSize&&empty->mGrowCount==grow,"empty pool identity/configuration changed");
    Check(FindGlobalAllocator(1536)==empty&&gNumGlobalAllocators==poolCount,"global allocator identity/count changed");
    temporary=empty->Calloc(1536);Check(empty->IsPointerFromAllocator(temporary)&&empty->mTotalItems==1,"empty pool did not regrow");
    for(size_t i=0;i<1536;++i)Check(((unsigned char*)temporary)[i]==0,"regrown Calloc was not zero");empty->Free(temporary,1536);
    auto after=Render(real);Check(before==after,"real plant renderer pixels changed after trim");
    holder->mReanimations.DataArrayFree(dead);
    const char *directory=getenv("PVZ_HOST_OUTPUT");std::filesystem::path output=directory?directory:".";
    auto pathA=output/"allocator-before.v4",pathB=output/"allocator-after.v4";
    Check(LawnSaveGame(gLawnApp->mBoard,pathA.string()),"original v4 baseline save failed");
    pvzh1_global_allocator_trim_empty(nullptr);
    Check(LawnSaveGame(gLawnApp->mBoard,pathB.string()),"original v4 after-trim save failed");
    auto a=Read(pathA),b=Read(pathB);Check(!a.empty()&&a==b,"portable v4 bytes changed after empty trim");
    std::ofstream report(output/"allocator-cache-verification.json");
    report<<"{\"scope\":\"Host fixture using real planted Reanimation, original pools/renderer and v4 writer\",\"real_plant_screen_hash\":"<<Hash(before.data(),before.size()*4)
        <<",\"after_trim_screen_hash\":"<<Hash(after.data(),after.size()*4)<<",\"v4_bytes\":"<<a.size()<<",\"v4_hash\":"<<Hash(a.data(),a.size())
        <<",\"active_and_dead_pointers_preserved\":true,\"empty_pool_identity_preserved\":true,\"calloc_regrow_exact\":true,\"v4_byte_identical\":true,\"all_passed\":true}\n";
    pvzp_log_line("HOST ALLOCATOR PROBE: live/dead ownership, empty regrow, original plant pixels and portable v4 bytes identical");
}
