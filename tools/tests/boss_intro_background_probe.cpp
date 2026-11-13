/* One static view, independent of Dave's animation. LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "Resources.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/EarlyLawn.h"
#include "graphics/RenderPlanCache.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
extern "C" volatile uint32_t pvzh1_early_lawn_diagnostic[8];
namespace {
void Require(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
void Run(bool exporting){
 using namespace Sexy;H1EarlyLawnTestEnable(false);H1RenderPlanTestEnable(false);h1DirtyCanvas.enabled=false;
 PlayerInfo profile;profile.mName="BOSS_STATIC_BACKGROUND";profile.mLevel=50;auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto*b=gLawnApp->mBoard;b->mX=220;b->mY=0;gLawnApp->mGameScene=SCENE_LEVEL_INTRO;
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();auto*pixels=target->GetBits();
 std::vector<uint32_t> expected;unsigned comparisons=0;
 for(unsigned background=0;background<3;++background){
  H1EarlyLawnTestEnable(false);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+background*0x38124du)&0xffffffu);
  Graphics original(screen);original.Translate(220,0);original.DrawImage(IMAGE_BACKGROUND6BOSS,-BOARD_OFFSET,0);
  std::vector<uint32_t> actual(pixels,pixels+480*272);
  if(background)for(unsigned y=0;y<271;++y)Require(std::equal(expected.data()+y*480,expected.data()+y*480+479,actual.data()+y*480),"Boss intro interior is not opaque");
  // The original rasterizer leaves the final row/column outside the image.
  // The game's ordinary full-frame clear supplies black at those boundaries.
  for(unsigned y=0;y<272;++y)actual[y*480+479]=0xff000000u;
  std::fill(actual.end()-480,actual.end(),0xff000000u);expected=actual;++comparisons;
 }
 auto output=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));
 if(exporting){std::ofstream f(output/"boss-intro.argb",std::ios::binary);f.write((char*)expected.data(),expected.size()*4);}
 else {
  H1EarlyLawnTestEnable(true);Require(H1EarlyLawnPrepare(b),"Boss intro static preload");h1DirtyCanvas.enabled=true;
  for(unsigned frame=0;frame<12;++frame){
   h1DirtyCanvas.Begin(target);Graphics replay(screen);replay.Translate(220,0);Require(H1EarlyLawnDraw(b,&replay),"Boss intro used live background");
   Require(std::equal(expected.begin(),expected.end(),pixels),"Boss intro pixels differ from original renderer");++comparisons;
   // Damage must be restored without caching Dave, text, or other overlays.
   for(unsigned y=40;y<140;++y)for(unsigned x=10;x<120;++x)pixels[y*480+x]=0xffff0000u;
   h1DirtyCanvas.Mark(target,10,40,110,100);
  }
  b->mX=219;Graphics moving(screen);moving.Translate(219,0);Require(!H1EarlyLawnDraw(b,&moving),"Static view consumed a moving camera");
  b->mX=220;Require(H1EarlyLawnPrepare(b,true),"Boss playing background preparation");
  Require(pvzh1_early_lawn_diagnostic[0]<530000,"Intro and playing backgrounds retained together");
  gLawnApp->mGameScene=SCENE_PLAYING;b->mX=130;h1DirtyCanvas.Begin(target);Graphics playing(screen);playing.Translate(130,0);Require(H1EarlyLawnDraw(b,&playing),"Boss playing background lost");
 }
 std::ofstream report(output/(exporting?"boss-intro-export-verification.json":"boss-intro-replay-verification.json"));
 report<<"{\"passed\":true,\"static_background_only\":true,\"dimensions\":[480,272],\"view_x\":220,\"original_renderer_frame_comparisons\":"<<comparisons<<",\"retained_damage_frames\":"<<(exporting?0:12)<<",\"dave_animation_baked\":false}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;H1EarlyLawnTestEnable(true);H1RenderPlanTestEnable(true);h1DirtyCanvas.enabled=true;
}
}
extern "C" void pvzh1_boss_intro_export(){Run(true);}
extern "C" void pvzh1_boss_intro_probe(){Run(false);}
