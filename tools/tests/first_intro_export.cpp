/* Bake the non-interactive original prologue at final LCD resolution. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CutScene.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "actors/ActorPose.h"
#include "video/IntroMovie.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "Common.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstdio>

extern "C" void pvzh1_first_intro_export(){
 using namespace Sexy;
 H1IntroMovieTestEnable(false);H1ActorPoseTestEnable(false);h1DirtyCanvas.enabled=false;
 PlayerInfo profile;profile.mName="INTRO_COMPILER";profile.mId=1;profile.mLevel=1;
 auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;SRand(20261107);
 gLawnApp->PreNewGame(GAMEMODE_INTRO,false);auto* board=gLawnApp->mBoard;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* physical=H1GetPhysicalScreen(screen);
 if(!physical||!physical->Active())throw std::runtime_error("Intro exporter requires the physical widescreen renderer");
 auto* target=physical->Target();auto* pixels=target->GetBits();
 std::ofstream frames(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"first-intro.argb",std::ios::binary);
 unsigned count=0,updates=0;int previousTime=-1;
 for(int requested=0;requested<13890;requested+=40){
  while(board->mCutScene->mCutsceneTime<requested){
   if(++updates>1600)throw std::runtime_error("Intro clock did not advance");
   gLawnApp->UpdateFrames();
   if(gLawnApp->mBoard!=board||gLawnApp->mGameMode!=GAMEMODE_INTRO)throw std::runtime_error("Intro ended before its original boundary");
  }
  if(board->mCutScene->mCutsceneTime!=requested||requested<=previousTime)throw std::runtime_error("Unexpected intro timestamp");
  h1DirtyCanvas.RequireClear();std::fill(pixels,pixels+480*272,0xff000000u);
  Graphics g(screen);g.Translate(board->mX,board->mY);board->Draw(&g);
  frames.write(reinterpret_cast<const char*>(pixels),480*272*4);if(!frames)throw std::runtime_error("Intro frame output failed");
  previousTime=requested;++count;
  if(count%50==0)std::fprintf(stderr,"INTRO EXPORT %u frames, %d ms\n",count,requested);
 }
 frames.close();std::ofstream proof(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"first-intro-export.json");
 proof<<"{\"passed\":true,\"width\":480,\"height\":272,\"frames\":"<<count<<",\"step_ms\":40,\"duration_ms\":13890,\"last_time_ms\":"<<previousTime<<",\"original_updates\":"<<updates<<",\"seed\":20261107,\"original_game_callbacks\":true,\"original_renderer\":true}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=previous;h1DirtyCanvas.enabled=true;H1ActorPoseTestEnable(true);
}
