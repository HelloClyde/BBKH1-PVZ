#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "PvzpLib/PvzpCommon.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_save_file_diagnostic[8];
namespace {void Check(bool v){if(!v)throw std::runtime_error("Original profile/V4 roundtrip failed");}}
extern "C" void pvzh1_save_engine_probe(){
    PlayerInfo profile;profile.mName="SAVE_TEST";profile.mId=1;profile.mLevel=4;
    auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
    profile.SaveDetails();PlayerInfo loaded;loaded.mId=1;loaded.mLevel=0;loaded.LoadDetails();Check(loaded.mLevel==4);
    gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);Check(gLawnApp->mBoard);
    gLawnApp->mBoard->mSunMoney=137;
    const auto path=GetSavedGameName(GAMEMODE_ADVENTURE,profile.mId);Check(LawnSaveGame(gLawnApp->mBoard,path));
    const auto first=std::filesystem::file_size(path);Check(first>24);
    // Replace an existing V4, then enter the original saved-board flow.
    gLawnApp->mBoard->mSunMoney=263;Check(LawnSaveGame(gLawnApp->mBoard,path));
    gLawnApp->KillBoard();gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,true);
    Check(gLawnApp->mBoard&&gLawnApp->mBoard->mLevel==4&&gLawnApp->mBoard->mSunMoney==263);
    gLawnApp->KillBoard();gLawnApp->mPlayerInfo=previous;
    Check(!pvzh1_save_file_diagnostic[6]);
    for(const auto& p:std::filesystem::recursive_directory_iterator(getenv("PVZ_SAVE_DIR")))
        Check(p.path().extension()!=".h1tmp"&&p.path().extension()!=".h1bak");
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"save-engine-verification.json");
    out<<"{\"passed\":true,\"original_profile_roundtrip\":true,\"original_V4_roundtrip\":true,\"level\":4,\"sun_restored\":263,\"first_V4_bytes\":"<<first<<",\"checked_writes\":"<<pvzh1_save_file_diagnostic[5]<<",\"failed_writes\":0,\"staging_artifacts\":0}\n";
}
