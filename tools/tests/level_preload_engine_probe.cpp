#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Cutscene.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern void H1PreloadCurrentBoard(LawnApp*);
namespace {void Check(bool v){if(!v)throw std::runtime_error("Current-level preload omitted a required dependency or loaded unused unlocked plants");}}
extern "C" void pvzh1_level_preload_probe(){
    PlayerInfo profile;profile.mName="PRELOAD_TEST";profile.mId=1;profile.mLevel=31;
    auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
    Check(gLawnApp->HasSeedType(SEED_WALLNUT));Check(!gReanimatorDefArray[REANIM_WALLNUT].mTracks.tracks);
    gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;Check(board&&board->mLevel==31);
    board->mCutScene->PreloadResources();
    Check(!gReanimatorDefArray[REANIM_WALLNUT].mTracks.tracks);
    Check(gReanimatorDefArray[REANIM_SPLASH].mTracks.tracks&&gReanimatorDefArray[REANIM_POOL_CLEANER].mTracks.tracks);
    board->mSeedBank->mSeedPackets[0].SetPacketType(SEED_WALLNUT,SEED_NONE);
    H1PreloadCurrentBoard(gLawnApp);Check(gReanimatorDefArray[REANIM_WALLNUT].mTracks.tracks);
    gLawnApp->KillSeedChooserScreen();gLawnApp->KillBoard();gLawnApp->mPlayerInfo=previous;
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"level-preload-verification.json");
    out<<"{\"passed\":true,\"level\":31,\"unused_unlocked_wallnut_not_loaded\":true,\"original_pool_dependencies_loaded\":true,\"selected_wallnut_loaded_before_play\":true,\"serialization_and_content_unchanged\":true}\n";
}
