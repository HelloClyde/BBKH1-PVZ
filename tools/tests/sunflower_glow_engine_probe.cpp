/* Real production/highlight additive tracks compared with the original draw. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" volatile uint32_t pvzh1_plant_pose_diagnostic[12];
extern void H1ActorPosePageTestSlots(unsigned);
extern "C" size_t pvzh1_actor_pose_release_pressure(size_t);
namespace {
void Check(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
Sexy::Color Shade(unsigned i){if(i==86)return Sexy::Color(128,128,128,255);if(i>=87)return Sexy::Color(255,255,255,i==87?196:92);return Sexy::Color(i*3,i*3,i*3,255);}
}
extern "C" void pvzh1_sunflower_glow_probe(){
 using namespace Sexy;PlayerInfo p;p.mName="GLOW_READER";p.mLevel=6;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;
 auto* plant=b->AddPlant(2,1,SEED_SUNFLOWER);auto* r=gLawnApp->ReanimationGet(plant->mBodyReanimID);H1PlantPosePrepare(REANIM_SUNFLOWER);r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;
 H1RenderPlanTestEnable(false);H1TextReplayTestEnable(false);auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;unsigned comparisons=0,stressed=0;
 auto compare=[&](unsigned frame,unsigned shade,unsigned background){r->mFrameStart=frame/2;r->mFrameCount=frame/2+1==unsigned(r->mDefinition->mTracks.tracks[0].mTransforms.count)?1:2;r->mAnimTime=(frame&1)*.5f;r->mLoopType=REANIM_LOOP;r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Shade(shade);std::vector<uint32_t> expected;
  for(bool replay:{false,true}){H1ActorPoseTestEnable(replay);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+background*0x38124du)&0xffffffu);Graphics g(screen);auto before=pvzh1_actor_pose_diagnostic[5];r->DrawRenderGroup(&g,0);
   if(!replay)expected.assign(pixels,pixels+480*272);else{Check(pvzh1_actor_pose_diagnostic[5]==before+1,"sunflower glow did not replay offline");if(expected!=std::vector<uint32_t>(pixels,pixels+480*272)){std::fprintf(stderr,"GLOW MISMATCH frame=%u shade=%u bg=%u\n",frame,shade,background);Check(false,"sunflower glow pixels differ");}++comparisons;}
  }
 };
 const unsigned total=r->mDefinition->mTracks.tracks[0].mTransforms.count*2-1;
 for(unsigned f=0;f<total;++f)for(unsigned shade=0;shade<89;++shade)for(unsigned bg=0;bg<3;++bg)compare(f,shade,bg);
 H1ActorPosePageTestSlots(2);while(pvzh1_actor_pose_release_pressure(649024)){};
 for(unsigned f:{4u,25u,46u})for(unsigned shade:{1u,42u,85u,86u,87u,88u})for(unsigned bg=0;bg<3;++bg){compare(f,shade,bg);++stressed;}
 H1ActorPosePageTestSlots(128);r->mExtraAdditiveColor=Color(100,30,80,255);Graphics g(screen);Check(!H1ActorPoseDraw(r,&g,0),"unsupported tint used gray glow");r->mExtraAdditiveColor=Color(255,255,255,130);Check(!H1ActorPoseDraw(r,&g,0),"unsupported alpha used highlight glow");
 r->mEnableExtraAdditiveDraw=false;h1DirtyCanvas.enabled=true;pvzh1_actor_pose_shutdown();Check(!pvzh1_actor_pose_diagnostic[0],"glow cleanup leaked");gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"sunflower-glow-reader-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"intensities\":89,\"two_slot_pressure_cases\":"<<stressed<<",\"unsupported_color_guards\":2,\"shutdown_bytes\":0}\n";
}
