/* Real Boss music factory must reuse the prewarmed, reduced module. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/Music.h"
#include "Lawn/System/PlayerInfo.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdint>
extern "C" volatile uint32_t pvzp_music_diagnostic[8];
extern "C" void pvzp_music_mix(int32_t*,unsigned);
extern void H1PreloadCurrentBoardForPlay(LawnApp*);
extern "C" void pvzh1_boss_music_probe(){
 PlayerInfo p;p.mName="BOSS_MUSIC";p.mLevel=50;auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;
 gLawnApp->mMusic->MusicTitleScreenInit();gLawnApp->mMusic->StopAllMusic();
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 gLawnApp->mMusic->StopAllMusic();H1PreloadCurrentBoardForPlay(gLawnApp);const auto loads=pvzp_music_diagnostic[5];
 fprintf(stderr,"BOSS PREWARM modules=%u samples=%u players=%u loads=%u\n",pvzp_music_diagnostic[0],pvzp_music_diagnostic[3],pvzp_music_diagnostic[4],pvzp_music_diagnostic[5]);
 if(pvzp_music_diagnostic[0]!=1||pvzp_music_diagnostic[3]!=1783700)throw std::runtime_error("Boss preload warmed the wrong module");
 gLawnApp->mMusic->StartGameMusic();
 fprintf(stderr,"BOSS START modules=%u samples=%u players=%u loads=%u\n",pvzp_music_diagnostic[0],pvzp_music_diagnostic[3],pvzp_music_diagnostic[4],pvzp_music_diagnostic[5]);
 if(gLawnApp->mMusic->mCurMusicTune!=MUSIC_TUNE_FINAL_BOSS_BRAINIAC_MANIAC||pvzp_music_diagnostic[5]!=loads||pvzp_music_diagnostic[4]!=1)throw std::runtime_error("Boss music start reread or failed to play");
 unsigned nonzero=0;for(unsigned frame=0;frame<32;++frame){int32_t samples[256]{};pvzp_music_mix(samples,256);for(auto v:samples)nonzero+=v!=0;}
 if(!nonzero)throw std::runtime_error("Boss music is silent");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-music-factory-verification.json");out<<"{\"passed\":true,\"sample_bytes\":1783700,\"cold_loads_on_start\":0,\"active_players\":"<<pvzp_music_diagnostic[4]<<",\"nonzero_audio_samples\":"<<nonzero<<"}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
