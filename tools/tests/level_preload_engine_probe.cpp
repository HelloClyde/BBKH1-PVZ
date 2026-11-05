/* A late profile must not expand dependencies of the same current board. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern void H1PreloadCurrentBoard(LawnApp*);
extern "C" volatile uint32_t pvzh1_level_preload_diagnostic[4];
namespace {unsigned Loaded(){unsigned n=0;for(unsigned i=0;i<NUM_REANIMS;++i)n+=gReanimatorDefArray[i].mTracks.count!=0;return n;}}
extern "C" void pvzh1_level_preload_probe(){
    PlayerInfo profile;profile.mName="PRELOAD_TEST";profile.mLevel=1;profile.mFinishedAdventure=0;
    auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
    gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
    if(!gLawnApp->mBoard)throw std::runtime_error("Expected a real board");
    const unsigned before=Loaded();
    const unsigned plants=pvzh1_level_preload_diagnostic[1],zombies=pvzh1_level_preload_diagnostic[2];
    // Deliberately poison storage the original wave builder does not initialize.
    // Only host fixture memory is changed; no hardware or production state.
    auto* board=gLawnApp->mBoard;
    for(int w=0;w<MAX_ZOMBIE_WAVES;++w){
        bool terminated=w>=board->mNumWaves;
        for(auto& type:board->mZombiesInWave[w]){
            if(terminated)type=ZombieType::ZOMBIE_BOSS;
            else if(type==ZombieType::ZOMBIE_INVALID)terminated=true;
        }
    }
    profile.mLevel=50;profile.mFinishedAdventure=2;H1PreloadCurrentBoard(gLawnApp);
    if(Loaded()!=before)throw std::runtime_error("Profile unlocks expanded current-board preload");
    const unsigned deltaPlants=pvzh1_level_preload_diagnostic[1]-plants,deltaZombies=pvzh1_level_preload_diagnostic[2]-zombies;
    if(deltaPlants>10||deltaZombies>5)throw std::runtime_error("First-board dependency set too broad");
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"level-preload-verification.json");
    out<<"{\"passed\":true,\"uninitialized_wave_tails_ignored\":true,\"same_board_unaffected_by_completed_profile\":true,\"loaded_reanimation_definitions\":"<<before<<",\"current_board_plant_types\":"<<deltaPlants<<",\"current_board_zombie_types\":"<<deltaZombies<<"}\n";
    gLawnApp->KillSeedChooserScreen();gLawnApp->KillBoard();gLawnApp->mPlayerInfo=previous;
}
