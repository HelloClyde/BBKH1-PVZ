/* Opaque camera viewport and retained restore oracle; LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "Resources.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/EarlyLawn.h"
#include "graphics/DirtyCanvas.h"
#include "Common.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_camera_lawn_diagnostic[8];
namespace {void Check(bool x,const char*s){if(!x)throw std::runtime_error(s);}}
extern "C" void pvzh1_camera_lawn_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mName="CAMERA_TEST";profile.mLevel=2;auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto*b=gLawnApp->mBoard;b->mY=0;gLawnApp->mGameScene=SCENE_LEVEL_INTRO;
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();auto*bits=target->GetBits();std::ifstream f(GetResourcePath("camera-lawns.h1cp"),std::ios::binary);uint32_t h[16];f.read((char*)h,64);std::vector<uint32_t> reference(3*634*272);f.read((char*)reference.data(),reference.size()*4);Check(bool(f),"camera reference read failed");unsigned cases=0;
 for(int level:{1,2,3,4}){b->mLevel=level;b->mSodPosition=0;H1EarlyLawnPrepare(b);Check(pvzh1_camera_lawn_diagnostic[0]>0,"current panorama not resident");unsigned which=level==1?0:level<4?1:2;
  for(int x=-120;x<=220;++x){if(x==130)continue;b->mX=x;unsigned offset=std::clamp(((220-x)*480+530)/1060,0,154);
   for(unsigned frame=0;frame<2;++frame){h1DirtyCanvas.Begin(target);Graphics g(screen);g.Translate(x,0);Check(H1EarlyLawnDraw(b,&g),"eligible camera fell back");
    for(unsigned y=0;y<272;++y)for(unsigned px=0;px<480;++px)Check(bits[y*480+px]==reference[(which*272+y)*634+offset+px],"camera copy or retained restore changed pixels");++cases;
    h1DirtyCanvas.Mark(target,50,60,31,19);for(unsigned y=60;y<79;++y)for(unsigned px=50;px<81;++px)bits[y*480+px]=0xff12ab34;
   }
  }
  b->mSodPosition=1000;b->mX=130;h1DirtyCanvas.Begin(target);Graphics g(screen);g.Translate(130,0);Check(H1EarlyLawnDraw(b,&g),"completed lawn fell back");Check(!pvzh1_camera_lawn_diagnostic[0],"panorama remained resident after sod began");
 }
 b->mSodPosition=0;b->mX=100;H1EarlyLawnPrepare(b);Graphics clipped(screen);clipped.Translate(100,0);clipped.ClipRect(10,10,100,100);Check(!H1EarlyLawnDraw(b,&clipped),"custom clip camera guard failed");gLawnApp->mGameScene=SCENE_PLAYING;Graphics playing(screen);playing.Translate(100,0);Check(!H1EarlyLawnDraw(b,&playing),"playing board used intro camera");
 // Failed optional panoramas leave the original camera draw available.
 const std::string folder=GetResourceFolder();auto bad=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"bad-camera";std::filesystem::create_directory(bad);std::ifstream original(GetResourcePath("camera-lawns.h1cp"),std::ios::binary);std::vector<char> blob((std::istreambuf_iterator<char>(original)),{});
 gLawnApp->mGameScene=SCENE_LEVEL_INTRO;
 for(unsigned kind=0;kind<4;++kind){b->mSodPosition=1000;b->mX=130;h1DirtyCanvas.Begin(target);Graphics staticLawn(screen);staticLawn.Translate(130,0);Check(H1EarlyLawnDraw(b,&staticLawn),"base lawn failed before bad camera");
  auto dir=bad/std::to_string(kind);std::filesystem::create_directory(dir);if(kind){auto corrupt=blob;if(kind==1)corrupt[0]^=1;if(kind==2)corrupt.back()^=1;if(kind==3)corrupt.resize(100);std::ofstream badFile(dir/"camera-lawns.h1cp",std::ios::binary);badFile.write(corrupt.data(),corrupt.size());}
  SetResourceFolder(dir.string());b->mSodPosition=0;b->mX=100;unsigned failures=pvzh1_camera_lawn_diagnostic[3];H1EarlyLawnPrepare(b);Check(!pvzh1_camera_lawn_diagnostic[0]&&pvzh1_camera_lawn_diagnostic[3]==failures+1,"bad panorama accepted or leaked");Graphics fallback(screen);fallback.Translate(100,0);for(unsigned n=0;n<3;++n)Check(!H1EarlyLawnDraw(b,&fallback),"bad camera failed to preserve fallback");Check(pvzh1_camera_lawn_diagnostic[3]==failures+1,"draw retried failed panorama");SetResourceFolder(folder);
 }
 pvzh1_early_lawn_shutdown();Check(!pvzh1_camera_lawn_diagnostic[0],"camera shutdown leaked");gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"camera-lawn-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<cases<<",\"all_341_logical_positions_except_static_130\":true,\"levels\":[1,2,3,4],\"reference\":\"independently decoded pre-rendered panorama at rounded LCD camera offset\",\"freed_when_sod_starts\":true,\"missing_or_malformed_files\":4,\"shutdown_bytes\":0}\n";
}
