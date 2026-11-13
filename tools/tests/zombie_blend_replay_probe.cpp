/* Full original-shader oracle for the selected transition, with damaged hats,
 * missing body parts, all retained flash shades and unchanged actor clocks. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "actors/DenseStateFormat.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "Resources.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
extern "C" volatile uint32_t pvzh1_dense_replay_diagnostic[4];
namespace {void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
extern "C" void pvzh1_zombie_blend_replay_probe(){
 using namespace Sexy;H1RenderPlanTestEnable(false);H1TextReplayTestEnable(false);
 PlayerInfo profile;profile.mName="ZOMBIE_BLEND_ORACLE";profile.mLevel=8;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 Check(H1ActorPosePrepare(true,true,false,true,true),"zombie blend bank preparation");
 auto* z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_NORMAL,2,0);auto* r=gLawnApp->ReanimationGet(z->mBodyReanimID);r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;unsigned comparisons=0;
 std::vector<int> groups;for(int i=0;i<r->mDefinition->mTracks.count;++i)groups.push_back(r->mTrackInstances[i].mRenderGroup);
 auto compare=[&](unsigned source,unsigned step,unsigned style,unsigned shade){
  auto fill=[&](){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+shade*0x38124du)&0xffffffu);};
  const auto clock=r->mAnimTime;std::vector<int> counters;for(int i=0;i<r->mDefinition->mTracks.count;++i)counters.push_back(r->mTrackInstances[i].mBlendCounter);
  const unsigned sampled=H1ActorPose::ZombieDisplayBlendStep(step);float sampledClock=0;for(unsigned i=0;i<sampled;++i)sampledClock+=H1ActorAnimIncrement(r);
  r->mAnimTime=sampledClock;for(int i=0;i<r->mDefinition->mTracks.count;++i)if(counters[i])r->mTrackInstances[i].mBlendCounter=20-sampled;
  H1ActorPoseTestEnable(false);fill();Graphics g(screen);r->Draw(&g);std::vector<uint32_t> expected(pixels,pixels+480*272);
  r->mAnimTime=clock;for(int i=0;i<r->mDefinition->mTracks.count;++i)r->mTrackInstances[i].mBlendCounter=counters[i];
  H1ActorPoseTestEnable(true);fill();auto before=pvzh1_dense_replay_diagnostic[2];r->Draw(&g);
  Check(pvzh1_dense_replay_diagnostic[2]==before+1,"zombie blend did not use its baked packet");Check(clock==r->mAnimTime,"zombie replay changed animation clock");
  if(!std::equal(expected.begin(),expected.end(),pixels)){std::fprintf(stderr,"ZOMBIE BLEND MISMATCH source=%u step=%u style=%u shade=%u\n",source,step,style,shade);Check(false,"baked zombie blend pixels differ");}++comparisons;
 };
 for(unsigned source:{88u,105u,141u,179u,180u})for(unsigned style=0;style<8;++style){
  r->mEnableExtraAdditiveDraw=false;r->mDead=false;r->mLoopCount=0;r->mLoopType=REANIM_LOOP;
  for(int i=0;i<r->mDefinition->mTracks.count;++i){auto& t=r->mTrackInstances[i];t.mBlendCounter=0;t.mImageOverride=nullptr;t.mRenderGroup=groups[i];}
  if(style<6){bool bucket=style>=3;auto& hat=r->mTrackInstances[r->FindTrackIndex(bucket?"anim_bucket":"anim_cone")];hat.mRenderGroup=0;unsigned damage=style%3;hat.mImageOverride=damage==1?(bucket?IMAGE_REANIM_ZOMBIE_BUCKET2:IMAGE_REANIM_ZOMBIE_CONE2):damage==2?(bucket?IMAGE_REANIM_ZOMBIE_BUCKET3:IMAGE_REANIM_ZOMBIE_CONE3):nullptr;}
  if(style==7){r->AssignRenderGroupToPrefix("anim_head",RENDER_GROUP_HIDDEN);r->AssignRenderGroupToPrefix("anim_innerarm",RENDER_GROUP_HIDDEN);r->mTrackInstances[r->FindTrackIndex("Zombie_outerarm_upper")].mImageOverride=IMAGE_REANIM_ZOMBIE_OUTERARM_UPPER2;}
  r->mAnimRate=12;r->SetFramesForLayer("anim_walk");r->mAnimTime=float(source-88)/92;r->StartBlend(20);r->mAnimRate=36;r->SetFramesForLayer("anim_eat");
  for(unsigned step=0;step<20;++step){for(unsigned shade:{0u,1u,13u,25u}){r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);compare(source,step,style,shade);}r->Update();}
 }
 pvzh1_actor_pose_shutdown();h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"zombie-blend-replay-verification.json");out<<"{\"passed\":true,\"shader_frame_comparisons\":"<<comparisons<<",\"baked_blend_replays\":"<<pvzh1_dense_replay_diagnostic[2]<<",\"original_blend_updates\":20,\"simulation_clock_preserved\":true,\"shutdown_bytes\":0}\n";
}
