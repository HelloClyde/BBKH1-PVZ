/* Compare complete original actor draws with offline replay at bake-grid
 * positions, including parent/child ordering and clips. LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "Common.h"
#include <zlib.h>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[12];
namespace{void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
extern "C" void pvzh1_actor_retained_probe(){
 PlayerInfo profile;profile.mName="POSE_TEST";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
 board->AddPlant(2,1,SeedType::SEED_PEASHOOTER);board->AddPlant(3,2,SeedType::SEED_SUNFLOWER);board->AddZombieInRow(ZombieType::ZOMBIE_NORMAL,2,0);
 auto* sod=gLawnApp->mEffectSystem->mReanimationHolder->AllocReanimation(0,0,0,ReanimationType::REANIM_SODROLL);
 Check(pvzh1_actor_pose_diagnostic[0]>0,"pose pack failed to load");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();h1DirtyCanvas.enabled=true;

 unsigned comparisons=0,limited=0;
 auto background=[&](unsigned i){return 0xff000000u|((i*0x164b35u+0x38124du)&0xffffffu);};
 for(auto* actor:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
  if(actor->mDead||(actor->mReanimationType!=REANIM_PEASHOOTER&&actor->mReanimationType!=REANIM_SUNFLOWER&&actor->mReanimationType!=REANIM_ZOMBIE))continue;
  h1DirtyCanvas.keyValid=false;
  for(unsigned frame=0;frame<24;++frame){
   actor->mOverlayMatrix.m02=250.f+frame*1060.f/480;actor->mOverlayMatrix.m12=300.f;
   actor->mAnimTime=float(frame*2%(2*(actor->mFrameCount-1)))/(2*(actor->mFrameCount-1));
   auto retained=h1DirtyCanvas;std::vector<uint32_t> previous(bits,bits+480*272);
   h1DirtyCanvas.enabled=false;h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)bits[i]=background(i);
   Sexy::Graphics reference(screen);actor->DrawRenderGroup(&reference,0);std::vector<uint32_t> expected(bits,bits+480*272);
   h1DirtyCanvas=retained;std::copy(previous.begin(),previous.end(),bits);h1DirtyCanvas.Begin(target);
   const uint64_t key[2]={0x52455441494e4544ull,(uintptr_t)actor};Check(h1DirtyCanvas.Root(target,key,sizeof(key)),"root rejected");
   for(unsigned row=0;row<34;++row){auto mask=h1DirtyCanvas.RestoreRow(row*8);if(mask!=H1DirtyCanvas::All)++limited;
    for(unsigned col=0;col<30;++col)if(mask&(1u<<col))for(unsigned y=row*8;y<row*8+8;++y)for(unsigned x=col*16;x<col*16+16;++x)bits[y*480+x]=background(y*480+x);
   }
   Sexy::Graphics actual(screen);actor->DrawRenderGroup(&actual,0);Check(expected==std::vector<uint32_t>(bits,bits+480*272),"retained actor left pixels or erased attachments");++comparisons;
  }
 }
 Check(comparisons>=72&&limited>0,"retained fixture did not exercise moving actors");pvzh1_actor_pose_shutdown();h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"actor-retained-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"limited_tile_rows\":"<<limited<<",\"moving_actors_and_attachments\":true,\"shutdown_bytes\":0}\n";
}
