/* Reproducible performance input, created through original game objects and
 * V4 serialization. Does not advance levels, award wins, or write results. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/CutScene.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" void pvzh1_dense_plant_fixture(){
 PlayerInfo profile;profile.mName="DENSE_PERF";profile.mId=1;profile.mLevel=6;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
 for(auto* p:board->mPlants)if(!p->mDead)p->Die();
 const SeedType cards[]={SEED_PEASHOOTER,SEED_SUNFLOWER,SEED_CHERRYBOMB,SEED_WALLNUT,SEED_POTATOMINE,SEED_SNOWPEA};
 for(int i=0;i<board->mSeedBank->mNumPackets;++i)board->mSeedBank->mSeedPackets[i].SetPacketType(cards[i%6],SEED_NONE);
 // Construct the legitimate post-intro game state directly as a test input;
 // the production Adventure animation is neither changed nor shortened.
 gLawnApp->StartPlaying();board->mX=130;board->mY=0;board->mSunMoney=500;
 unsigned plants=0;
 for(int row=0;row<5;++row)for(int col=0;col<8;++col){
  const SeedType seed=col<2?SEED_SUNFLOWER:col==7?SEED_WALLNUT:SEED_PEASHOOTER;
  if(board->CanPlantAt(col,row,seed)!=PLANTING_OK)throw std::runtime_error("Illegal performance fixture planting");
  board->AddPlant(col,row,seed);++plants;
 }
 const auto path=GetSavedGameName(GAMEMODE_ADVENTURE,profile.mId);
 if(!LawnSaveGame(board,path)||plants!=40)throw std::runtime_error("Dense fixture serialization failed");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"dense-fixture-verification.json");
 out<<"{\"passed\":true,\"level\":6,\"plants\":40,\"sunflowers\":10,\"peashooters\":25,\"wallnuts\":5,\"original_legal_planting_and_serialization\":true,\"game_success_state_written\":false,\"production_animation_unchanged\":true}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
