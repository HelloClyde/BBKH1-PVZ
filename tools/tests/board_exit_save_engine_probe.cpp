/* Retire only replay data before the original dense-board V4 serialization. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <stdexcept>
extern void H1RetireBoardReplay();
extern void H1PreloadCurrentBoard(LawnApp*);
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" volatile uint32_t pvzh1_board_replay_retirement_diagnostic[3];
extern "C" void pvzh1_board_exit_save_probe(){
 auto check=[](bool v,const char* why){if(!v){std::fprintf(stderr,"EXIT SAVE ORACLE: %s\n",why);throw std::runtime_error(why);}};auto bytes=[](const std::string& path){std::ifstream f(path,std::ios::binary);return std::vector<char>((std::istreambuf_iterator<char>(f)),{});};
 PlayerInfo profile;profile.mName="EXIT_SAVE_ORACLE";profile.mId=1;profile.mLevel=6;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);gLawnApp->StartPlaying();auto* board=gLawnApp->mBoard;board->mX=130;board->mY=0;
 for(int row=0;row<5;++row)for(int col=0;col<8;++col)board->AddPlant(col,row,col<2?SEED_SUNFLOWER:col==7?SEED_WALLNUT:SEED_PEASHOOTER);
 H1PreloadCurrentBoard(gLawnApp);check(pvzh1_actor_pose_diagnostic[0]>1000000,"replay pressure fixture was not loaded");std::fprintf(stderr,"EXIT SAVE ORACLE: warmed\n");
 const auto path=GetSavedGameName(GAMEMODE_ADVENTURE,profile.mId);check(LawnSaveGame(board,path),"baseline V4 save failed");auto original=bytes(path);check(original.size()>200000,"dense save fixture too small");
 std::vector<std::pair<Reanimation*,std::vector<unsigned char>>> actors;
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)actors.push_back({r,std::vector<unsigned char>((unsigned char*)r,(unsigned char*)r+sizeof(*r))});
 H1RetireBoardReplay();check(!pvzh1_actor_pose_diagnostic[0],"replay data survived retirement");std::fprintf(stderr,"EXIT SAVE ORACLE: retired\n");
 for(auto& [r,state]:actors)check(!std::memcmp(r,state.data(),state.size()),"retirement changed original animation state");
 check(LawnSaveGame(board,path)&&bytes(path)==original,"retirement changed serialized V4 bytes");
 H1PreloadCurrentBoard(gLawnApp);auto calls=pvzh1_board_replay_retirement_diagnostic[0];
 std::fprintf(stderr,"EXIT SAVE ORACLE: confirmed UI exit\n");gLawnApp->DoConfirmBackToMain();gLawnApp->ButtonDepress(2000+int(DIALOG_CONFIRM_BACK_TO_MAIN));std::fprintf(stderr,"EXIT SAVE ORACLE: menu reached\n");
 check(pvzh1_board_replay_retirement_diagnostic[0]==calls+1&&!pvzh1_board_replay_retirement_diagnostic[2],"confirmed exit did not retire before saving");
 check(!gLawnApp->mBoard&&gLawnApp->mGameSelector,"confirmed exit did not reach the menu");
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,true);board=gLawnApp->mBoard;check(board&&gLawnApp->GetDialog(DIALOG_CONTINUE),"original continue flow rejected confirmed-exit save");unsigned plants=0;for(auto* p:board->mPlants)if(!p->mDead)++plants;check(plants==40,"confirmed-exit save lost plants");
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;pvzh1_actor_pose_shutdown();
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"board-exit-save-verification.json");out<<"{\"passed\":true,\"plants\":"<<plants<<",\"retained_animation_states\":"<<actors.size()<<",\"identical_V4_bytes\":"<<original.size()<<",\"confirmed_exit_saved_and_original_reader_reloaded\":true,\"replay_shutdown_bytes\":0}\n";
}
