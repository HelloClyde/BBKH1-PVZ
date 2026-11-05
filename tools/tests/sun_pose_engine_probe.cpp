/* Actual coin attachment drawing compared with the original Sun shader. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Coin.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
namespace {void Check(bool v,const char* why){if(!v)throw std::runtime_error(why);}}
extern "C" void pvzh1_sun_pose_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mName="SUN_REPLAY";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;auto* coin=b->AddCoin(105,270,CoinType::COIN_SUN,CoinMotion::COIN_MOTION_FROM_SKY);
 Reanimation* sun=nullptr;for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)if(!r->mDead&&r->mReanimationType==REANIM_SUN)sun=r;Check(sun,"real coin has no Sun attachment");
 gLawnApp->mGameScene=GameScenes::SCENE_PLAYING;coin->mGroundY=270;coin->mVelX=coin->mVelY=0;coin->mHitGround=true;coin->Update();
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;unsigned comparisons=0;
 auto fill=[&](unsigned bg){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};
 const unsigned frames=sun->mDefinition->mTracks.tracks[0].mTransforms.count;
 for(unsigned frame=0;frame<frames;++frame)for(unsigned part=0;part<(frame+1==frames?1u:4u);++part){
  sun->mFrameStart=frame;sun->mFrameCount=frame+1==frames?1:2;sun->mAnimTime=part*.25f;
  for(unsigned bg=0;bg<3;++bg){Graphics g(screen);g.Translate(130+coin->mX,coin->mY);H1ActorPoseTestEnable(false);fill(bg);coin->Draw(&g);std::vector<uint32_t> expected(pixels,pixels+480*272);
   H1ActorPoseTestEnable(true);fill(bg);auto before=pvzh1_actor_pose_diagnostic[12];coin->Draw(&g);Check(pvzh1_actor_pose_diagnostic[12]==before+1,"real coin did not replay baked Sun");Check(expected==std::vector<uint32_t>(pixels,pixels+480*272),"Sun attachment pixels differ");++comparisons;
  }
 }
 sun->mFrameStart=0;sun->mFrameCount=frames;sun->mAnimTime=0;unsigned updates=0;
 for(unsigned i=0;i<200;++i){coin->Update();sun->Update();Graphics g(screen);g.Translate(130+coin->mX,coin->mY);fill(0);auto before=pvzh1_actor_pose_diagnostic[12];coin->Draw(&g);Check(pvzh1_actor_pose_diagnostic[12]==before+1,"ordinary Sun lost cached drawing");++updates;}
 sun->mColorOverride=Color(255,255,255,128);Graphics g(screen);Check(!H1ActorPoseDraw(sun,&g,0),"fade failed to retain original drawing");sun->mColorOverride=Color::White;sun->mOverlayMatrix.m00=.5f;Check(!H1ActorPoseDraw(sun,&g,0),"unsupported scale failed to retain original drawing");
 pvzh1_actor_pose_shutdown();h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"sun-pose-engine-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"ordinary_coin_updates_and_replays\":"<<updates<<",\"color_and_scale_fallbacks\":2,\"sample_rate_hz\":24,\"shutdown_bytes\":0}\n";
}
