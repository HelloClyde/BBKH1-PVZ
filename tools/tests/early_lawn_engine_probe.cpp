/* Compare tiled replay and retained restoration to independently decoded
 * opaque composites. LGPL-3.0-or-later. */
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
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_early_lawn_diagnostic[8];
namespace {void Check(bool x,const char* m){if(!x)throw std::runtime_error(m);}}
extern "C" void pvzh1_early_lawn_engine_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mName="LAWN_REPLAY";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;b->mX=130;b->mY=0;Check(pvzh1_early_lawn_diagnostic[0]>0,"lawn pack did not load");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* physical=H1GetPhysicalScreen(screen);auto* target=physical->Target();auto* bits=target->GetBits();
 std::ifstream f(GetResourcePath("early-lawns.h1lb"),std::ios::binary);uint32_t h[16];f.read(reinterpret_cast<char*>(h),64);std::vector<uint16_t> maps(6*1020);f.read(reinterpret_cast<char*>(maps.data()),maps.size()*2);std::vector<uint32_t> data(h[7]*128);f.read(reinterpret_cast<char*>(data.data()),data.size()*4);Check(bool(f),"reference read failed");
 auto pixel=[&](unsigned frame,unsigned x,unsigned y){unsigned tile=maps[frame*1020+(y/8)*30+x/16];return data[tile*128+(y%8)*16+x%16];};
 unsigned comparisons=0;
 for(int level:{1,2,3,4})for(int sod:{0,10,125,250,375,500,625,750,875,990,1000}){
  b->mLevel=level;b->mSodPosition=sod;H1EarlyLawnPrepare(b);unsigned which=level==1?0:level<4?1:2;
  Check(pvzh1_early_lawn_diagnostic[0]<data.size()*4,"unrelated lawn pages remained resident");
  unsigned width=level==1?IMAGE_SOD1ROW->GetWidth():level<4?IMAGE_SOD3ROW->GetWidth():773;
  int logical=130+(level==1?239:level<4?235:232)-BOARD_OFFSET;auto rect=physical->MapRect(Rect(logical,0,(sod*width+500)/1000,600));int edge=rect.mX+rect.mWidth;
  std::vector<uint32_t> expected(480*272);for(unsigned y=0;y<272;++y)for(unsigned x=0;x<480;++x)expected[y*480+x]=pixel(which*2+(int(x)<edge),x,y);
  for(int frame=0;frame<4;++frame){h1DirtyCanvas.Begin(target);Graphics g(screen);g.Translate(130,0);Check(H1EarlyLawnDraw(b,&g),"eligible lawn fell back");Check(expected==std::vector<uint32_t>(bits,bits+480*272),"lawn replay or damage restoration differs");++comparisons;
   h1DirtyCanvas.Mark(target,100,75,16,8);for(int y=75;y<83;++y)for(int x=100;x<116;++x)bits[y*480+x]=0xff123456;
  }
 }
 Graphics clipped(screen);clipped.Translate(130,0);clipped.ClipRect(100,100,100,100);Check(!H1EarlyLawnDraw(b,&clipped),"custom clip guard failed");b->mX=350;Graphics camera(screen);camera.Translate(350,0);Check(!H1EarlyLawnDraw(b,&camera),"camera guard failed");
 pvzh1_early_lawn_shutdown();Check(!pvzh1_early_lawn_diagnostic[0],"lawn shutdown leaked");gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"early-lawn-engine-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"reference\":\"independently decoded original endpoint composites with fixed pixel reveal\",\"camera_and_custom_clip_guards\":true,\"shutdown_bytes\":0}\n";
}
