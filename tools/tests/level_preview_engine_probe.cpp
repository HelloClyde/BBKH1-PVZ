/* Original renderer versus production LCD replay. LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/EarlyLawn.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImage.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <set>
#include <stdexcept>
#include <algorithm>
#include "music_title_reuse_engine_probe.cpp"
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" volatile uint32_t pvzh1_camera_lawn_diagnostic[8];
namespace {void Verify(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
extern "C" void pvzh1_level_preview_probe(){
 using namespace Sexy;
 PlayerInfo profile;profile.mName="LEVEL_PREVIEW_READER";profile.mLevel=6;
 auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 H1ActorPoseTestEnable(false);H1EarlyLawnTestEnable(false);
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 fprintf(stderr,"PREVIEW_STAGE board ready\n");
 auto*b=gLawnApp->mBoard;auto*z=b->AddZombieInRow(ZOMBIE_POLEVAULTER,2,-2);
 auto*r=gLawnApp->ReanimationGet(z->mBodyReanimID);
 fprintf(stderr,"PREVIEW_STAGE actor ready\n");
 for(int t=0;t<r->mDefinition->mTracks.count;++t)for(int f=0;f<r->mDefinition->mTracks.tracks[t].mTransforms.count;++f){auto* image=r->mDefinition->mTracks.tracks[t].mTransforms.mTransforms[f].mImage;if(uintptr_t(image)>1000)if(auto* memory=dynamic_cast<Sexy::MemoryImage*>(image))memory->GetBits();}
 H1ActorPoseTestEnable(true);Verify(H1PlantPosePrepare(REANIM_POLEVAULTER),"pole bank load");
 fprintf(stderr,"PREVIEW_STAGE bank ready\n");
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();auto*pixels=target->GetBits();
 h1DirtyCanvas.enabled=false;H1RenderPlanTestEnable(false);
 r->mOverlayMatrix.m02=120*1060.f/480;r->mOverlayMatrix.m12=136*600.f/272;
 std::set<int> frames;
 for(const char*name:{"anim_idle","anim_idle2"})if(r->TrackExists(name)){int start,count;r->GetFramesForLayer(name,start,count);for(int i=start;i<start+count;++i)frames.insert(i);}
 unsigned comparisons=0;
 for(int frame:frames)for(unsigned half=0;half<2;++half){
  r->mFrameStart=frame;r->mFrameCount=2;r->mAnimTime=half*.5f;
  for(unsigned selection=0;selection<2;++selection){
   for(int i=0;i<r->mDefinition->mTracks.count;++i){r->mTrackInstances[i].mRenderGroup=selection&&i%3==0?1:0;r->mTrackInstances[i].mBlendCounter=0;}
   for(unsigned bg=0;bg<3;++bg){std::vector<uint32_t> expected;
    for(bool replay:{false,true}){H1ActorPoseTestEnable(replay);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();
     for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);
     if(comparisons==0)fprintf(stderr,"PREVIEW_STAGE draw replay=%d frame=%d half=%u\n",int(replay),frame,half);
     Graphics g(screen);auto hits=pvzh1_actor_pose_diagnostic[5];r->DrawRenderGroup(&g,0);
     if(!replay)expected.assign(pixels,pixels+480*272);
     else {Verify(pvzh1_actor_pose_diagnostic[5]==hits+1,"pole used live renderer");
      Verify(expected==std::vector<uint32_t>(pixels,pixels+480*272),"pole replay pixels");++comparisons;}
    }
   }
  }
 }
 H1EarlyLawnTestEnable(true);gLawnApp->mGameScene=SCENE_LEVEL_INTRO;b->mSodPosition=1000;b->mY=0;
 Verify(H1EarlyLawnPrepare(b),"day panorama preload");h1DirtyCanvas.enabled=true;
 std::ifstream input(GetResourcePath("camera-lawn-day.h1cp"),std::ios::binary);input.seekg(64);
 std::vector<uint32_t> reference(634*272);input.read((char*)reference.data(),reference.size()*4);Verify(bool(input),"panorama reference");unsigned pans=0;
 for(int x=-120;x<=220;++x){if(x==130)continue;b->mX=x;h1DirtyCanvas.Begin(target);Graphics g(screen);g.Translate(x,0);Verify(H1EarlyLawnDraw(b,&g),"day pan used live renderer");
  unsigned offset=std::clamp(((220-x)*480+530)/1060,0,154);
  for(unsigned y=0;y<272;++y)Verify(std::equal(pixels+y*480,pixels+(y+1)*480,reference.data()+y*634+offset),"day panorama pixels");++pans;
 }
 gLawnApp->mGameScene=SCENE_PLAYING;b->mX=130;h1DirtyCanvas.Begin(target);Graphics g(screen);g.Translate(130,0);Verify(H1EarlyLawnDraw(b,&g)&&!pvzh1_camera_lawn_diagnostic[0],"panorama not retired");
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 pvzh1_music_title_reuse_probe();
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"level-preview-replay-verification.json");
 out<<"{\"passed\":true,\"zombie_whole_frame_comparisons\":"<<comparisons<<",\"camera_whole_frame_comparisons\":"<<pans<<",\"panorama_released_before_play\":true,\"all_idle_half_frames_and_visibility_selections\":true}\n";
}
