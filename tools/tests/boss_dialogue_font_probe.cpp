/* Draw the original final-Boss Dave dialogue through its real font callers. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/MemoryImageCache.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_font_logical_request_diagnostic[8];
extern "C" char pvzh1_font_logical_request_path[128];
extern "C" void pvzh1_boss_dialogue_font_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mName="BOSS_DIALOGUE_FONT";profile.mLevel=50;
 auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();
 gLawnApp->CrazyDaveEnter();unsigned draws=0;const auto before=pvzh1_font_logical_request_diagnostic[0];
 for(unsigned message=2300;message<2306;++message){gLawnApp->CrazyDaveTalkIndex(message);
  for(unsigned frame=0;frame<4;++frame){auto*r=gLawnApp->ReanimationGet(gLawnApp->mCrazyDaveReanimID);r->Update();gLawnApp->UpdateCrazyDave();h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();Graphics g(screen);gLawnApp->DrawCrazyDave(&g);++draws;H1ImageCacheEndFrame();}
 }
 const auto requests=pvzh1_font_logical_request_diagnostic[0]-before;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-dialogue-font-verification.json");out<<"{\"passed\":"<<(!requests?"true":"false")<<",\"original_dialogue_draws\":"<<draws<<",\"font_atlas_requests\":"<<requests<<",\"last_font\":\""<<pvzh1_font_logical_request_path<<"\"}\n";out.close();
 if(requests)throw std::runtime_error("Original Dave dialogue expanded a font atlas");gLawnApp->CrazyDaveLeave();gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
