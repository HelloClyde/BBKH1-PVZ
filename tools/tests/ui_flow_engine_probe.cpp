/* Full original clock with baked display; verify natural first-level handoff. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CutScene.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/ProfileMgr.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "video/IntroMovie.h"
#include "Common.h"
#include "OptionsFeedback.h"
#include "Lawn/Widget/NewUserDialog.h"
#include "Lawn/Widget/NewOptionsDialog.h"
#include "widget/Slider.h"
#include "widget/Checkbox.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_intro_movie_diagnostic[16];
extern "C" void pvzh1_ui_flow_probe(){
 using namespace Sexy;
 auto check=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
 check(gLawnApp->GetMusicVolume()==.1&&gLawnApp->GetSfxVolume()==.1,"Default volume is not 10 percent");
 gLawnApp->ShowGameSelector();
 gLawnApp->DoCreateUserDialog();
 auto* dialog=static_cast<NewUserDialog*>(gLawnApp->GetDialog(DIALOG_CREATEUSER));
 dialog->SetName("ccc");gLawnApp->FinishCreateUserDialog(true);
 check(gLawnApp->mPlayerInfo&&gLawnApp->mPlayerInfo->mName=="ccc","Real create-user dialog did not create ccc");
 check(gLawnApp->mPlayerInfo->mLevel==1,"New user not at first level");
 gLawnApp->DoNewOptions(true);
 auto* options=static_cast<NewOptionsDialog*>(gLawnApp->GetDialog(DIALOG_NEWOPTIONS));
 check(options&&options->mMusicVolumeSlider->mVal==.1&&options->mSfxVolumeSlider->mVal==.1,"Slider values differ from actual gains");
 check(!options->mFullscreenCheckbox->mVisible&&!options->mHardwareAccelerationCheckbox->mVisible,"Desktop settings visible on H1");
 options->mMusicVolumeSlider->MouseDown(85,26,1);options->mMusicVolumeSlider->MouseUp(85,26);
 check(gLawnApp->GetMusicVolume()>.45&&gLawnApp->GetMusicVolume()<.55,"Touching music slider did not change gain");
 options->mSfxVolumeSlider->MouseDown(102,26,1);options->mSfxVolumeSlider->MouseUp(102,26);
 check(gLawnApp->GetSfxVolume()>.55&&gLawnApp->GetSfxVolume()<.65,"Touching effects slider did not change gain");
 const auto music=gLawnApp->GetMusicVolume(),sound=gLawnApp->GetSfxVolume();H1AudioSettingsSave(gLawnApp);
 gLawnApp->SetMusicVolume(.9);gLawnApp->SetSfxVolume(.9);H1AudioSettingsLoad(gLawnApp);
 check(std::abs(gLawnApp->GetMusicVolume()-music)<.006&&std::abs(gLawnApp->GetSfxVolume()-sound)<.006,"Audio settings not restored");
 gLawnApp->KillNewOptionsDialog();
 auto* previous=gLawnApp->mPlayerInfo;
 gLawnApp->KillGameSelector();
 gLawnApp->PreNewGame(GAMEMODE_INTRO,false);auto* board=gLawnApp->mBoard;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* physical=H1GetPhysicalScreen(screen);
 if(!H1IntroMovieActive(board)||!physical||!physical->Active())throw std::runtime_error("Movie not prepared");
 const unsigned peak=pvzh1_intro_movie_diagnostic[1];
 {Graphics g(screen);h1DirtyCanvas.Begin(physical->Target());board->Draw(&g);
  const auto expanded=pvzh1_intro_movie_diagnostic[10];h1DirtyCanvas.Begin(physical->Target());board->Draw(&g);
  if(pvzh1_intro_movie_diagnostic[10]!=expanded)throw std::runtime_error("Repeated frame expanded again");}

 unsigned count=0,updates=0;
 for(int requested=0;requested<13890;requested+=40){
  while(board->mCutScene->mCutsceneTime<requested){++updates;gLawnApp->UpdateFrames();if(gLawnApp->mBoard!=board)throw std::runtime_error("Early handoff");}
  if(board->mPlants.mSize||board->mZombies.mSize||board->mLawnMowers.mSize)throw std::runtime_error("Decorative actors allocated during replay");
  h1DirtyCanvas.Begin(physical->Target());
  Graphics g(screen);g.Translate(board->mX,board->mY);board->Draw(&g);
  ++count;
 }
 if(pvzh1_intro_movie_diagnostic[2]!=348||pvzh1_intro_movie_diagnostic[7])throw std::runtime_error("Frame sequence incomplete");
 if(pvzh1_intro_movie_diagnostic[1]>=peak||pvzh1_intro_movie_diagnostic[15])throw std::runtime_error("Consumed frames retained in memory");
 gLawnApp->UpdateFrames();if(gLawnApp->mGameMode!=GAMEMODE_ADVENTURE||gLawnApp->mBoard==board||pvzh1_intro_movie_diagnostic[0]||pvzh1_intro_movie_diagnostic[1]||pvzh1_intro_movie_diagnostic[14]!=13890)throw std::runtime_error("Movie not released at natural handoff");
 for(unsigned i=0;i<5000&&gLawnApp->mGameScene!=GameScenes::SCENE_PLAYING;++i){gLawnApp->UpdateFrames();if(i%4==0){Graphics g(screen);g.Translate(gLawnApp->mBoard->mX,gLawnApp->mBoard->mY);gLawnApp->mBoard->Draw(&g);}}
 if(gLawnApp->mGameScene!=GameScenes::SCENE_PLAYING||gLawnApp->mBoard->mLevel!=1)throw std::runtime_error("First level did not start");
 std::ofstream proof(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"first-intro-export.json");
 proof<<"{\"passed\":true,\"frames\":"<<count<<",\"width\":480,\"height\":272,\"duration_ms\":13890,\"original_updates\":"<<updates+1<<",\"no_decorative_actors\":true,\"natural_first_level\":true,\"cache_released\":true,\"consumed_space_reused\":true,\"repeated_frame_reused\":true,\"compressed_cache_released\":true,\"peak_movie_bytes\":"<<peak<<"}\n";
 gLawnApp->DoBackToMain();check(gLawnApp->mGameScene==SCENE_MENU&&gLawnApp->mGameSelector&&!gLawnApp->mBoard,"Return to menu did not complete");
 gLawnApp->mPlayerInfo=previous;
}
