/* Exercise source actors and real credits factories without generic banks. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/Music.h"
#include "Lawn/Widget/CreditScreen.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "actors/ActorBakePolicy.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/MemoryImageCache.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
extern "C" volatile uint32_t pvzh1_extra_actor_diagnostic[8];
extern "C" void pvzp_music_mix(int32_t*,unsigned);
namespace {void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
extern "C" void pvzh1_selective_actor_probe(){
 using namespace Sexy;
 Check(!std::filesystem::exists(GetResourcePath("extra-actors")),"generic animation banks are still shipped");
 PlayerInfo profile;profile.mName="SELECTIVE_ACTOR_TEST";profile.mLevel=6;profile.mFinishedAdventure=1;
 auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();unsigned cases=0;
 for(auto type:{REANIM_BOSS,REANIM_CRAZY_DAVE,REANIM_LAWNMOWER,REANIM_READYSETPLANT,REANIM_FLAG,REANIM_TREEOFWISDOM}){
  Check(!H1UseExtraActorBank(type),"unmeasured actor requires a bank");Check(H1ExtraPosePrepare(type),"live actor preparation failed");
  auto*r=gLawnApp->AddReanimation(120,100,0,type);
  for(float time:{0.f,.25f,.75f}){r->mAnimTime=time;h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();Graphics g(screen);
   Check(!H1ActorPoseDraw(r,&g,0),"unmeasured actor selected offline renderer");r->DrawRenderGroup(&g,0);++cases;
  }r->ReanimationDie();H1ImageCacheEndFrame();
 }
 gLawnApp->KillBoard();gLawnApp->ShowCreditScreen();auto*c=gLawnApp->mCreditScreen.get();
 {Graphics first(screen);c->Draw(&first);}c->Update();
 for(int phase=0;phase<3;++phase)for(float frame:{0.f,124.f,200.f}){
  c->JumpToFrame(CreditsPhase(phase),frame);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();Graphics g(screen);c->Draw(&g);++cases;H1ImageCacheEndFrame();
 }
 auto*music=gLawnApp->mMusic->GetMusicHandle(MUSIC_FILE_CREDITS_ZOMBIES_ON_YOUR_LAWN);
 Check(Mix_ModMusicStreamJumpToOrder(music,16)==-1,"PCM accepted a tracker order");
 Mix_VolumeMusic(128);Mix_VolumeMusicStream(music,128);
 Check(!pvzh1_music_seek_pcm_ms(music,1000),"PCM timed seek failed");
 std::ifstream pcm(GetResourcePath("sounds/ZombiesOnYourLawn.h1pcm"),std::ios::binary);pcm.seekg(16+16000*2);
 int16_t expected[64];pcm.read(reinterpret_cast<char*>(expected),sizeof(expected));Check(bool(pcm),"PCM reference unavailable");
 int32_t actual[64]{};pvzp_music_mix(actual,64);for(unsigned i=0;i<64;++i)Check(actual[i]==expected[i],"PCM seek samples differ");
 Check(!pvzh1_music_seek_pcm_ms(music,UINT32_MAX)&&!Mix_PlayingMusicStream(music),"PCM end seek was not clamped");
 Check(!pvzh1_extra_actor_diagnostic[0]&&!pvzh1_extra_actor_diagnostic[2],"generic banks were opened or failed");
 gLawnApp->KillCreditScreen();pvzh1_actor_pose_shutdown();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"selective-actor-verification.json");
 out<<"{\"passed\":true,\"source_actor_draw_cases\":"<<cases-9<<",\"actual_credits_factory_draw_cases\":9,\"generic_bank_opens\":0,\"generic_bank_failures\":0,\"pcm_seek_sample_comparisons\":64,\"pcm_end_seek_clamped\":true,\"hardware_timing\":false}\n";
}
