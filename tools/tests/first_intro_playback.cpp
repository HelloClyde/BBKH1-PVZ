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
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_intro_movie_diagnostic[16];
extern "C" void pvzh1_first_intro_export(){
 using namespace Sexy;
 // Legal native-format fresh-user input, emitted by the original serializer.
 auto* fresh=gLawnApp->mProfileMgr->AddProfile("INTRO_TEST");
 if(!fresh)throw std::runtime_error("Unable to create fixture profile");
 fresh->SaveDetails();gLawnApp->mProfileMgr->Save();
 const auto fixture=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"fresh-user";
 std::filesystem::create_directories(fixture);
 std::filesystem::copy_file(GetAppDataPath("userdata/users.dat"),fixture/"users.dat",std::filesystem::copy_options::overwrite_existing);
 std::filesystem::copy_file(GetAppDataPath("userdata/user1.dat"),fixture/"user1.dat",std::filesystem::copy_options::overwrite_existing);
 auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=fresh;
 gLawnApp->PreNewGame(GAMEMODE_INTRO,false);auto* board=gLawnApp->mBoard;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* physical=H1GetPhysicalScreen(screen);
 if(!H1IntroMovieActive(board)||!physical||!physical->Active())throw std::runtime_error("Movie not prepared");
 const unsigned peak=pvzh1_intro_movie_diagnostic[1];
 {Graphics g(screen);h1DirtyCanvas.Begin(physical->Target());board->Draw(&g);
  const auto expanded=pvzh1_intro_movie_diagnostic[10];h1DirtyCanvas.Begin(physical->Target());board->Draw(&g);
  if(pvzh1_intro_movie_diagnostic[10]!=expanded)throw std::runtime_error("Repeated frame expanded again");}
 std::ofstream frames(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"first-intro.argb",std::ios::binary);
 unsigned count=0,updates=0;
 for(int requested=0;requested<13890;requested+=40){
  while(board->mCutScene->mCutsceneTime<requested){++updates;gLawnApp->UpdateFrames();if(gLawnApp->mBoard!=board)throw std::runtime_error("Early handoff");}
  if(board->mPlants.mSize||board->mZombies.mSize||board->mLawnMowers.mSize)throw std::runtime_error("Decorative actors allocated during replay");
  h1DirtyCanvas.Begin(physical->Target());
  Graphics g(screen);g.Translate(board->mX,board->mY);board->Draw(&g);
  frames.write(reinterpret_cast<const char*>(physical->Target()->GetBits()),480*272*4);++count;
 }
 if(pvzh1_intro_movie_diagnostic[2]!=348||pvzh1_intro_movie_diagnostic[7])throw std::runtime_error("Frame sequence incomplete");
 if(pvzh1_intro_movie_diagnostic[1]>=peak||pvzh1_intro_movie_diagnostic[15])throw std::runtime_error("Consumed frames retained in memory");
 gLawnApp->UpdateFrames();if(gLawnApp->mGameMode!=GAMEMODE_ADVENTURE||gLawnApp->mBoard==board||pvzh1_intro_movie_diagnostic[0]||pvzh1_intro_movie_diagnostic[1]||pvzh1_intro_movie_diagnostic[14]!=13890)throw std::runtime_error("Movie not released at natural handoff");
 for(unsigned i=0;i<5000&&gLawnApp->mGameScene!=GameScenes::SCENE_PLAYING;++i){gLawnApp->UpdateFrames();if(i%4==0){Graphics g(screen);g.Translate(gLawnApp->mBoard->mX,gLawnApp->mBoard->mY);gLawnApp->mBoard->Draw(&g);}}
 if(gLawnApp->mGameScene!=GameScenes::SCENE_PLAYING||gLawnApp->mBoard->mLevel!=1)throw std::runtime_error("First level did not start");
 std::ofstream proof(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"first-intro-export.json");
 proof<<"{\"passed\":true,\"frames\":"<<count<<",\"width\":480,\"height\":272,\"duration_ms\":13890,\"original_updates\":"<<updates+1<<",\"no_decorative_actors\":true,\"natural_first_level\":true,\"cache_released\":true,\"consumed_space_reused\":true,\"repeated_frame_reused\":true,\"compressed_cache_released\":true,\"peak_movie_bytes\":"<<peak<<"}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=previous;
}
