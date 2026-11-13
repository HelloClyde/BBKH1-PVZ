/* Actual TryLoadGame callback, using an unmodified physical-H1 fixture save. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/Music.h"
#include "Lawn/Widget/ContinueDialog.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdlib>
extern "C" volatile uint32_t pvzp_music_diagnostic[8];
extern "C" volatile uint32_t pvzh1_level_preload_diagnostic[4];
extern "C" void pvzp_music_mix(int32_t*,unsigned);
extern bool H1TargetedBoardPreloadActive();
extern "C" void __real__Z25H1PrewarmDefinitionImagesib(int,bool);
namespace {unsigned targetedFutureLoads=0;}
extern "C" void __wrap__Z25H1PrewarmDefinitionImagesib(int type,bool loading){
 if(H1TargetedBoardPreloadActive()&&type==REANIM_ZOMBIE_NEWSPAPER)++targetedFutureLoads;
 __real__Z25H1PrewarmDefinitionImagesib(type,loading);
}
namespace {void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
extern "C" void pvzh1_boss_resume_preload_probe(){
 namespace fs=std::filesystem;
 PlayerInfo p;p.mName="RESUME_H1";p.mId=1;p.mLevel=50;
 auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->mGameMode=GAMEMODE_ADVENTURE;
 fs::path target=fs::absolute(GetSavedGameName(GAMEMODE_ADVENTURE,p.mId)).lexically_normal();
 auto allowed=fs::absolute(fs::path(getenv("PVZ_HOST_OUTPUT")).parent_path()/"save").lexically_normal();
 Check(target.string().starts_with(allowed.string()),"Probe save destination escaped isolated save folder");
 fs::create_directories(target.parent_path());
 fs::copy_file("build/zombie-logical-v26-hardware/fixture-sustained-game1_0.v4",target,fs::copy_options::overwrite_existing);
 // The public entry owns the loading/audio scopes required by TryLoadGame.
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,true);
 Check(gLawnApp->mBoard&&gLawnApp->mBoard->mLevel==50,"Wrong loaded board");
  Check(gLawnApp->mBoard->mMainCounter==5522,"Loading advanced or replaced the saved simulation");
 Check(pvzh1_level_preload_diagnostic[0]==1,"Saved board was preloaded repeatedly");
  Check(targetedFutureLoads==0,"Targeted resume loaded an unused future enemy");
 Check(pvzp_music_diagnostic[0]==1&&pvzp_music_diagnostic[3]==1783700,"Continue did not prewarm the Boss module");
 Check(gReanimatorDefArray[REANIM_ZOMBIE_NEWSPAPER].mTracks.count==0,"Future unreferenced enemy still retained on resume");
 unsigned borrowers=0;
 for(auto*r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
  Check(r->mDefinition&&r->mDefinition->mTracks.count>0,"Saved active/pending borrower definition was retired");++borrowers;
 }
 const auto loads=pvzp_music_diagnostic[5];
 auto* dialog=dynamic_cast<ContinueDialog*>(gLawnApp->GetDialog(DIALOG_CONTINUE));
 Check(dialog,"Original Continue dialog was not created");
 dialog->ButtonDepress(ContinueDialog::ContinueDialog_Continue);
 gLawnApp->mBoard->Pause(false);
 Check(pvzp_music_diagnostic[5]==loads&&pvzp_music_diagnostic[4]==1,"Continue reread music or failed to start its player");
 unsigned nonzero=0;
 for(unsigned f=0;f<32;++f){int32_t samples[256]{};pvzp_music_mix(samples,256);for(auto v:samples)nonzero+=v!=0;}
 Check(nonzero,"Resumed Boss music is silent");
 ReanimatorEnsureDefinitionLoaded(REANIM_ZOMBIE_NEWSPAPER,true);
 Check(gReanimatorDefArray[REANIM_ZOMBIE_NEWSPAPER].mTracks.count>0,"Future enemy definition cannot reload");
 std::ofstream out(fs::path(getenv("PVZ_HOST_OUTPUT"))/"boss-resume-preload-verification.json");
 out<<"{\"passed\":true,\"actual_try_load_game\":true,\"physical_fixture_save\":true,\"single_preload\":true,\"music_cold_loads_after_continue\":0,\"players\":"<<pvzp_music_diagnostic[4]<<",\"nonzero_samples\":"<<nonzero<<",\"saved_borrowers_preserved\":"<<borrowers<<",\"future_enemy_retired_and_reloadable\":true}\n";
 gLawnApp->KillDialog(DIALOG_CONTINUE);gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
