/* Full original backdrop, including animated overlays and retained damage. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/EarlyLawn.h"
#include "graphics/RenderPlanCache.h"
#include "Common.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
extern "C" volatile uint32_t pvzh1_early_lawn_diagnostic[8];
namespace {void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"REGULAR LAWN: %s\n",why);throw std::runtime_error(why);}}}
extern "C" void pvzh1_regular_lawn_probe(){
 using namespace Sexy;H1RenderPlanTestEnable(false);h1DirtyCanvas.enabled=true;
 PlayerInfo profile;profile.mName="REGULAR_LAWN_READER";auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();
 unsigned comparisons=0,retained=0,replays=0,guards=0;
 for(int level:{6,11,21,31,41,50}){
  profile.mLevel=level;H1EarlyLawnTestEnable(true);gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;board->mX=130;board->mY=0;board->mSodPosition=1000;H1EarlyLawnPrepare(board,true);
  Check(pvzh1_early_lawn_diagnostic[0],"regular background did not preload");
  auto draw=[&](){Graphics g(screen);g.Translate(130,0);board->DrawBackdrop(&g);};
  std::vector<uint32_t> base;
  for(auto scene:{SCENE_LEVEL_INTRO,SCENE_ZOMBIES_WON,SCENE_PLAYING})for(unsigned bg=0;bg<3;++bg){
   gLawnApp->mGameScene=scene;
   H1EarlyLawnTestEnable(false);for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);h1DirtyCanvas.Begin(target);draw();std::vector<uint32_t> expected(pixels,pixels+480*272);
   H1EarlyLawnTestEnable(true);for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+(bg+1)*0x38124du)&0xffffffu);h1DirtyCanvas.Begin(target);auto before=pvzh1_early_lawn_diagnostic[1];draw();
   Check(pvzh1_early_lawn_diagnostic[1]==before+1,"ordinary background did not use offline tiles");Check(expected==std::vector<uint32_t>(pixels,pixels+480*272),"cached backdrop or animated overlay differs from original");++comparisons;++replays;if(scene==SCENE_PLAYING)base=expected;
  }
  for(unsigned frame=0;frame<12;++frame){
   h1DirtyCanvas.Begin(target);draw();const int x=16+frame*17,y=40+frame*8;const uint32_t color=0xff239317u+frame*0x100u;
   {H1CanvasKnownDraw ink(target,x,y,20,11);for(int row=y;row<y+11;++row)for(int col=x;col<x+20;++col)pixels[row*480+col]=color;}
   auto expected=base;for(int row=y;row<y+11;++row)for(int col=x;col<x+20;++col)expected[row*480+col]=color;
   Check(expected==std::vector<uint32_t>(pixels,pixels+480*272),"retained background left a trail or lost unchanged pixels");++retained;
  }
  for(unsigned kind=0;kind<3;++kind){Graphics g(screen);g.Translate(130,0);if(kind==0)g.mLinearBlend=false;if(kind==1)g.mColorizeImages=true;if(kind==2)g.ClipRect(1,1,700,500);Check(!H1EarlyLawnDraw(board,&g),"unsupported background state was replayed");++guards;}
 }
 const auto originalFolder=GetResourceFolder();const auto bad=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"missing-lawn";std::filesystem::create_directory(bad);
 pvzh1_early_lawn_shutdown();SetResourceFolder(bad.string());Check(!H1EarlyLawnPrepare(gLawnApp->mBoard,true),"missing required lawn accepted");Check(!pvzh1_early_lawn_diagnostic[0],"failed lawn retained memory");SetResourceFolder(originalFolder);
 gLawnApp->KillBoard();pvzh1_early_lawn_shutdown();Check(!pvzh1_early_lawn_diagnostic[0],"background shutdown leaked");gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"regular-lawn-reader-verification.json");out<<"{\"passed\":true,\"backgrounds\":6,\"whole_frame_comparisons\":"<<comparisons<<",\"offline_replays\":"<<replays<<",\"retained_damage_frames\":"<<retained<<",\"state_guards\":"<<guards<<",\"pool_water_and_other_overlays_match\":true,\"shutdown_bytes\":0}\n";
}
