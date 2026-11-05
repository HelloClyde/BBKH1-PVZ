/* Offline opaque panorama at the LCD sampling density; LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/EarlyLawn.h"
#include "graphics/MemoryImageCache.h"
#include "Resources.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
extern "C" void pvzh1_camera_lawn_export(){
 using namespace Sexy;H1EarlyLawnTestEnable(false);H1RenderPlanTestEnable(false);h1DirtyCanvas.enabled=false;
 PlayerInfo profile;profile.mName="PANORAMA_COMPILER";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto*b=gLawnApp->mBoard;b->mX=220;b->mY=0;b->mSodPosition=0;b->mTutorialState=TutorialState::TUTORIAL_OFF;gLawnApp->mGameScene=SCENE_LEVEL_INTRO;
 auto*device=gLawnApp->mGLInterface.get();int oldWidth=device->mWidth;device->mWidth=1400;auto saved=std::move(device->mScreenImage);device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,1400,600,634,272);
 auto*screen=device->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();auto*pixels=target->GetBits();unsigned which=0;
 for(int level:{1,2,4}){b->mLevel=level;std::fill(pixels,pixels+634*272,0x01234567u);Graphics g(screen);g.Translate(220,0);b->DrawBackdrop(&g);
  const uint32_t* source=nullptr;const uint32_t*const* pages=nullptr;uint32_t sw=0,sh=0;auto* background=dynamic_cast<MemoryImage*>(IMAGE_BACKGROUND1UNSODDED);
  if(!(H1ImageCachePrepareStorageRGBPages(background,&pages,&sw,&sh)||H1ImageCachePrepareStorageTexture(background,&source,&sw,&sh))||sw!=630||sh!=270)throw std::runtime_error("Unexpected background storage grid");
  unsigned edge=0;for(unsigned i=0;i<634*272;++i){if(pixels[i]==0x01234567u){if(i%634!=633&&i/634!=271)throw std::runtime_error("Uncovered panorama interior");unsigned sample=std::min(sh-1,unsigned((uint64_t(i/634)*2+1)*sh/(272*2)))*sw+std::min(sw-1,unsigned((uint64_t(i%634)*2+1)*sw/(634*2)));pixels[i]=pages?pages[sample>>10][sample&1023]:source[sample];++edge;}pixels[i]|=0xff000000u;}
  if(edge!=905)throw std::runtime_error("Changed raster boundary coverage");

  std::ofstream f(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/("camera-lawn-"+std::to_string(which++)+".argb"),std::ios::binary);f.write(reinterpret_cast<char*>(pixels),634*272*4);
 }
 device->mScreenImage=std::move(saved);device->mWidth=oldWidth;H1EarlyLawnTestEnable(true);H1RenderPlanTestEnable(true);h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"camera-lawn-export-verification.json");out<<"{\"passed\":true,\"panoramas\":3,\"dimensions\":[634,272],\"opaque\":true,\"opaque_background_edge_texels\":905,\"renderer\":\"Original DrawBackdrop at 634/1400 and 272/600 sampling density; camera viewport translation snaps to the LCD grid\"}\n";
}
