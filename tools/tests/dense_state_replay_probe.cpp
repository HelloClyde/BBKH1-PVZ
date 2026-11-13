/* Original shader versus retained LCD-scale samples; simulation matrices must
 * keep their exact bits after replay, including non-sampled display scales. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "actors/DeferredOverlay.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[12];
namespace {void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"SCALE ORACLE FAILED: %s\n",why);throw std::runtime_error(why);}}}
extern "C" void pvzh1_dense_state_replay_probe(){
 using namespace Sexy;H1RenderPlanTestEnable(false);H1TextReplayTestEnable(false);
 PlayerInfo profile;profile.mName="DENSE_SCALE_ORACLE";profile.mLevel=8;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 Check(H1ActorPosePrepare(true,true,false,true,true),"LCD state bank preparation");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;unsigned comparisons=0;
 auto compare=[&](Reanimation* r,float originalX,float originalY,float sampledX,float sampledY){
  for(unsigned bg=0;bg<2;++bg){auto fill=[&](){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};
   r->mOverlayMatrix.m00=sampledX;r->mOverlayMatrix.m11=sampledY;H1ActorPoseTestEnable(false);fill();Graphics g(screen);r->Draw(&g);std::vector<uint32_t> expected(pixels,pixels+480*272);
   r->mOverlayMatrix.m00=originalX;r->mOverlayMatrix.m11=originalY;SexyTransform2D exact=r->mOverlayMatrix;H1ActorPoseTestEnable(true);fill();auto before=pvzh1_actor_pose_diagnostic[5];
   r->Draw(&g);if(pvzh1_actor_pose_diagnostic[5]==before)std::fprintf(stderr,"REPLAY MISS type=%d scale=%g,%g time=%g clip=%d,%d,%d,%d\n",int(r->mReanimationType),originalX,originalY,r->mAnimTime,g.mClipRect.mX,g.mClipRect.mY,g.mClipRect.mWidth,g.mClipRect.mHeight);Check(pvzh1_actor_pose_diagnostic[5]>before,"LCD sample did not replay");Check(!std::memcmp(&exact,&r->mOverlayMatrix,sizeof(exact)),"raster sampling changed simulation matrix");
   if(!std::equal(expected.begin(),expected.end(),pixels)){std::fprintf(stderr,"SCALE MISMATCH type=%d scale=%g,%g sampled=%g,%g time=%g\n",int(r->mReanimationType),originalX,originalY,sampledX,sampledY,r->mAnimTime);unsigned differences=0;for(unsigned i=0;i<480*272;++i)if(expected[i]!=pixels[i]){if(differences++<10)std::fprintf(stderr,"  xy=%u,%u expected=%08x actual=%08x\n",i%480,i/480,expected[i],pixels[i]);}std::fprintf(stderr,"different pixels=%u additive=%d\n",differences,r->mEnableExtraAdditiveDraw);Check(false,"LCD sample differs from shader oracle");}++comparisons;
  }
 };
 for(auto seed:{SEED_PEASHOOTER,SEED_SUNFLOWER}){auto* plant=gLawnApp->mBoard->AddPlant(2,1,seed);plant->DoBlink();auto* r=gLawnApp->ReanimationGet(plant->mBlinkReanimID);H1ResolveActorOverlay(r);r->mOverlayMatrix=SexyTransform2D();r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;
  for(unsigned scale:{32u,41u,64u,74u,76u,96u})for(unsigned frame=0;frame<5;++frame)for(unsigned shade:{0u,43u,88u}){
   const bool full=r->mLoopType==REANIM_PLAY_ONCE_FULL_LAST_FRAME||r->mLoopType==REANIM_LOOP_FULL_LAST_FRAME||r->mLoopType==REANIM_PLAY_ONCE_FULL_LAST_FRAME_AND_HOLD;
   r->mAnimTime=float(frame)/(2*(r->mFrameCount-(full?0:1)));r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=shade==88?Color(255,255,255,92):Color(shade*3,shade*3,shade*3,255);
   const float sampled=float(scale)/64;compare(r,1,sampled,1,sampled);
   if(scale>32&&scale<96)compare(r,1.0000001f,sampled+.003f,1,sampled);
  }plant->EndBlink();
 }
 auto* r=gLawnApp->AddReanimation(0,0,0,REANIM_SUN);r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;
 for(unsigned scale:{13u,20u,22u,25u,40u,50u,100u})for(unsigned frame=0;frame<49;frame+=3){r->mAnimTime=float(frame)/(4*(r->mFrameCount-1));float s=float(scale)/50;compare(r,s,s,s,s);if(scale!=100){float offset=scale==50?-.003f:.003f;compare(r,s+offset,s+offset,s,s);}}
 pvzh1_actor_pose_shutdown();h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"dense-state-replay-verification.json");out<<"{\"passed\":true,\"shader_frame_comparisons\":"<<comparisons<<",\"simulation_matrix_bits_preserved\":true,\"shutdown_bytes\":0}\n";
}
