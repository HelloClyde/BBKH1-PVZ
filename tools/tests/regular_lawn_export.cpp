/* Ordinary background pixels only; animated overlays stay in Board::DrawBackdrop. */
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
#include <cstdio>
extern "C" void pvzh1_regular_lawn_export(){
 using namespace Sexy;H1EarlyLawnTestEnable(false);H1RenderPlanTestEnable(false);h1DirtyCanvas.enabled=false;
 PlayerInfo profile;profile.mName="REGULAR_LAWN_COMPILER";auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();
 const int levels[]={6,11,21,31,41,50};unsigned comparisons=0;
 for(unsigned index=0;index<6;++index){
  profile.mLevel=levels[index];gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
  Image* images[]={IMAGE_BACKGROUND1,IMAGE_BACKGROUND2,IMAGE_BACKGROUND3,IMAGE_BACKGROUND4,IMAGE_BACKGROUND5,IMAGE_BACKGROUND6BOSS};auto* image=images[index];if(!image)throw std::runtime_error("background resource missing");
  std::vector<uint32_t> expected;
  for(unsigned bg=0;bg<3;++bg){for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);h1DirtyCanvas.Begin(target);
   Graphics g(screen);g.Translate(130,0);g.DrawImage(image,-BOARD_OFFSET,0);std::vector<uint32_t> actual(pixels,pixels+480*272);
   if(!bg)expected=actual;else if(actual!=expected){unsigned first=0,n=0;for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){if(!n)first=i;++n;}std::fprintf(stderr,"BG%u image=%d,%d differing=%u first=%u xy=%u,%u expected=%08x actual=%08x\n",index+1,image->mWidth,image->mHeight,n,first,first%480,first/480,expected[first],actual[first]);throw std::runtime_error("background is not an opaque full-screen foundation");}++comparisons;
  }
  std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/("background-"+std::to_string(index+1)+".argb"),std::ios::binary);out.write((char*)expected.data(),expected.size()*4);
 }
 std::ofstream proof(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"regular-lawn-export-verification.json");proof<<"{\"passed\":true,\"backgrounds\":6,\"opaque_full_frame_comparisons\":"<<comparisons<<",\"pool_water_and_other_overlays_baked\":false}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;h1DirtyCanvas.enabled=true;
}
