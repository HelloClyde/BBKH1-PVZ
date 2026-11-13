/* Follow the real Boss intro dialogue, updates and drawing into gameplay. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CutScene.h"
#include "Lawn/System/PlayerInfo.h"
#include "widget/WidgetManager.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/GLInterface.h"
#include "graphics/DirtyCanvas.h"
#include <exception>
#include <stdexcept>
#include <filesystem>
#include <fstream>
extern "C" void pvzh1_boss_progression_probe(){
 PlayerInfo profile;profile.mName="BOSS_PROGRESS";profile.mLevel=50;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 unsigned draws=0,clicks=0,tick=0;
 try{
  auto* target=Sexy::H1GetPhysicalScreen(gLawnApp->mGLInterface->GetScreenImage())->Target();
  for(;tick<6000;++tick){
   gLawnApp->UpdateFrames();
   if(tick%100==50&&gLawnApp->mBoard->mCutScene->IsShowingCrazyDave()){
    gLawnApp->mBoard->mCutScene->MouseDown(430,167);++clicks;
   }
   if(tick%5==0){h1DirtyCanvas.Begin(target);gLawnApp->mWidgetManager->DrawScreen();++draws;}
   if(gLawnApp->mGameScene==SCENE_PLAYING&&gLawnApp->mBoard->mMainCounter>30)break;
  }
  if(gLawnApp->mGameScene!=SCENE_PLAYING)throw std::runtime_error("Boss intro did not enter gameplay");
 }catch(const std::exception& e){
  fprintf(stderr,"BOSS_PROGRESSION_FAILURE tick=%u draws=%u clicks=%u scene=%d cutscene=%d dave=%d what=%s\n",tick,draws,clicks,int(gLawnApp->mGameScene),gLawnApp->mBoard->mCutScene->mCutsceneTime,gLawnApp->mCrazyDaveMessageIndex,e.what());throw;
 }
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-progression-verification.json");
 out<<"{\"passed\":true,\"logic_updates\":"<<tick<<",\"draws\":"<<draws<<",\"dialogue_clicks\":"<<clicks<<"}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
