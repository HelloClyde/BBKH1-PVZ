/* Offline body and hat packets against the original shader, including damage and hit colors. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "Resources.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" volatile uint32_t pvzh1_combat_sprite_diagnostic[12];
namespace {void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
extern "C" void pvzh1_zombie_hat_replay_probe(){
 using namespace Sexy;PlayerInfo p;p.mName="HAT_BODY";p.mLevel=2;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();
 auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;unsigned comparisons=0,quantized=0;
 for(auto type:{ZOMBIE_TRAFFIC_CONE,ZOMBIE_PAIL}){
  auto*z=gLawnApp->mBoard->AddZombieInRow(type,2,0);auto*r=gLawnApp->ReanimationGet(z->mBodyReanimID);
  r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;r->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;
  const char* hat=type==ZOMBIE_TRAFFIC_CONE?"anim_cone":"anim_bucket";
  auto* track=r->GetTrackInstanceByName(hat);Check(track,"Missing real hat track");
  Image* variants[3]={nullptr,type==ZOMBIE_TRAFFIC_CONE?IMAGE_REANIM_ZOMBIE_CONE2:IMAGE_REANIM_ZOMBIE_BUCKET2,
   type==ZOMBIE_TRAFFIC_CONE?IMAGE_REANIM_ZOMBIE_CONE3:IMAGE_REANIM_ZOMBIE_BUCKET3};
  auto compare=[&](float referenceTime,float replayTime,unsigned bg){
   std::vector<uint32_t> expected;
   for(bool replay:{false,true}){
    H1ActorPoseTestEnable(replay);h1DirtyCanvas.RequireClear();
    for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);
    r->mAnimTime=replay?replayTime:referenceTime;
    Graphics g(screen);auto before=pvzh1_actor_pose_diagnostic[5],hats=pvzh1_combat_sprite_diagnostic[3];r->DrawRenderGroup(&g,0);
    if(!replay)expected.assign(pixels,pixels+480*272);
    else{if(track->mRenderGroup==RENDER_GROUP_NORMAL)Check(pvzh1_combat_sprite_diagnostic[3]>hats,"Visible hat did not replay");Check(pvzh1_actor_pose_diagnostic[5]>=before+1,"Hatted zombie body did not reuse existing offline packet");
     if(!std::equal(expected.begin(),expected.end(),pixels)){fprintf(stderr,"HAT mismatch type=%d start=%d ref=%f replay=%f bg=%u\n",int(type),r->mFrameStart,referenceTime,replayTime,bg);throw std::runtime_error("Hat/body paint order or pixels changed");}
     ++comparisons;}
   }
  };
  for(unsigned damage=0;damage<4;++damage){
   track->mImageOverride=variants[damage%3];track->mRenderGroup=damage==3?RENDER_GROUP_HIDDEN:RENDER_GROUP_NORMAL;
   for(unsigned frame:{0u,1u,88u,89u,182u,183u,234u,235u,354u,355u,498u,499u})
   for(unsigned shade:{0u,10u,25u})for(unsigned bg=0;bg<2;++bg){
    r->mFrameStart=frame/2;r->mFrameCount=2;r->mEnableExtraAdditiveDraw=shade!=0;
    r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);compare((frame&1)*.5f,(frame&1)*.5f,bg);
   }
  }
  track->mImageOverride=nullptr;track->mRenderGroup=RENDER_GROUP_NORMAL;r->mEnableExtraAdditiveDraw=false;
  r->mFrameStart=44;r->mFrameCount=2;
  for(float actual:{0.f,.249f,.25f,.499f,.5f,.749f,.75f,.999f})for(unsigned bg=0;bg<2;++bg){
   float sample=actual>=.75f?1.f:actual>=.25f?.5f:0.f;compare(sample,actual,bg);++quantized;
  }
  // Finished/dead actors must not leave their separately drawn hats behind.
  r->mDead=true;
  for(bool replay:{false,true}){
   H1ActorPoseTestEnable(replay);h1DirtyCanvas.RequireClear();
   std::fill_n(pixels,480*272,0xff7194c5u);Graphics g(screen);r->DrawRenderGroup(&g,0);
   Check(std::all_of(pixels,pixels+480*272,[](uint32_t v){return v==0xff7194c5u;}),"Dead zombie left a hat behind");
  }
  r->mDead=false;
 }
 H1ActorPoseTestEnable(true);h1DirtyCanvas.enabled=true;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"zombie-hat-replay-verification.json");
 out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"hat_types\":2,\"damage_or_hidden_states\":4,\"hit_shades\":3,\"quantized_fraction_cases\":"<<quantized<<",\"separate_hat_bank\":true}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
