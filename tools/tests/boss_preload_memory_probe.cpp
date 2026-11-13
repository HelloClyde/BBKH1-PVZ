/* Actual boss-board dependencies, before its first rendered frame. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/EarlyLawn.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_actor_lifetime_diagnostic[8];
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" volatile uint32_t pvzh1_early_lawn_diagnostic[8];
extern "C" void pvzh1_actor_pose_shutdown();
extern "C" void pvzh1_boss_preload_memory_probe(){
 PlayerInfo profile;profile.mName="BOSS_PRELOAD_MEMORY";profile.mLevel=50;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 if(pvzh1_actor_lifetime_diagnostic[3])throw std::runtime_error("Boss level retained unnecessary pea/sunflower normal pixels");
 const auto introBytes=pvzh1_early_lawn_diagnostic[0];
 if(introBytes<480*272*4||introBytes>530000)throw std::runtime_error("Boss intro should retain exactly one target-size background view");
 std::vector<std::pair<unsigned,unsigned>> defs;
 for(unsigned i=0;i<gReanimatorDefCount;++i){const auto&d=gReanimatorDefArray[i];unsigned bytes=d.mTracks.count*12;
  for(int t=0;t<d.mTracks.count;++t)bytes+=d.mTracks.tracks[t].mTransforms.count*44;
  if(bytes)defs.push_back({bytes,i});}
 std::sort(defs.rbegin(),defs.rend());unsigned sum=0;
 for(auto [bytes,type]:defs){sum+=bytes;fprintf(stderr,"BOSS_DEFINITION type=%u native_estimated_bytes=%u\n",type,bytes);}
 for(auto*p:gLawnApp->mBoard->mPlants)fprintf(stderr,"BOSS_PLANT type=%u\n",unsigned(p->mSeedType));
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-preload-verification.json");
 out<<"{\"passed\":true,\"unused_normal_bank_bytes\":0,\"boss_intro_static_view_bytes\":"<<introBytes<<",\"unused_playing_view_during_intro\":0,\"definition_array_estimate_mips32\":"<<sum<<",\"loaded_definitions\":"<<defs.size()<<",\"baked_pixels_host_bytes\":"<<pvzh1_actor_pose_diagnostic[0]<<",\"native_timing\":false}\n";
 if(!H1EarlyLawnPrepare(gLawnApp->mBoard,true)||!pvzh1_early_lawn_diagnostic[0])throw std::runtime_error("Boss playing view cannot prepare its lawn pixels");
 gLawnApp->KillBoard();pvzh1_actor_pose_shutdown();gLawnApp->mPlayerInfo=old;
}
