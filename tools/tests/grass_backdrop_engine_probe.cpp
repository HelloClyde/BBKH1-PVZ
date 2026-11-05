/* Moving early-level lawns must agree with unretained production draws. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Cutscene.h"
#include "Lawn/System/PlayerInfo.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/DirtyCanvas.h"
#include "misc/ResourceManager.h"
#include "Resources.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
namespace {
struct AppAccess:Sexy::SexyAppBase {using Sexy::SexyAppBase::ProcessSafeDeleteList;};
void Check(bool v){if(!v)throw std::runtime_error("Moving lawn differs from unretained backdrop");}
}
extern "C" void pvzh1_grass_backdrop_probe(){
 using namespace Sexy;
 PlayerInfo profile;profile.mName="LAWN_TEST";profile.mLevel=2;
 auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;Check(board);
 // NewGame loads the replacement before the deferred old Widget destructor.
 // LOW_MEMORY must not let that destructor release the new background group.
 for(unsigned restart=0;restart<3;++restart){
  gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
  (gLawnApp->*&AppAccess::ProcessSafeDeleteList)();
  if(!gLawnApp->mResourceManager->IsGroupLoaded("DelayLoad_Background1")||
     !gLawnApp->mResourceManager->IsGroupLoaded("DelayLoad_BackgroundUnsodded")){
   fprintf(stderr,"REGRESSION: deferred old board unloaded replacement background\n");Check(false);
  }
  Check(dynamic_cast<MemoryImage*>(IMAGE_BACKGROUND1UNSODDED));
  Check(IMAGE_BACKGROUND1UNSODDED->GetWidth()==1400);
 }
 board=gLawnApp->mBoard;Check(board);
 gLawnApp->mGameScene=GameScenes::SCENE_LEVEL_INTRO;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();
 auto draw=[&](){Graphics g(screen);g.Translate(board->mX,0);board->DrawBackdrop(&g);};
 unsigned checks=0;
 for(int level:{1,2,4})for(int camera:{-220,130,350})for(int sod:{0,250,500,750,1000}){
  board->mLevel=level;board->mX=camera;board->mSodPosition=sod;
  H1RenderPlanTestEnable(false);h1DirtyCanvas.enabled=false;h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();draw();
  std::vector<uint32_t> expected(bits,bits+480*272);
  unsigned colored=0;for(auto pixel:expected)colored+=(pixel&0xffffffu)!=0;Check(colored>20000);
  H1RenderPlanTestEnable(true);h1DirtyCanvas.enabled=true;h1DirtyCanvas.keyValid=false;
  for(unsigned frame=0;frame<7;++frame){
   h1DirtyCanvas.Begin(target);draw();Check(expected==std::vector<uint32_t>(bits,bits+480*272));++checks;
   h1DirtyCanvas.Mark(target,80,75,16,8);for(unsigned y=75;y<83;++y)for(unsigned x=80;x<96;++x)bits[y*480+x]=0xff123456;
  }
 }
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"grass-backdrop-verification.json");
 out<<"{\"passed\":true,\"deferred_deletion_restarts\":3,\"replacement_background_lifetime_checked\":true,\"levels\":[1,2,4],\"camera_positions\":3,\"sod_positions\":5,\"whole_frame_comparisons\":"<<checks<<",\"oracle\":\"Original production draw with retention and render-plan caches disabled\"}\n";
 H1RenderPlanTestEnable(true);h1DirtyCanvas.enabled=true;
 gLawnApp->KillSeedChooserScreen();gLawnApp->KillBoard();gLawnApp->mPlayerInfo=previous;
}
