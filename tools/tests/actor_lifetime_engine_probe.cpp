/* Resource lifetimes at the original intro/play/delete/load boundaries. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <stdexcept>
extern void H1PreloadCurrentBoard(LawnApp*);
extern "C" volatile uint32_t pvzh1_actor_lifetime_diagnostic[8];
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
namespace {void Check(bool value,const char* why){if(!value){std::fprintf(stderr,"LIFETIME: %s\n",why);throw std::runtime_error(why);}}}
extern "C" void pvzh1_actor_lifetime_probe(){
 PlayerInfo profile;profile.mName="LIFETIME_ORACLE";profile.mId=1;profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
 const unsigned sodBytes=pvzh1_actor_lifetime_diagnostic[2];Check(sodBytes>500000,"intro pixels were not resident");
 Check(board->CanPlantAt(0,2,SEED_PEASHOOTER)==PLANTING_OK&&board->CanPlantAt(1,2,SEED_SUNFLOWER)==PLANTING_OK,"live actor fixture is not legal");
 board->AddPlant(0,2,SEED_PEASHOOTER);board->AddPlant(1,2,SEED_SUNFLOWER);
 std::vector<std::pair<Reanimation*,std::vector<unsigned char>>> states;
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)states.push_back({r,std::vector<unsigned char>((unsigned char*)r,(unsigned char*)r+sizeof(*r))});
 Check(states.size()>=3,"live actor preservation oracle is empty");
 const unsigned before=pvzh1_actor_pose_diagnostic[0];H1ActorPoseFinishSod();
 Check(!pvzh1_actor_lifetime_diagnostic[2]&&pvzh1_actor_pose_diagnostic[0]+sodBytes==before,"finished intro bank remained resident");
 for(auto& [r,state]:states)Check(!std::memcmp(r,state.data(),state.size()),"retirement changed live animation state");
 pvzh1_actor_pose_shutdown();H1PreloadCurrentBoard(gLawnApp);gLawnApp->StartPlaying();
 Check(!pvzh1_actor_lifetime_diagnostic[2],"original StartPlaying did not retire intro pixels");
 Check(pvzh1_actor_lifetime_diagnostic[0]&&pvzh1_actor_lifetime_diagnostic[1]&&pvzh1_actor_lifetime_diagnostic[3],"play bank was retired with intro");
 auto* plant=board->AddPlant(2,1,SEED_PEASHOOTER);auto* body=gLawnApp->ReanimationGet(plant->mBodyReanimID);body->mOverlayMatrix.LoadIdentity();body->mOverlayMatrix.m02=265;body->mOverlayMatrix.m12=300;
 Sexy::Graphics graphics(gLawnApp->mGLInterface->GetScreenImage());body->DrawRenderGroup(&graphics,0);
 const unsigned forgotten=pvzh1_actor_lifetime_diagnostic[5];plant->Die();gLawnApp->mEffectSystem->ProcessDeleteQueue();gLawnApp->mEffectSystem->ProcessDeleteQueue();
 Check(pvzh1_actor_lifetime_diagnostic[5]>forgotten,"deleted actor retained signature records");
 const auto resources=Sexy::GetResourceFolder();const auto missing=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"missing-required-pixels";std::filesystem::create_directory(missing);
 pvzh1_actor_pose_shutdown();Sexy::SetResourceFolder(missing.string());bool rejected=false;const auto scene=gLawnApp->mGameScene;
 try{H1PreloadCurrentBoard(gLawnApp);}catch(const std::runtime_error& e){rejected=std::strstr(e.what(),"offline actor bank preload failed");}
 Check(rejected&&gLawnApp->mGameScene==scene,"required bank failure continued into play");
 Sexy::SetResourceFolder(resources);pvzh1_actor_pose_shutdown();H1PreloadCurrentBoard(gLawnApp);
 Check(!pvzh1_actor_lifetime_diagnostic[2],"mid-game preload reloaded a finished intro");
 gLawnApp->KillBoard();pvzh1_actor_pose_shutdown();Check(!pvzh1_actor_pose_diagnostic[0],"lifetime cleanup leaked");gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"actor-lifetime-verification.json");out<<"{\"passed\":true,\"retired_intro_bytes\":"<<sodBytes<<",\"unchanged_live_actor_states\":"<<states.size()<<",\"original_start_playing_retires_intro\":true,\"deleted_actor_memo_released\":true,\"required_bank_failure_rejected_before_play\":true,\"midgame_intro_reload\":false,\"shutdown_bytes\":0}\n";
}
