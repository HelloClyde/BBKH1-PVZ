/* Observe actual original shooting/attachment updates and cache misses. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[12];
extern "C" void pvzh1_actor_shoot_coverage_probe(){
 PlayerInfo p;p.mName="SHOOT_COVERAGE";p.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;b->mX=130;gLawnApp->mGameScene=SCENE_PLAYING;
 auto* pea=b->AddPlant(2,1,SEED_PEASHOOTER);auto* zombie=b->AddZombieInRow(ZOMBIE_NORMAL,1,0);zombie->mPosX=600;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();h1DirtyCanvas.enabled=false;
 std::ofstream o(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"shoot-coverage.jsonl");
 for(int update=0;update<400;++update){
  pea->Update();zombie->Update();gLawnApp->mEffectSystem->Update();gLawnApp->mEffectSystem->ProcessDeleteQueue();
  auto* head=gLawnApp->ReanimationGet(pea->mHeadReanimID);Sexy::Graphics g(screen);h1DirtyCanvas.RequireClear();
  unsigned blend=0;for(int i=0;i<head->mDefinition->mTracks.count;++i)blend=std::max<unsigned>(blend,head->mTrackInstances[i].mBlendCounter);
  uint64_t key=0;bool signature=H1ActorPose::Signature(head,0,key,true);auto before=pvzh1_actor_pose_diagnostic[5];bool hit=H1ActorPoseDraw(head,&g,0);
  if(!hit)throw std::runtime_error("Original repeated shooting missed offline head recipe");
  if(blend)o<<"{\"update\":"<<update<<",\"hit\":"<<hit<<",\"blend\":"<<blend<<",\"clip\":"<<head->mFrameStart<<",\"base\":"<<head->mFrameBasePose<<",\"time\":"<<head->mAnimTime<<",\"rate\":"<<head->mAnimRate<<",\"signature\":"<<signature<<",\"key\":"<<key<<",\"misses\":"<<pvzh1_actor_pose_diagnostic[2]<<",\"guards\":"<<pvzh1_actor_pose_diagnostic[3]<<"}\n";
 }
 h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream summary(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"shoot-coverage-verification.json");summary<<"{\"passed\":true,\"original_plant_updates\":400,\"head_replay_hits\":400,\"manual_shoot_triggers\":0}\n";
}
