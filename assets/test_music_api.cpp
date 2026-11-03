#include "SDL_mixer.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <memory>

static std::string root;
namespace Sexy {
std::string GetResourcePath(std::string_view relative) { return root+"/"+std::string(relative); }
}
extern "C" void pvzp_log_line(const char *text) { std::fprintf(stderr,"%s\n",text); }
extern "C" void pvzp_music_mix(int32_t*,unsigned);
extern "C" volatile uint32_t pvzp_music_diagnostic[8];

static unsigned long long mix_energy(unsigned blocks) {
    unsigned long long value=0;
    for (unsigned b=0;b<blocks;b++) {
        int32_t output[256]={};pvzp_music_mix(output,256);
        for (int v:output) value+=(unsigned long long)((int64_t)v*v);
    }
    return value;
}
static bool check(bool condition,const char *message) {
    if (!condition) std::fprintf(stderr,"FAIL: %s\n",message);
    return condition;
}
int main(int argc,char **argv) {
    if (argc!=2) return 2;root=argv[1];
    auto module=std::unique_ptr<Mix_Music,void(*)(Mix_Music*)>(Mix_LoadMUS("sounds/mainmusic.mo3"),Mix_FreeMusic);
    if (!check(bool(module),"load actual XM using original MO3 logical path")) return 3;
    if (!check(Mix_PlayMusicStream(module.get(),-1)==0,"play tracker")) return 4;
    if (!check(Mix_ModMusicStreamJumpToOrder(module.get(),0x98)==0,"jump to title")) return 5;
    int order=0;
    Mix_ModMusicStreamGetOrder(module.get(),&order);
    if (!check(order==0x98,"pending packed order")) return 6;
    unsigned long long normal=mix_energy(200);
    Mix_ModMusicStreamGetOrder(module.get(),&order);
    if (!check(normal>0 && (order&65535)==0x98 && (unsigned)order>>16>0,"title PCM and row progression")) return 7;
    int before=order;
    Mix_PauseMusicStream(module.get());
    if (!check(Mix_PausedMusicStream(module.get()) && mix_energy(50)==0,"pause emits silence")) return 8;
    Mix_ModMusicStreamGetOrder(module.get(),&order);
    if (!check(order==before,"pause freezes exact order and row")) return 9;
    Mix_ResumeMusicStream(module.get());
    if (!check(mix_energy(100)>0,"resume PCM")) return 10;
    if (!check(Mix_ModMusicStreamJumpToOrder(module.get(),0)==0,"jump to order zero")) return 11;
    mix_energy(20);Mix_ModMusicStreamGetOrder(module.get(),&order);
    if (!check((order&65535)==0,"order zero is a valid libxmp pre-first state")) return 12;
    int packed=(17<<16)|0x98;
    if (!check(Mix_ModMusicStreamJumpToOrder(module.get(),packed)==0,"packed row resume")) return 13;
    Mix_ModMusicStreamGetOrder(module.get(),&order);
    if (!check(order==packed,"exact pending packed row")) return 14;
    mix_energy(1);Mix_ModMusicStreamGetOrder(module.get(),&order);
    if (!check((order&65535)==0x98 && ((unsigned)order>>16)==17,"row 17 survives actual mix")) return 15;
    for (int ch=0;ch<30;ch++) if (!check(Mix_ModMusicStreamSetChannelVolume(module.get(),ch,0)==128,"30 original channel gains")) return 16;
    mix_energy(20);
    unsigned long long muted=mix_energy(200);
    for (int ch=0;ch<30;ch++) Mix_ModMusicStreamSetChannelVolume(module.get(),ch,128);
    unsigned long long restored=mix_energy(200);
    if (!check(restored>0 && muted<restored/100,"independent channel gain strongly suppresses PCM")) return 17;
    Mix_VolumeMusicStream(module.get(),0);
    if (!check(mix_energy(20)==0,"stream zero volume is exact silence")) return 18;
    Mix_VolumeMusicStream(module.get(),128);Mix_VolumeMusic(0);
    if (!check(mix_energy(20)==0,"global zero volume is exact silence")) return 19;
    Mix_VolumeMusic(128);
    if (!check(Mix_HaltMusicStream(module.get())==0 && !Mix_PlayingMusicStream(module.get()) && mix_energy(20)==0,"halt frees player and stops PCM")) return 20;
    auto drums=std::unique_ptr<Mix_Music,void(*)(Mix_Music*)>(Mix_LoadMUS("sounds/mainmusic.mo3"),Mix_FreeMusic);
    if (!check(bool(drums) && Mix_PlayMusicStream(module.get(),-1)==0 && Mix_PlayMusicStream(drums.get(),-1)==0,"MAIN and DRUMS independent descriptors")) return 30;
    if (!check(pvzp_music_diagnostic[0]==1 && pvzp_music_diagnostic[4]==2 && pvzp_music_diagnostic[5]==1,"two players share exactly one retained original module load")) return 40;
    if (!check(pvzp_music_diagnostic[1]>pvzp_music_diagnostic[2] && pvzp_music_diagnostic[3]>0,"lossless track sharing reduces live event storage")) return 41;
    std::printf("shared_modules=%u players=%u original_track_bytes=%u unique_track_bytes=%u sample_bytes=%u module_loads=%u\n",
                pvzp_music_diagnostic[0],pvzp_music_diagnostic[4],pvzp_music_diagnostic[1],pvzp_music_diagnostic[2],pvzp_music_diagnostic[3],pvzp_music_diagnostic[5]);
    Mix_ModMusicStreamJumpToOrder(module.get(),0x98);
    Mix_ModMusicStreamJumpToOrder(drums.get(),0x5e);
    mix_energy(80);
    int main_order=0,drums_before=0,drums_after=0;
    Mix_ModMusicStreamGetOrder(module.get(),&main_order);
    Mix_ModMusicStreamGetOrder(drums.get(),&drums_before);
    if (!check((main_order&65535)==0x98 && (drums_before&65535)==0x5e,"simultaneous independent orders")) return 31;
    Mix_PauseMusicStream(module.get());
    auto independent_energy=mix_energy(80);
    Mix_ModMusicStreamGetOrder(module.get(),&order);
    Mix_ModMusicStreamGetOrder(drums.get(),&drums_after);
    if (!check(order==main_order && drums_after!=drums_before && independent_energy>0,"MAIN pause leaves DRUMS advancing and audible")) return 32;
    for (int ch=0;ch<30;ch++) Mix_ModMusicStreamSetChannelVolume(drums.get(),ch,0);
    mix_energy(20);
    if (!check(mix_energy(80)==0,"DRUMS channel gains independently mute remaining player")) return 33;
    for (int ch=0;ch<30;ch++) Mix_ModMusicStreamSetChannelVolume(drums.get(),ch,128);
    Mix_ResumeMusicStream(module.get());
    if (!check(Mix_HaltMusicStream(module.get())==0 && Mix_PlayingMusicStream(drums.get()) && mix_energy(80)>0,"MAIN halt leaves DRUMS live")) return 34;
    if (!check(Mix_PlayMusicStream(module.get(),-1)==0 && Mix_ModMusicStreamJumpToOrder(module.get(),packed)==0 && mix_energy(1)>0,"MAIN restarts retained module with exact seek")) return 35;
    Mix_ModMusicStreamGetOrder(module.get(),&order);
    if (!check(order==packed,"restarted MAIN packed row")) return 36;
    module.reset();
    if (!check(Mix_PlayingMusicStream(drums.get()) && mix_energy(80)>0 && pvzp_music_diagnostic[0]==1 && pvzp_music_diagnostic[4]==1,"free one descriptor retains other player's module")) return 37;
    Mix_HaltMusicStream(drums.get());
    if (!check(Mix_PlayMusicStream(drums.get(),-1)==0 && Mix_ModMusicStreamJumpToOrder(drums.get(),0x98)==0 && mix_energy(80)>0,"last descriptor can stop and restart")) return 38;
    drums.reset();
    if (!check(mix_energy(20)==0 && pvzp_music_diagnostic[0]==0 && pvzp_music_diagnostic[4]==0,"free final descriptor stops audio and releases shared data")) return 39;
    auto credits=std::unique_ptr<Mix_Music,void(*)(Mix_Music*)>(Mix_LoadMUS("sounds/ZombiesOnYourLawn.ogg"),Mix_FreeMusic);
    if (!check(bool(credits) && Mix_PlayMusicStream(credits.get(),0)==0,"credits original audio path streams H1PC")) return 21;
    auto credits_energy=mix_energy(200);
    if (!check(credits_energy>0,"credits PCM nonzero")) return 22;
    std::printf("Music API passed: XM load/title/packed row seek/order0/pause/resume/30 channels/stream and global gain/halt/credits PCM; MAIN+DRUMS independent order/pause/channel gain/halt/restart/free.\n");
    std::printf("normal_energy=%llu channel_muted_energy=%llu channel_restored_energy=%llu credits_energy=%llu\n",normal,muted,restored,credits_energy);
    return 0;
}
