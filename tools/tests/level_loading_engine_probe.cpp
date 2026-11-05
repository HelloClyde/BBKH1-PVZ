/* Exercise the reused loader after deleting the original startup widget. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "Lawn/Widget/TitleScreen.h"
#include "PvzpLib/PvzpCommon.h"
#include "Startup.h"
#include "Resources.h"
#include "graphics/SoftwareDisplay.h"
#include "widget/WidgetManager.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <algorithm>
extern "C" volatile uint32_t pvzh1_level_loading_diagnostic[12];
namespace {
unsigned frames=0,startUpdate=0;bool beforeBoard=false;
std::vector<uint32_t> original;unsigned exactFrames=0;
int Observe(const uint32_t* p,int w,int h,void*){
 if(!pvzh1_level_loading_diagnostic[1])return 1;
 if(w!=480||h!=272||gLawnApp->mUpdateCount!=startUpdate)throw std::runtime_error("Loader advanced simulation or changed screen size");
 const unsigned stage=pvzh1_level_loading_diagnostic[5];
 if(stage==0||stage==100){const auto* expected=original.data()+(stage==100?480*272:0);if(!std::equal(expected,expected+480*272,p))throw std::runtime_error("Reused loading pixels differ from original renderer");++exactFrames;}
 if(!frames){beforeBoard=!gLawnApp->mBoard;auto path=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"level-loading.ppm";std::ofstream f(path,std::ios::binary);f<<"P6\n480 272\n255\n";for(int i=0;i<w*h;++i){char rgb[3]={char(p[i]>>16),char(p[i]>>8),char(p[i])};f.write(rgb,3);}}
 ++frames;return 1;
}
}
extern "C" void pvzh1_level_loading_probe(){
 const char* reference=getenv("PVZ_LOADING_REFERENCE");if(!reference)throw std::runtime_error("Original loading reference required");std::ifstream input(reference,std::ios::binary);uint32_t header[4];input.read((char*)header,16);if(!input||header[0]!=0x4c4c3148||header[2]!=480||header[3]!=272)throw std::runtime_error("Invalid loader oracle");original.resize(480*272*2);input.read((char*)original.data(),original.size()*4);if(!input)throw std::runtime_error("Truncated loader oracle");
 PlayerInfo profile;profile.mName="LOADER_TEST";profile.mLevel=2;profile.mId=1;auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->mWidgetManager->RemoveWidget(gLawnApp->mTitleScreen.get());gLawnApp->mTitleScreen.reset();gLawnApp->ShowGameSelector();gLawnApp->KillGameSelector();
 if(gLawnApp->mTitleScreen||!pvzh1_level_loading_diagnostic[10])throw std::runtime_error("Baked reused loader was not prepared");
 startUpdate=gLawnApp->mUpdateCount;pvzh1_graphics_set_present(Observe,nullptr);
 {H1LevelLoadingScope outer;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);if(!pvzh1_level_loading_diagnostic[1])throw std::runtime_error("Nested scope hid loader early");}
 if(pvzh1_level_loading_diagnostic[10]||!beforeBoard||frames<2||pvzh1_level_loading_diagnostic[1]||pvzh1_level_loading_diagnostic[5]!=100||pvzh1_level_loading_diagnostic[2]!=pvzh1_level_loading_diagnostic[3])throw std::runtime_error("Fresh board loader did not complete");
 if(!gLawnApp->mBoard||gLawnApp->mGameScene!=SCENE_LEVEL_INTRO)throw std::runtime_error("Original new game flow changed");
 const auto path=GetSavedGameName(GAMEMODE_ADVENTURE,profile.mId);if(!LawnSaveGame(gLawnApp->mBoard,path))throw std::runtime_error("Host save fixture failed");
 gLawnApp->KillBoard();const unsigned before=frames;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,true);
 if(!gLawnApp->mBoard||frames<=before||pvzh1_level_loading_diagnostic[1]||pvzh1_level_loading_diagnostic[2]!=2||pvzh1_level_loading_diagnostic[3]!=2)throw std::runtime_error("Saved-game loader or cleanup changed");
 gLawnApp->mWidgetManager->MarkAllDirty();gLawnApp->DrawDirtyStuff();if(pvzh1_level_loading_diagnostic[1])throw std::runtime_error("Loader covers the continuation dialog");
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=previous;
 if(exactFrames!=4)throw std::runtime_error("Missing empty/full original loader comparisons");std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"level-loading-verification.json");out<<"{\"passed\":true,\"startup_widget_deleted\":true,\"startup_assets_reused\":true,\"first_present_before_board_creation\":true,\"fresh_and_saved_games\":true,\"no_simulation_updates\":true,\"loader_hidden_before_continue_dialog\":true,\"balanced_scopes\":2,\"original_pixel_comparisons\":"<<exactFrames<<",\"loading_frames\":"<<frames<<"}\n";
}
