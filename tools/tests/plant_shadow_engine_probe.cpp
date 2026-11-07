/* Compare installed shadow spans with the original public sprite path. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/PvzpCommon.h"
#include "Resources.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
extern "C" volatile uint32_t pvzh1_shadow_pose_diagnostic[4];
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
namespace {void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"SHADOW CHECK: %s\n",why);throw std::runtime_error(why);}}}
extern "C" void pvzh1_plant_shadow_probe(){
 using namespace Sexy;H1RenderPlanTestEnable(false);h1DirtyCanvas.enabled=false;
 PlayerInfo profile;profile.mName="SHADOW_READER";auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();
 auto fill=[&](unsigned bg){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};unsigned comparisons=0,replays=0;
 for(int level:{1,11,21,31,41}){
  profile.mLevel=level;H1ActorPoseTestEnable(true);gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);H1ActorPosePrepare();auto* board=gLawnApp->mBoard;
  Check(pvzh1_shadow_pose_diagnostic[1],"shadow bundle was not preloaded");
  for(int row=0;row<6;++row)for(int col=0;col<9;++col)for(unsigned night=0;night<2;++night)for(unsigned bg=0;bg<3;++bg){
   auto* image=night?IMAGE_PLANTSHADOW2:IMAGE_PLANTSHADOW;Graphics g(screen);g.Translate(130.f+board->GridToPixelX(col,row),board->GridToPixelY(col,row));
   H1ActorPoseTestEnable(false);fill(bg);std::vector<uint32_t> background(pixels,pixels+480*272);PvzpDrawImageCelCenterScaledF(&g,image,-3,51,0,1,1);std::vector<uint32_t> expected(pixels,pixels+480*272);
   H1ActorPoseTestEnable(true);fill(bg);auto before=pvzh1_shadow_pose_diagnostic[2];PvzpDrawImageCelCenterScaledF(&g,image,-3,51,0,1,1);
   Check(expected==std::vector<uint32_t>(pixels,pixels+480*272),"shadow replay changed original frame pixels");
   if(expected!=background){if(pvzh1_shadow_pose_diagnostic[2]!=before+1)std::fprintf(stderr,"level=%d col=%d row=%d xy=%g,%g linear=%d clip=%d,%d,%d,%d owner=%d misses=%u\n",level,col,row,g.mTransX,g.mTransY,g.mLinearBlend,g.mClipRect.mX,g.mClipRect.mY,g.mClipRect.mWidth,g.mClipRect.mHeight,h1DirtyCanvas.owner==target,pvzh1_shadow_pose_diagnostic[3]);Check(pvzh1_shadow_pose_diagnostic[2]==before+1,"ordinary visible shadow did not use offline spans");}
   replays+=pvzh1_shadow_pose_diagnostic[2]-before;++comparisons;
  }
 }
 unsigned guards=0;
 for(unsigned kind=0;kind<6;++kind){Graphics g(screen);g.Translate(170,80);float x=-3,y=51,sx=1;
  if(kind==0)g.mLinearBlend=false;if(kind==1)g.mScaleX=.5f;if(kind==2)g.mColorizeImages=true;if(kind==3)g.ClipRect(1,1,700,500);if(kind==4)sx=1.3f;if(kind==5)x=-7;
  Check(!H1PlantShadowDraw(&g,IMAGE_PLANTSHADOW,x,y,0,sx,1),"unsupported shadow geometry or Graphics state was replayed");++guards;
 }
 gLawnApp->KillBoard();pvzh1_actor_pose_shutdown();Check(!pvzh1_shadow_pose_diagnostic[1]&&!pvzh1_actor_pose_diagnostic[0],"shadow shutdown retained memory");gLawnApp->mPlayerInfo=old;h1DirtyCanvas.enabled=true;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"plant-shadow-reader-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"offline_replays\":"<<replays<<",\"state_guards\":"<<guards<<",\"shutdown_bytes\":0}\n";
}
