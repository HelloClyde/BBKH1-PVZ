/* Full engine oracle; controlled poses are renderer tests, not gameplay proof. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Coin.h"
#include "Lawn/System/PlayerInfo.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/TextReplay.h"
#include "graphics/RenderPlanCache.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
using namespace Sexy;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
extern "C" void pvzh1_dirty_canvas_probe(){
 PlayerInfo player;player.mName="H1test";player.mId=0;player.mLevel=1;
 auto* oldPlayer=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&player;
 gLawnApp->MakeNewBoard();Check(gLawnApp->mBoard->LoadGame(getenv("PVZH1_TEST_BOARD_SAVE")),"real first-level save failed to load");
 auto* board=gLawnApp->mBoard;gLawnApp->mGameScene=GameScenes::SCENE_PLAYING;board->mPaused=false;
 auto* device=gLawnApp->mGLInterface.get();auto saved=std::move(device->mScreenImage);
 std::vector<std::vector<uint32_t>> expected;
 unsigned cases=0,limited=0;uint32_t roots=0,skipped=0,written=0;
 for(bool enabled:{false,true}){
  H1TextReplayTestEnable(enabled);
  h1DirtyCanvas.enabled=enabled;
  device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,H1LogicalWidth,H1LogicalHeight,H1ScreenWidth,H1ScreenHeight);
  auto* screen=device->GetScreenImage();auto* target=H1ScreenTarget(screen);
  for(unsigned frame=0;frame<64;++frame){
   board->mMainCounter=frame*7;
   board->mSodPosition=frame==32?500:1000;
   for(Coin* coin:board->mCoins)if(coin->IsSun()){coin->mPosX=100.f+(frame*23)%600;coin->mPosY=50.f+(frame*31)%350;}
   Graphics g(screen);g.Translate(board->mX,board->mY);
   if(frame==12||frame==13)g.SetClipRect(3,5,794,588);
   if(frame==15)H1FillRect(target,Rect(5,7,31,29),Color(219,37,111,127),0);
   if(frame!=17)board->Draw(&g);
   if(frame==23)target->GetBits()[13]=0xff7935b1u; // untracked raw access forces all tiles dirty
   {
    H1CanvasProtectedPixels read(target);auto* pixels=target->GetBits();
    std::vector<uint32_t> actual(pixels,pixels+H1ScreenWidth*H1ScreenHeight);
    if(!enabled)expected.push_back(std::move(actual));
    else {if(actual!=expected[frame]){fprintf(stderr,"DIRTY CANVAS mismatch frame=%u roots=%u limited=%u\n",frame,h1DirtyCanvas.roots,h1DirtyCanvas.limitedRoots);throw std::runtime_error("full frame differs from original clear/draw sequence");}++cases;}
    if(enabled)if(const uint32_t* mask=h1DirtyCanvas.Presentation(pixels)){
     ++limited;
     if(frame)for(unsigned i=0;i<H1ScreenWidth*H1ScreenHeight;++i)if((expected[frame][i]^expected[frame-1][i])&0xffffffu)
      Check(mask[(i/H1ScreenWidth)/H1TileHeight]&(uint32_t(1)<<((i%H1ScreenWidth)/H1TileWidth)),"LCD damage mask omitted a changed pixel");
    }
   }
   h1DirtyCanvas.Begin(target);target->BitsChanged();
  }
  if(enabled){roots=h1DirtyCanvas.roots;skipped=h1DirtyCanvas.backgroundSkipped;written=h1DirtyCanvas.backgroundWritten;}
  device->mScreenImage.reset();Check(h1DirtyCanvas.owner==nullptr,"destroyed screen retained a canvas owner");
 }
 fprintf(stderr,"DIRTY COUNTERS roots=%u limited=%u skipped=%u written=%u\n",roots,limited,skipped,written);
 extern volatile uint32_t unused;
 for(unsigned i=0;i<24;++i)fprintf(stderr,"PLAN[%u]=%u ",i,pvzh1_render_plan_diagnostic[i]);fprintf(stderr,"\n");
 Check(roots>20&&limited>20&&skipped>100000,"fixture never exercised retained background and limited presentation");
 device->mScreenImage=std::move(saved);h1DirtyCanvas.enabled=true;gLawnApp->mPlayerInfo=oldPlayer;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"dirty-canvas-verification.json");
 out<<"{\"passed\":true,\"whole_frame_cases\":"<<cases<<",\"roots\":"<<roots<<",\"limited_presentations\":"<<limited<<",\"background_pixels_skipped\":"<<skipped<<",\"background_pixels_written\":"<<written<<",\"moving_sun\":true,\"changing_text_alpha\":true,\"clip_changes\":true,\"no_draw_frame\":true,\"raw_access\":true,\"released_owner\":true}\n";
 fprintf(stderr,"DIRTY CANVAS PASS %u full frames, %u limited, %u background pixels skipped\n",cases,limited,skipped);
}
