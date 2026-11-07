/* Real definition ownership, active-reference guards and original reloads. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include <bit>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <vector>
extern void H1ReanimationDefinitionsTrimForMenu();
extern "C" volatile uint32_t pvzh1_definition_retirement_diagnostic[4];
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
uint64_t Fingerprint(ReanimatorDefinition& d){
 uint64_t hash=14695981039346656037ull;
 auto byte=[&](unsigned char b){hash^=b;hash*=1099511628211ull;};
 auto word=[&](uint32_t w){for(unsigned i=0;i<4;++i)byte(w>>(i*8));};
 auto text=[&](const char* p){while(*p)byte(*p++);byte(0);};
 word(d.mTracks.count);word(std::bit_cast<uint32_t>(d.mFPS));
 for(int t=0;t<d.mTracks.count;++t){const auto& track=d.mTracks.tracks[t];text(track.mName);word(track.mTransforms.count);
  for(int f=0;f<track.mTransforms.count;++f){const auto& a=track.mTransforms.mTransforms[f];
   for(float v:{a.mTransX,a.mTransY,a.mSkewX,a.mSkewY,a.mScaleX,a.mScaleY,a.mFrame,a.mAlpha})word(std::bit_cast<uint32_t>(v));text(a.mText);}
 }return hash;
}
}
extern "C" void pvzh1_definition_retirement_probe(){
 PlayerInfo profile;profile.mName="DEFINITION_RETIREMENT";profile.mLevel=6;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 unsigned retained=0,reloads=0;uint64_t original=0;
 for(unsigned cycle=0;cycle<3;++cycle){
  gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
  board->AddPlant(2,1,SEED_PEASHOOTER);board->AddPlant(3,2,SEED_SUNFLOWER);board->AddPlant(4,2,SEED_WALLNUT);
  ReanimatorEnsureDefinitionLoaded(REANIM_ZOMBIE,true);auto& zombie=gReanimatorDefArray[REANIM_ZOMBIE];
  const auto expected=Fingerprint(zombie);if(!cycle)original=expected;Check(expected==original,"zombie reload changed authored definition");
  std::vector<std::pair<ReanimatorDefinition*,uint64_t>> active;
  for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)active.push_back({r->mDefinition,Fingerprint(*r->mDefinition)});
  H1ReanimationDefinitionsTrimForMenu();
  for(auto [d,before]:active){Check(d->mTracks.tracks&&Fingerprint(*d)==before,"trim changed a definition referenced by a live or dead actor");++retained;}
  // The original board retirement and menu construction call the same hook.
  gLawnApp->ShowGameSelector();Check(!gLawnApp->mBoard&&gLawnApp->mGameSelector,"ordinary return did not construct the menu");
  Check(!gReanimatorDefArray[REANIM_PEASHOOTER].mTracks.tracks,"unused plant definition survived menu retirement");
  Check(!zombie.mTracks.tracks,"unused zombie definition survived menu retirement");
  ReanimatorEnsureDefinitionLoaded(REANIM_ZOMBIE,true);Check(Fingerprint(zombie)==original,"retired definition reload changed numbers or strings");++reloads;
 }
 H1ReanimationDefinitionsTrimForMenu();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"definition-retirement-verification.json");
 out<<"{\"passed\":true,\"menu_cycles\":3,\"active_definition_guards\":"<<retained<<",\"original_definition_reloads\":"<<reloads<<",\"definitions_retired\":"<<pvzh1_definition_retirement_diagnostic[0]<<",\"host_payload_bytes_released\":"<<pvzh1_definition_retirement_diagnostic[1]<<"}\n";
}
