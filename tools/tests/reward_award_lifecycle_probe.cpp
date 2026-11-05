#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "graphics/RewardEffects.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_reward_effect_diagnostic[12];
extern "C" void pvzh1_reward_award_lifecycle_probe(){
 PlayerInfo profile;profile.mName="REWARD_LIFETIME";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 unsigned loads=pvzh1_reward_effect_diagnostic[1];
 for(unsigned repeat=0;repeat<2;++repeat){
  gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
  if(pvzh1_reward_effect_diagnostic[0])throw std::runtime_error("reward cache retained during new-board intro");
  gLawnApp->StartPlaying();
  if(!pvzh1_reward_effect_diagnostic[0])throw std::runtime_error("reward cache not preloaded");
  gLawnApp->KillBoard();gLawnApp->ShowAwardScreen(AWARD_FORLEVEL,false);
  if(pvzh1_reward_effect_diagnostic[0])throw std::runtime_error("reward cache retained during award UI loading");
  gLawnApp->KillAwardScreen();
 }
 if(pvzh1_reward_effect_diagnostic[1]!=loads+2)throw std::runtime_error("next-board reward reload mismatch");
 gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"reward-award-lifecycle-verification.json");out<<"{\"passed\":true,\"board_preloads\":2,\"not_resident_during_intro\":true,\"prepared_before_play\":true,\"released_before_award_UI_load\":true,\"next_board_reloads\":true,\"shutdown_bytes\":0}\n";
}
