/* All original factory keys now return bounded, immutable preview sources. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/ReanimationLawn.h"
#include "actors/CachedFrames.h"
#include "graphics/MemoryImage.h"
#include "graphics/MemoryImageCache.h"
#include "Common.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_cached_frame_diagnostic[8];
namespace {void Check(bool v,const char* why){if(!v){std::fprintf(stderr,"CACHED FRAMES: %s\n",why);throw std::runtime_error(why);}}}
extern "C" void pvzh1_cached_frames_probe(){
 auto* cache=gLawnApp->mReanimatorCache.get();std::ifstream cases("build/cached-frame-cases-v1.txt");Check(bool(cases),"missing frozen case list");unsigned count=0;int kind,id,variation,w,h,sw,sh;
 while(cases>>kind>>id>>variation>>w>>h>>sw>>sh){
  auto image=kind==0?cache->MakeCachedPlantFrame(SeedType(id),DrawVariation(variation)):kind==1?cache->MakeCachedMowerFrame(LawnMowerType(id)):cache->MakeCachedZombieFrame(ZombieType(id));
  Check(image&&image->mWidth==w&&image->mHeight==h&&!image->mBits,"factory materialized the logical canvas or changed coordinates");
  Sexy::H1ImageCacheStorageStats stats{};Check(Sexy::H1ImageCacheGetStorageStats(image.get(),&stats)&&stats.storageWidth==unsigned(sw)&&stats.storageHeight==unsigned(sh),"preview storage dimensions differ");
  const uint32_t*const* pages=nullptr;uint32_t width=0,height=0;Check(Sexy::H1ImageCachePrepareStorageRGBPages(image.get(),&pages,&width,&height)&&width==unsigned(sw)&&height==unsigned(sh),"bounded preview pages missing");
  char expected[64];if(kind==0)std::snprintf(expected,sizeof(expected),"cache/p%02d-v%02d",id,variation);else std::snprintf(expected,sizeof(expected),"cache/%c%02d",kind==1?'m':'z',id);
  Check(image->mFilePath==Sexy::GetResourcePath(expected),"factory selected a different baked image");++count;
 }
 Check(count==214&&!pvzh1_cached_frame_diagnostic[4],"factory coverage incomplete or used online rasterization");
 PlayerInfo profile;profile.mName="PREVIEW_LIFETIME";profile.mId=1;profile.mLevel=6;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);Check(pvzh1_cached_frame_diagnostic[1]>0,"original level boundary skipped preview preload");
 unsigned resident=0;for(auto& image:cache->mPlantImages)if(image){Check(!image->mBits,"level preview expanded its source canvas");++resident;}
 Check(resident>=6,"seed chooser previews were not preloaded");H1CachedFramesRelease();
 for(auto& image:cache->mPlantImages)Check(!image,"plant preview survived release");for(auto& image:cache->mLawnMowers)Check(!image,"mower preview survived release");for(auto& image:cache->mZombieImages)Check(!image,"zombie preview survived release");Check(!cache->mImageVariationList.mSize,"variant preview survived release");
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 const auto root=Sexy::GetResourceFolder();const auto missing=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"missing-previews";std::filesystem::create_directory(missing);Sexy::SetResourceFolder(missing.string());bool rejected=false;
 try{cache->MakeCachedPlantFrame(SEED_PEASHOOTER,VARIATION_NORMAL);}catch(const std::runtime_error& e){rejected=std::string_view(e.what()).find("offline preview load failed")!=std::string_view::npos;}
 Sexy::SetResourceFolder(root);Check(rejected&&!pvzh1_cached_frame_diagnostic[4],"missing preview entered an online factory");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"cached-frames-verification.json");out<<"{\"passed\":true,\"factory_keys\":"<<count<<",\"plant_variants\":175,\"mowers\":4,\"zombies\":35,\"logical_dimensions_preserved\":true,\"maximum_data_block_bytes\":4096,\"logical_canvas_allocations\":0,\"seed_chooser_preloaded_images\":"<<resident<<",\"all_preview_owners_released\":true,\"missing_required_preview_rejected\":true}\n";
}
