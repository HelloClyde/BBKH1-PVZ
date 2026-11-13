/* Full shader-oracle comparisons for baked head transitions and roll. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CutScene.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/Attachment.h"
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
extern "C" void pvzh1_layered_head_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mName="OFFLINE_TRANSITIONS";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;
 auto* plant=b->AddPlant(2,1,SEED_PEASHOOTER);auto* head=gLawnApp->ReanimationGet(plant->mHeadReanimID);head->mOverlayMatrix.m02=265;head->mOverlayMatrix.m12=300;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;unsigned comparisons=0,hits=0;
 auto fill=[&](unsigned bg){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};
 auto compare=[&](){for(unsigned bg=0;bg<3;++bg){H1ActorPoseTestEnable(false);fill(bg);Graphics g(screen);head->Draw(&g);std::vector<uint32_t> expected(pixels,pixels+480*272);H1ActorPoseTestEnable(true);fill(bg);auto before=pvzh1_actor_pose_diagnostic[5];head->Draw(&g);Check(pvzh1_actor_pose_diagnostic[5]>before,"head transition did not replay");Check(expected==std::vector<uint32_t>(pixels,pixels+480*272),"head transition pixels differ");++comparisons;++hits;}};
 auto clear=[&](){plant->EndBlink();for(int i=0;i<head->mDefinition->mTracks.count;++i)AttachmentDie(head->mTrackInstances[i].mAttachmentID);for(int i=0;i<head->mDefinition->mTracks.count;++i)head->mTrackInstances[i].mBlendCounter=0;head->mLoopCount=0;head->mDead=false;};
 for(unsigned oldHalf:{58u,70u,84u,106u}){clear();plant->DoBlink();head->mAnimRate=18;head->SetFramesForLayer("anim_head_idle");head->mAnimTime=float(oldHalf-58)/48;head->StartBlend(20);head->SetFramesForLayer("anim_shooting");head->mAnimRate=35;head->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;
  for(int step=0;step<20;++step){compare();head->Update();}
 }
 for(unsigned oldHalf:{132u,156u})for(unsigned newHalf:{58u,70u,84u,106u})for(unsigned step:{0u,5u,10u,19u}){clear();plant->DoBlink();head->mAnimRate=35;head->SetFramesForLayer("anim_shooting");head->mAnimTime=float(oldHalf-108)/48;head->StartBlend(20);head->SetFramesForLayer("anim_head_idle");head->mAnimTime=float(newHalf-58)/48;
  for(int i=0;i<head->mDefinition->mTracks.count;++i)if(head->mTrackInstances[i].mBlendCounter)head->mTrackInstances[i].mBlendCounter=20-step;compare();}
 pvzh1_actor_pose_shutdown();h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"layered-head-verification.json");out<<"{\"passed\":true,\"head_blend_frame_comparisons\":"<<comparisons<<",\"head_blend_replays\":"<<hits<<",\"original_blend_updates\":20,\"shutdown_bytes\":0}\n";
}
