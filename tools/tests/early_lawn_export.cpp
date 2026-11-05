/* Offline full lawn composites and reveal equivalence. LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "Resources.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/EarlyLawn.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
extern "C" void pvzh1_early_lawn_export(){
 using namespace Sexy;H1EarlyLawnTestEnable(false);PlayerInfo profile;profile.mName="LAWN_COMPILER";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;b->mX=130;b->mY=0;b->mTutorialState=TutorialState::TUTORIAL_OFF;gLawnApp->mGameScene=SCENE_LEVEL_INTRO;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* physical=H1GetPhysicalScreen(screen);auto* target=physical->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;H1RenderPlanTestEnable(false);
 auto draw=[&](int level,int sod){b->mLevel=level;b->mSodPosition=sod;h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();Graphics g(screen);g.Translate(130,0);b->DrawBackdrop(&g);return std::vector<uint32_t>(pixels,pixels+480*272);};
 std::ofstream report(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"early-lawn-export-verification.json");report<<"{\"cases\":[";bool first=true;unsigned checks=0;uint64_t differences=0;
 std::vector<std::vector<uint32_t>> frames;
 for(int level:{1,2,4}){
  auto before=draw(level,0),after=draw(level,1000);frames.push_back(before);frames.push_back(after);
  for(int sod=0;sod<=1000;sod+=10){auto expected=draw(level,sod);
   const int sourceWidth=level==1?IMAGE_SOD1ROW->GetWidth():level==2?IMAGE_SOD3ROW->GetWidth():773;
   const int width=PvzpAnimateCurve(0,1000,sod,0,sourceWidth,PvzpCurves::CURVE_LINEAR);
   auto boundary=physical->MapRect(Rect(130+(level==1?239:level==2?235:232)-BOARD_OFFSET,0,width,600));int right=boundary.mX+boundary.mWidth;unsigned different=0;
   for(int y=0;y<272;++y)for(int x=0;x<480;++x)different+=expected[y*480+x]!=(x<right?after:before)[y*480+x];
   if(level==2&&sod==500){for(int variant=0;variant<2;++variant){auto image=expected;if(variant)for(int y=0;y<272;++y)for(int x=0;x<480;++x)image[y*480+x]=(x<right?after:before)[y*480+x];std::ofstream f(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/(variant?"fixed-middle.argb":"original-middle.argb"),std::ios::binary);f.write(reinterpret_cast<const char*>(image.data()),image.size()*4);}}
   if(!first)report<<",";first=false;report<<"["<<level<<","<<sod<<","<<right<<","<<different<<"]";++checks;differences+=different;
  }
 }
 for(unsigned i=0;i<frames.size();++i){std::ofstream f(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/("lawn-"+std::to_string(i)+".argb"),std::ios::binary);f.write(reinterpret_cast<const char*>(frames[i].data()),480*272*4);}
 report<<"],\"whole_frame_comparisons\":"<<checks<<",\"different_pixels\":"<<differences<<",\"physical_dimensions\":[480,272],\"scope\":\"Endpoints are original full composites. Intermediate original UV sampling varies with crop width; offline reveal intentionally fixes those samples.\"}\n";
 H1RenderPlanTestEnable(true);H1EarlyLawnTestEnable(true);h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
