/* Full shader-oracle comparisons for baked head transitions and roll. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CutScene.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpParticle.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "Common.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[12];
namespace{void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}}
extern "C" void pvzh1_offline_transitions_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mName="OFFLINE_TRANSITIONS";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;
 auto* plant=b->AddPlant(2,1,SEED_PEASHOOTER);auto* head=gLawnApp->ReanimationGet(plant->mHeadReanimID);head->mOverlayMatrix.m02=265;head->mOverlayMatrix.m12=300;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;unsigned comparisons=0,hits=0;
 auto fill=[&](unsigned bg){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};
 auto compare=[&](){for(unsigned bg=0;bg<3;++bg){H1ActorPoseTestEnable(false);fill(bg);Graphics g(screen);head->Draw(&g);std::vector<uint32_t> expected(pixels,pixels+480*272);H1ActorPoseTestEnable(true);fill(bg);auto before=pvzh1_actor_pose_diagnostic[5];head->Draw(&g);Check(pvzh1_actor_pose_diagnostic[5]>before,"head transition did not replay");Check(expected==std::vector<uint32_t>(pixels,pixels+480*272),"head transition pixels differ");++comparisons;++hits;}};
 auto clear=[&](){for(int i=0;i<head->mDefinition->mTracks.count;++i)head->mTrackInstances[i].mBlendCounter=0;head->mLoopCount=0;head->mDead=false;};
 for(unsigned oldHalf:{58u,70u,84u,106u}){clear();head->mAnimRate=18;head->SetFramesForLayer("anim_head_idle");head->mAnimTime=float(oldHalf-58)/48;head->StartBlend(20);head->SetFramesForLayer("anim_shooting");head->mAnimRate=35;head->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;
  for(int step=0;step<20;++step){compare();head->Update();}
 }
 for(unsigned oldHalf:{132u,156u})for(unsigned newHalf:{58u,70u,84u,106u})for(unsigned step:{0u,5u,10u,19u}){clear();head->mAnimRate=35;head->SetFramesForLayer("anim_shooting");head->mAnimTime=float(oldHalf-108)/48;head->StartBlend(20);head->SetFramesForLayer("anim_head_idle");head->mAnimTime=float(newHalf-58)/48;
  for(int i=0;i<head->mDefinition->mTracks.count;++i)if(head->mTrackInstances[i].mBlendCounter)head->mTrackInstances[i].mBlendCounter=20-step;compare();}
 b->mX=130;b->mY=0;b->mLevel=1;b->mCutScene->mCutsceneTime=6000;b->mCutScene->mSodTime=2000;Check(H1SodOfflineBegin(b),"complete roll missing");SRand(20261105);
 auto* roll=gLawnApp->AddReanimation(0,0,0,REANIM_SODROLL);auto* dirt=gLawnApp->AddPvzpParticle(35,348,1,PARTICLE_SOD_ROLL);unsigned age=0,rollComparisons=0;
 for(unsigned frame:{0u,1u,2u,10u,20u,40u,60u,80u,90u}){unsigned tick=(frame*5+1)/2;while(age<tick){if(!roll->mDead)roll->Update();if(!dirt->mDead)dirt->Update();++age;}
  b->mCutScene->mCutsceneTime=6000+frame*25;
  for(unsigned bg=0;bg<3;++bg){H1ActorPoseTestEnable(false);fill(bg);Graphics g(screen);g.Translate(130,0);roll->Draw(&g);dirt->Draw(&g);std::vector<uint32_t> expected(pixels,pixels+480*272);H1ActorPoseTestEnable(true);fill(bg);Check(H1SodOfflineDraw(b,&g),"roll replay failed");Check(expected==std::vector<uint32_t>(pixels,pixels+480*272),"roll replay pixels differ");++rollComparisons;}
 }
 b->mCutScene->mCutsceneTime=8251;Graphics g(screen);g.Translate(130,0);Check(!H1SodOfflineDraw(b,&g),"roll outlived original trail");
 pvzh1_actor_pose_shutdown();h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"offline-transitions-verification.json");out<<"{\"passed\":true,\"head_blend_frame_comparisons\":"<<comparisons<<",\"head_blend_replays\":"<<hits<<",\"complete_roll_frame_comparisons\":"<<rollComparisons<<",\"original_blend_updates\":20,\"final_roll_trail_ends\":true,\"shutdown_bytes\":0}\n";
}
