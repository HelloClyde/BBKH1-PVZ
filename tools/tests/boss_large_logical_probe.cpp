/* Exercise the real Boss draw wrapper, status colors and original options. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/MemoryImageCache.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_large_logical_request_diagnostic[8];
extern "C" char pvzh1_large_logical_request_path[128];
namespace {
void Check(unsigned before,unsigned frame,int state,int group){
 if(pvzh1_large_logical_request_diagnostic[0]==before)return;
 fprintf(stderr,"LARGE_LOGICAL frame=%u state=%d group=%d count=%u path=%s caller=%08x%08x size=%ux%u flags=%u\n",
  frame,state,group,pvzh1_large_logical_request_diagnostic[0]-before,pvzh1_large_logical_request_path,
  pvzh1_large_logical_request_diagnostic[7],pvzh1_large_logical_request_diagnostic[1],
  pvzh1_large_logical_request_diagnostic[2],pvzh1_large_logical_request_diagnostic[3],pvzh1_large_logical_request_diagnostic[4]);
 throw std::runtime_error("Real Boss wrapper expanded a large logical image");
}
}
extern "C" void pvzh1_boss_large_logical_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mName="BOSS_LARGE";profile.mLevel=50;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* z=gLawnApp->mBoard->GetBossZombie();
 if(!z)z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_BOSS,0,-2);
 auto* r=gLawnApp->ReanimationGet(z->mBodyReanimID);
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();
 const unsigned before=pvzh1_large_logical_request_diagnostic[0];unsigned draws=0;
 r->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;
 const unsigned frames=r->mDefinition->mTracks.tracks[0].mTransforms.count;
 for(unsigned frame=0;frame+1<frames;frame+=13)for(int state=0;state<3;++state){
  r->mFrameStart=frame;r->mFrameCount=2;r->mAnimTime=.5f;
  z->mBodyHealth=state==1?1:z->mBodyMaxHealth;z->mChilledCounter=state==2?100:0;
  for(auto part:{BossPart::BOSS_PART_BACK_LEG,BossPart::BOSS_PART_FRONT_LEG,BossPart::BOSS_PART_MAIN,BossPart::BOSS_PART_BACK_ARM}){
   h1DirtyCanvas.Begin(target);Graphics g(screen);g.Translate(gLawnApp->mBoard->mX,0);
   z->DrawBossPart(&g,part);Check(before,frame,state,int(part));H1ImageCacheEndFrame();++draws;
  }
 }
 gLawnApp->mGameScene=SCENE_PLAYING;gLawnApp->mBoard->Pause(true);
 gLawnApp->DoNewOptions(false);Check(before,frames,0,100);
 gLawnApp->KillNewOptionsDialog();Check(before,frames,0,101);
 gLawnApp->DoBackToMain();Check(before,frames,0,102);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-large-logical-verification.json");
 out<<"{\"passed\":true,\"real_boss_part_draws\":"<<draws<<",\"status_colors\":3,\"original_options_open_close\":true,\"large_logical_requests\":0}\n";
 gLawnApp->mPlayerInfo=old;
}
