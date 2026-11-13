/* Return to cached title PCM must release halted shared XM readers. */
#include "SDL_mixer.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" volatile uint32_t pvzp_music_diagnostic[8];
extern "C" size_t pvzh1_music_release_idle(size_t);
extern "C" void pvzh1_music_warm_game();
extern "C" void pvzp_music_mix(int32_t*,unsigned);
extern "C" unsigned pvzh1_music_title_position(Mix_Music*);
extern "C" unsigned pvzh1_music_title_streaming(Mix_Music*);
extern "C" void pvzp_music_shutdown();
#include "PagedTitlePCM.h"
#include "Common.h"
#include <vector>
namespace {void Check(bool value){if(!value)throw std::runtime_error("Cached title retained halted tracker or failed restart");}}
extern "C" void pvzh1_music_title_reuse_probe(){
 pvzp_music_shutdown();auto* game=Mix_LoadMUS("sounds/mainmusic.xm");auto* title=Mix_LoadMUS("sounds/mainmusic.xm");Check(title&&game);
 Check(Mix_ModMusicStreamJumpToOrder(title,0x98)==0);
 const auto coldLoads=pvzp_music_diagnostic[5];pvzh1_music_warm_game();
 Check(Mix_PlayingMusicStream(title)&&pvzp_music_diagnostic[0]==1&&pvzp_music_diagnostic[4]==0&&pvzp_music_diagnostic[5]==coldLoads+1);
 pvzh1_music_warm_game();Check(pvzp_music_diagnostic[5]==coldLoads+1);
 Mix_VolumeMusic(128);Mix_VolumeMusicStream(title,128);Check(pvzh1_music_title_streaming(title));
 FILE* streamed=fopen(Sexy::GetResourcePath("sounds/mainmusic-title.h1pcm").c_str(),"rb");Check(streamed&&fseek(streamed,16+2*pvzh1_music_title_position(title),SEEK_SET)==0);
 for(unsigned block=0;block<2;++block){int16_t reference[256];int32_t mixed[256]{};Check(fread(reference,2,256,streamed)==256);pvzp_music_mix(mixed,256);for(unsigned i=0;i<256;++i)Check(mixed[i]==reference[i]);}fclose(streamed);
 Mix_HaltMusicStream(title);
 Check(Mix_PlayMusicStream(game,-1)==0);Check(Mix_ModMusicStreamJumpToOrder(game,0)==0);Check(pvzp_music_diagnostic[0]==1&&pvzp_music_diagnostic[4]==1);
 Check(pvzp_music_diagnostic[5]==coldLoads+1);
 Check(pvzp_music_diagnostic[3]==1841555);
 Check(Mix_ModMusicStreamJumpToOrder(game,(7<<16)|0x7a)==0);int chooseOrder=-1;Check(Mix_ModMusicStreamGetOrder(game,&chooseOrder)==0&&chooseOrder==((7<<16)|0x7a));
 Check(pvzp_music_diagnostic[5]==coldLoads+1&&pvzp_music_diagnostic[0]==1&&pvzp_music_diagnostic[3]==1841555);
 Check(Mix_ModMusicStreamJumpToOrder(game,0)==0);
 auto* active=Mix_LoadMUS("sounds/mainmusic.xm");Check(active);Check(Mix_PlayMusicStream(active,-1)==0);Check(Mix_ModMusicStreamJumpToOrder(active,0)==0);
 Mix_HaltMusicStream(game);Check(pvzp_music_diagnostic[0]==1&&pvzp_music_diagnostic[4]==1);
 Check(Mix_ModMusicStreamJumpToOrder(title,0x98)==0);Check(Mix_PlayingMusicStream(active)&&pvzp_music_diagnostic[0]==1&&pvzp_music_diagnostic[4]==1);
 Mix_HaltMusicStream(title);Mix_HaltMusicStream(active);
 Check(Mix_ModMusicStreamJumpToOrder(title,0x98)==0);Check(Mix_PlayingMusicStream(title)&&pvzp_music_diagnostic[0]==0&&pvzp_music_diagnostic[4]==0);
 Mix_HaltMusicStream(title);const auto before=pvzp_music_diagnostic[5];Check(Mix_PlayMusicStream(game,-1)==0);Check(Mix_ModMusicStreamJumpToOrder(game,0)==0);
 Check(pvzp_music_diagnostic[0]==1&&pvzp_music_diagnostic[4]==1&&pvzp_music_diagnostic[5]==before+1);
 Mix_FreeMusic(game);Mix_FreeMusic(title);Mix_FreeMusic(active);Check(pvzp_music_diagnostic[0]==0&&pvzp_music_diagnostic[4]==0);
 // Force the same idle-cache eviction and title reload that failed on H1.
 title=Mix_LoadMUS("sounds/mainmusic.xm");Check(title&&Mix_ModMusicStreamJumpToOrder(title,0x98)==0);Mix_HaltMusicStream(title);
 Check(pvzh1_music_release_idle(1)>=2790400);Check(Mix_ModMusicStreamJumpToOrder(title,0x98)==0);Mix_FreeMusic(title);
 FILE* file=fopen(Sexy::GetResourcePath("sounds/mainmusic-title.h1pcm").c_str(),"rb");Check(file);unsigned char header[16];Check(fread(header,1,16,file)==16);
 const unsigned count=unsigned(header[12])|(unsigned(header[13])<<8)|(unsigned(header[14])<<16)|(unsigned(header[15])<<24);H1PagedTitlePCM pcm;Check(pcm.Load(file,count));fclose(file);
 file=fopen(Sexy::GetResourcePath("sounds/mainmusic-title.h1pcm").c_str(),"rb");Check(file&&fseek(file,16,SEEK_SET)==0);std::vector<int16_t> expected(count),actual(count);Check(fread(expected.data(),2,count,file)==count);fclose(file);
 pcm.Copy(actual.data(),0,count);Check(actual==expected);for(unsigned at:{0u,1984u,2047u,2048u,count-256}){int16_t out[256];pcm.Copy(out,at,256);Check(std::equal(out,out+256,expected.begin()+at));}pcm.Clear();Check(pcm.empty()&&pcm.capacity_bytes()==0);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"music-title-reuse-verification.json");
 out<<"{\"passed\":true,\"cached_title_releases_halted_tracker\":true,\"title_pcm_stream_handoff\":true,\"stream_samples_compared\":512,\"day_and_choose_share_one_module\":true,\"day_and_choose_sample_bytes\":1841555,\"active_player_ownership_preserved\":true,\"subsequent_game_reloads_and_plays\":true,\"evicted_title_cache_reloaded\":true,\"paged_pcm_samples_compared\":"<<count<<",\"native_maximum_data_page_bytes\":4096,\"final_shared_modules\":0}\n";
}
