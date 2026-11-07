/* Immutable, display-density previews from the original build-time factories. */
#include "CachedFrames.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Challenge.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/LawnMower.h"
#include "Lawn/GridItem.h"
#include "Lawn/System/ReanimationLawn.h"
#include "PvzpLib/PvzpCommon.h"
#include "graphics/MemoryImage.h"
#include "graphics/MemoryImageCache.h"
#include "Common.h"
#include <cstdio>
#include <stdexcept>
extern "C" {volatile uint32_t pvzh1_cached_frame_diagnostic[8];}
#ifdef PVZ_HOST_RUNNER
namespace {bool cachedFramesEnabled=true;}
bool H1CachedFramesTestEnabled(){return cachedFramesEnabled;}
void H1CachedFramesTestEnable(bool value){cachedFramesEnabled=value;}
#endif
std::unique_ptr<Sexy::MemoryImage> H1CachedFrameLoad(int kind,int id,int variation){
 char name[64];
 if(kind==0)std::snprintf(name,sizeof(name),"cache/p%02d-v%02d",id,variation);
 else std::snprintf(name,sizeof(name),"cache/%c%02d",kind==1?'m':'z',id);
 auto image=std::make_unique<Sexy::MemoryImage>(gLawnApp);const auto path=Sexy::GetResourcePath(name);
 const uint32_t*const* pages=nullptr;uint32_t width=0,height=0;
 if(!Sexy::H1ImageCacheDescribe(image.get(),path,false,0)){
  ++pvzh1_cached_frame_diagnostic[3];throw std::runtime_error(std::string("H1 offline preview load failed: ")+name);
 }
 PvzpSandImageIfNeeded(image.get());
 if(!Sexy::H1ImageCachePrepareStorageRGBPages(image.get(),&pages,&width,&height)){
  ++pvzh1_cached_frame_diagnostic[3];throw std::runtime_error(std::string("H1 offline preview load failed: ")+name);
 }
 ++pvzh1_cached_frame_diagnostic[0];pvzh1_cached_frame_diagnostic[6]=4096;return image;
}
namespace {
void Reserve(Sexy::MemoryImage* image){
 if(!Sexy::H1ImageCacheReserveUI(image))throw std::runtime_error("H1 offline preview reservation failed");
 Sexy::H1ImageCacheStorageStats stats{};if(Sexy::H1ImageCacheGetStorageStats(image,&stats))pvzh1_cached_frame_diagnostic[7]+=stats.bytes;
}
void WarmVariant(ReanimatorCache* cache,int seed,DrawVariation variation){
 for(auto* node=cache->mImageVariationList.mHead;node;node=node->mNext)if(node->mValue.mSeedType==SeedType(seed)&&node->mValue.mDrawVariation==variation){Reserve(node->mValue.mImage);return;}
 ReanimCacheImageVariation entry;entry.mSeedType=SeedType(seed);entry.mDrawVariation=variation;entry.mImage=cache->MakeCachedPlantFrame(SeedType(seed),variation).release();cache->mImageVariationList.AddHead(entry);Reserve(entry.mImage);
}
}
void H1CachedFramesPrepare(const bool* plants,const bool* filtered,bool packetsChosen){
#ifdef PVZ_HOST_RUNNER
 if(!cachedFramesEnabled)return;
#endif
 if(!gLawnApp||!gLawnApp->mBoard||!gLawnApp->mReanimatorCache)return;
 ++pvzh1_cached_frame_diagnostic[1];pvzh1_cached_frame_diagnostic[7]=0;
 auto* cache=gLawnApp->mReanimatorCache.get();auto* board=gLawnApp->mBoard;bool keep[NUM_SEED_TYPES]{},zombies[NUM_CACHED_ZOMBIE_TYPES]{};
 cache->ReanimatorCacheDispose();
 for(int i=0;i<int(NUM_SEED_TYPES);++i)keep[i]=plants[i]||(!packetsChosen&&board->ChooseSeedsOnCurrentLevel()&&gLawnApp->HasSeedType(SeedType(i)));
 for(auto* pot:board->mGridItems)if(!pot->mDead&&pot->mGridItemType==GRIDITEM_SCARY_POT){if(pot->mScaryPotType==SCARYPOT_SEED&&int(pot->mSeedType)>=0&&int(pot->mSeedType)<NUM_SEED_TYPES)keep[int(pot->mSeedType)]=true;if(pot->mScaryPotType==SCARYPOT_ZOMBIE&&int(pot->mZombieType)>=0&&int(pot->mZombieType)<NUM_CACHED_ZOMBIE_TYPES)zombies[int(pot->mZombieType)]=true;}
 if(board->mSeedBank)for(int i=0;i<board->mSeedBank->mNumPackets&&i<SEEDBANK_MAX;++i){const auto seed=board->mSeedBank->mSeedPackets[i].mPacketType;if(Challenge::IsZombieSeedType(seed)){const int type=int(Challenge::IZombieSeedTypeToZombieType(seed));if(type>=0&&type<NUM_CACHED_ZOMBIE_TYPES)zombies[type]=true;}}
 for(int i=0;i<int(NUM_SEED_TYPES);++i){
  if(!keep[i]){if(cache->mPlantImages[i]){cache->mPlantImages[i].reset();++pvzh1_cached_frame_diagnostic[2];}continue;}
  if(!cache->mPlantImages[i])cache->mPlantImages[i]=cache->MakeCachedPlantFrame(SeedType(i),VARIATION_NORMAL);Reserve(cache->mPlantImages[i].get());
  if(filtered[i])WarmVariant(cache,i,VARIATION_IMITATER);
 }
 bool mowers[NUM_MOWER_TYPES]{};for(auto* mower:board->mLawnMowers)if(!mower->mDead)mowers[int(mower->mMowerType)]=true;
 for(int i=0;i<int(NUM_MOWER_TYPES);++i){if(!mowers[i]){cache->mLawnMowers[i].reset();continue;}if(!cache->mLawnMowers[i])cache->mLawnMowers[i]=cache->MakeCachedMowerFrame(LawnMowerType(i));Reserve(cache->mLawnMowers[i].get());}
 for(int i=0;i<int(NUM_CACHED_ZOMBIE_TYPES);++i){if(!zombies[i]){cache->mZombieImages[i].reset();continue;}if(!cache->mZombieImages[i])cache->mZombieImages[i]=cache->MakeCachedZombieFrame(ZombieType(i));Reserve(cache->mZombieImages[i].get());}
}
void H1CachedFramesRelease(){
 if(!gLawnApp||!gLawnApp->mReanimatorCache)return;auto* cache=gLawnApp->mReanimatorCache.get();
 cache->ReanimatorCacheDispose();
 for(auto& image:cache->mPlantImages)if(image){image.reset();++pvzh1_cached_frame_diagnostic[2];}
 for(auto& image:cache->mZombieImages)if(image){image.reset();++pvzh1_cached_frame_diagnostic[2];}
 for(auto& image:cache->mLawnMowers)if(image){image.reset();++pvzh1_cached_frame_diagnostic[2];}
}
