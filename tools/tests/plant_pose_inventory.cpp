/* Inspect every original plant definition and the real initialized actors. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
extern "C" void pvzh1_plant_pose_inventory(){
 PlayerInfo profile;profile.mName="PLANT_COMPILER";profile.mId=1;profile.mLevel=50;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"plant-inventory.json");out<<"[\n";
 std::set<Reanimation*> seen;bool first=true;
 for(int seed=0;seed<int(NUM_SEED_TYPES);++seed){
  Plant::PreloadPlantResources(SeedType(seed));
  board->AddPlant(2,1,SeedType(seed));
  for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
   if(r->mDead||seen.contains(r))continue;seen.insert(r);
   if(!first)out<<",\n";first=false;
   out<<"{\"seed\":"<<seed<<",\"plant\":"<<std::quoted(gPlantDefs[seed].mPlantName)<<",\"type\":"<<int(r->mReanimationType)<<",\"tracks\":"<<r->mDefinition->mTracks.count<<",\"frames\":"<<r->mDefinition->mTracks.tracks[0].mTransforms.count<<",\"base_pose\":"<<r->mFrameBasePose<<",\"clips\":[";
   bool clipFirst=true;for(int i=0;i<r->mDefinition->mTracks.count;++i){const char* name=r->mDefinition->mTracks.tracks[i].mName;if(std::strncmp(name,"anim_",5))continue;
    int start,count;r->GetFramesForLayer(name,start,count);if(!clipFirst)out<<',';clipFirst=false;out<<'['<<std::quoted(name)<<','<<start<<','<<count<<']';}
   out<<"]}";
  }
 }
 out<<"\n]\n";gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
