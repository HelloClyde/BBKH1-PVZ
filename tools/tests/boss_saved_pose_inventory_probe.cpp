#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/MemoryImageCache.h"
#include <set>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern void H1PreloadCurrentBoardForPlay(LawnApp*);
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" void pvzh1_boss_saved_pose_inventory_probe(){
 using namespace Sexy;PlayerInfo p;p.mName="SAVED_BOSS";p.mId=1;p.mLevel=50;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto*b=gLawnApp->mBoard;
 if(!LawnLoadGame(b,std::filesystem::absolute("build/zombie-logical-v26-hardware/fixture-sustained-game1_0.v4").string()))throw std::runtime_error("Cannot load actual H1 fixture save");
 H1PreloadCurrentBoardForPlay(gLawnApp);b->mPaused=false;b->mX=130;b->mY=0;gLawnApp->mGameScene=SCENE_PLAYING;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();
 h1DirtyCanvas.Begin(target);Graphics bg(screen);bg.Translate(b->mX,b->mY);b->Draw(&bg);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-saved-pose-inventory.json");out<<"{\"actors\":[";bool first=true;
 for(auto*z:b->mZombies){
  auto*r=gLawnApp->ReanimationTryToGet(z->mBodyReanimID);if(!r||r->mDead||r->mReanimationType!=REANIM_ZOMBIE)continue;
  std::set<int> groups;for(int i=0;i<r->mDefinition->mTracks.count;++i)if(r->mTrackInstances[i].mRenderGroup>=0)groups.insert(r->mTrackInstances[i].mRenderGroup);
  ReanimatorFrameTime ft;r->GetFrameTime(&ft);
  for(auto group:groups){
   uint64_t key;bool signature=H1ActorPose::Signature(r,group,key,false,true);
   const auto misses=pvzh1_actor_pose_diagnostic[2],rejects=pvzh1_actor_pose_diagnostic[3];
   Graphics g(screen);g.Translate(b->mX,b->mY);bool replay=H1ActorPoseDraw(r,&g,group);
   if(!first)out<<',';first=false;
   out<<"{\"zombie_type\":"<<int(z->mZombieType)<<",\"phase\":"<<int(z->mZombiePhase)<<",\"group\":"<<group<<",\"start\":"<<r->mFrameStart<<",\"count\":"<<r->mFrameCount<<",\"before\":"<<ft.mAnimFrameBeforeInt<<",\"fraction\":"<<ft.mFraction<<",\"rate\":"<<r->mAnimRate<<",\"signature\":"<<signature<<",\"key\":"<<key<<",\"replay\":"<<replay<<",\"miss_delta\":"<<pvzh1_actor_pose_diagnostic[2]-misses<<",\"reject_delta\":"<<pvzh1_actor_pose_diagnostic[3]-rejects<<",\"tracks\":[";
   for(int i=0;i<r->mDefinition->mTracks.count;++i){if(i)out<<',';const auto&t=r->mTrackInstances[i];out<<"{\"name\":\""<<r->mDefinition->mTracks.tracks[i].mName<<"\",\"group\":"<<t.mRenderGroup<<",\"override\":"<<bool(t.mImageOverride)<<",\"blend\":"<<t.mBlendCounter<<'}';}
   out<<"]}";
  }
 }
 out<<"]}\n";gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
