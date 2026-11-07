/* Build-time invocation of the complete original cached-preview family. */
#include "LawnApp.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/ReanimationLawn.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "actors/CachedFrames.h"
#include "graphics/MemoryImage.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <cstdio>
#include <stdexcept>
#include <zlib.h>
extern "C" void pvzh1_cached_frames_export(){
 H1ActorPoseTestEnable(false);
 H1CachedFramesTestEnable(false);
 const auto folder=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));std::filesystem::create_directory(folder/"raw");
 std::ofstream manifest(folder/"cached-frames.json");manifest<<"{\"original_cached_frame_factories\":true,\"files\":[";unsigned count=0,plants=0,zombies=0,mowers=0;
 auto save=[&](const char* name,int kind,int id,int variation,std::unique_ptr<Sexy::MemoryImage> image){
  if(!image||!image->mBits)throw std::runtime_error("Original preview factory did not produce pixels");
  const unsigned bytes=image->mWidth*image->mHeight*4;
  std::ofstream out(folder/"raw"/(std::string(name)+".argb"),std::ios::binary);out.write(reinterpret_cast<const char*>(image->mBits.get()),bytes);if(!out)throw std::runtime_error("Offline preview write failed");
  if(count++)manifest<<",";
  manifest<<"{\"name\":\""<<name<<"\",\"kind\":"<<kind<<",\"id\":"<<id<<",\"variation\":"<<variation<<",\"width\":"<<image->mWidth<<",\"height\":"<<image->mHeight<<",\"bytes\":"<<bytes<<",\"crc32\":"<<crc32(0,reinterpret_cast<const Bytef*>(image->mBits.get()),bytes)<<"}";
 };
 auto* cache=gLawnApp->mReanimatorCache.get();
 for(int seed=0;seed<int(NUM_SEED_TYPES);++seed){
  Plant::PreloadPlantResources(SeedType(seed));std::set<int> variants={int(VARIATION_NORMAL),int(VARIATION_IMITATER),int(VARIATION_IMITATER_LESS)};
  const auto type=GetPlantDefinition(SeedType(seed)).mReanimationType;Reanimation inspect;inspect.ReanimationInitializeType(0,0,type);
  if(inspect.TrackExists("Marigold_petals"))for(int v=int(VARIATION_MARIGOLD_WHITE);v<=int(VARIATION_MARIGOLD_LIGHT_GREEN);++v)variants.insert(v);
  for(auto [name,variation]:{std::pair{"anim_zengarden",VARIATION_ZEN_GARDEN},{"anim_waterplants",VARIATION_ZEN_GARDEN_WATER},{"anim_idle_noflower",VARIATION_SPROUT_NO_FLOWER},{"anim_idle_aquarium",VARIATION_AQUARIUM}})if(inspect.TrackExists(name))variants.insert(int(variation));
  for(int variation:variants){char name[40];std::snprintf(name,sizeof(name),"p%02d-v%02d",seed,variation);std::fprintf(stderr,"BAKE PREVIEW %s\n",name);save(name,0,seed,variation,cache->MakeCachedPlantFrame(SeedType(seed),DrawVariation(variation)));++plants;}
 }
 for(int mower=0;mower<int(NUM_MOWER_TYPES);++mower){char name[40];std::snprintf(name,sizeof(name),"m%02d",mower);save(name,1,mower,0,cache->MakeCachedMowerFrame(LawnMowerType(mower)));++mowers;}
 for(int zombie=0;zombie<int(NUM_CACHED_ZOMBIE_TYPES);++zombie){
  const auto use=zombie==int(ZOMBIE_CACHED_POLEVAULTER_WITH_POLE)?ZOMBIE_POLEVAULTER:ZombieType(zombie);const auto& def=GetZombieDefinition(use);if(def.mReanimationType==REANIM_NONE)continue;
  char name[40];std::snprintf(name,sizeof(name),"z%02d",zombie);std::fprintf(stderr,"BAKE PREVIEW %s\n",name);Zombie::PreloadZombieResources(use);save(name,2,zombie,0,cache->MakeCachedZombieFrame(ZombieType(zombie)));++zombies;
 }
 manifest<<"],\"plant_variants\":"<<plants<<",\"mowers\":"<<mowers<<",\"zombies\":"<<zombies<<",\"total\":"<<count<<"}\n";
}
