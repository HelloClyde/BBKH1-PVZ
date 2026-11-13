/* Original board preload with a real Boss factory and active borrowers. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern void H1PreloadCurrentBoardForPlay(LawnApp*);
namespace {
unsigned Bytes(){unsigned bytes=0;for(unsigned i=0;i<gReanimatorDefCount;++i){const auto&d=gReanimatorDefArray[i];bytes+=d.mTracks.count*12;for(int t=0;t<d.mTracks.count;++t)bytes+=d.mTracks.tracks[t].mTransforms.count*44;}return bytes;}
void Check(bool okay,const char*why){if(!okay){fprintf(stderr,"BOSS_RESIDENCY: %s\n",why);throw std::runtime_error(why);}}
}
extern "C" void pvzh1_boss_play_residency_probe(){
 PlayerInfo profile;profile.mName="BOSS_PLAY_RESIDENCY";profile.mLevel=50;auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto*z=gLawnApp->mBoard->GetBossZombie();if(!z)z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_BOSS,0,-2);Check(z,"Original Boss factory failed");
 struct Borrower{Reanimation*r;ReanimatorDefinition*definition;ReanimatorTrack*tracks;int count;};std::vector<Borrower> borrowers;
 for(auto*r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)borrowers.push_back({r,r->mDefinition,r->mDefinition->mTracks.tracks,r->mDefinition->mTracks.count});
 const auto before=Bytes();H1PreloadCurrentBoardForPlay(gLawnApp);const auto after=Bytes();
 for(const auto&b:borrowers)Check(b.r->mDefinition==b.definition&&b.definition->mTracks.tracks==b.tracks&&b.definition->mTracks.count==b.count,"An active/pending definition was retired");
 Check(after+2*1024*1024<before,"Unused future-enemy definitions still occupy Boss playing memory");
 ReanimatorEnsureDefinitionLoaded(REANIM_ZOMBIE_NEWSPAPER,true);Check(gReanimatorDefArray[REANIM_ZOMBIE_NEWSPAPER].mTracks.count>0,"Future enemy cannot reload original definition");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-play-residency-verification.json");out<<"{\"passed\":true,\"definition_bytes_mips32_before\":"<<before<<",\"definition_bytes_mips32_after\":"<<after<<",\"active_or_pending_borrowers_preserved\":"<<borrowers.size()<<",\"original_boss_factory\":true,\"future_enemy_reload\":true,\"animation_files_added\":0}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
