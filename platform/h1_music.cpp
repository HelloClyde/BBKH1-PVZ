/* LGPL-3.0-or-later. Original tracker orders/channels through libxmp-lite.
 * MO3 is unpacked offline to XM; original music scheduling stays in Music.cpp.
 * Ogg credits use the same streamed H1PC format as sound effects.
 */
#include "SDL_mixer.h"
#include "h1_host.h"
#ifdef PVZ_NATIVE_PROFILE
#include "profiler.h"
#endif
#include "Common.h"
#include "xmp_shared.h"
#include "PagedTitlePCM.h"
#include <xmp.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>
#include <new>

struct SharedTracker {
    std::string path;
    pvzh1_xmp_module *module=nullptr;
    unsigned readers=0;
};
static std::vector<SharedTracker*> modules;
struct Mix_Music{
    std::string path;
    xmp_context module=nullptr;
    SharedTracker *shared=nullptr;
    FILE *pcm=nullptr;
    unsigned pcm_samples=0,pcm_position=0;
    H1PagedTitlePCM title_samples;
    int volume=128,loops=0;
    bool tracker=false,title_pcm=false,title_streaming=false,playing=false,paused=false,pending_order=false;
    int requested_order=0;
    int channels[64];
    Mix_Music(){for(int &v:channels)v=128;}
};
static std::vector<Mix_Music*> streams;
static int global_volume=128;
extern "C" { volatile uint32_t pvzp_music_diagnostic[8]; }
static void update_stats(){
    uint32_t original=0,unique=0,samples=0,players=0;
    for(SharedTracker *entry:modules){
        pvzh1_xmp_module_stats s{};
        if(pvzh1_xmp_module_get_stats(entry->module,&s)==0){
            original+=(uint32_t)s.original_track_bytes;unique+=(uint32_t)s.unique_track_bytes;
            samples+=(uint32_t)s.sample_bytes;players+=s.players;
        }
    }
    pvzp_music_diagnostic[0]=(uint32_t)modules.size();pvzp_music_diagnostic[1]=original;
    pvzp_music_diagnostic[2]=unique;pvzp_music_diagnostic[3]=samples;pvzp_music_diagnostic[4]=players;
}
static uint32_t little32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static std::string module_path(const Mix_Music *music){
    const std::string suffix="mainmusic.xm";
    const unsigned order=music->requested_order&65535;
    if(order>=0xdd&&order<0xec&&music->path.size()>=suffix.size()&&
       music->path.compare(music->path.size()-suffix.size(),suffix.size(),suffix)==0)
        return music->path.substr(0,music->path.size()-3)+"-award.xm";
    if((order<0x30||(order>=0x7a&&order<0x7d))&&music->path.size()>=suffix.size()&&
       music->path.compare(music->path.size()-suffix.size(),suffix.size(),suffix)==0)
        return music->path.substr(0,music->path.size()-3)+"-day.xm";
    return music->path;
}
static void release_module(Mix_Music *music);
static bool acquire_module(Mix_Music *music){
    // Keep the compact module for the descriptor's lifetime. Tune switches
    // stop/recreate independent players without rereading/reexpanding all XM
    // tracks, which would otherwise create a large transient memory peak.
    const std::string path=module_path(music);
    if(music->shared&&music->shared->path==path)return true;
    if(music->shared)release_module(music);
    for(SharedTracker *entry:modules)if(entry->path==path){
        ++entry->readers;music->shared=entry;return true;
    }
    pvzh1_xmp_module *handle=pvzh1_xmp_module_load(path.c_str());
    if(!handle)return false;
    auto *entry=new(std::nothrow) SharedTracker;
    if(!entry){pvzh1_xmp_module_free(handle);return false;}
    entry->path=path;entry->module=handle;entry->readers=1;
    modules.push_back(entry);music->shared=entry;pvzp_music_diagnostic[5]=pvzp_music_diagnostic[5]+1;update_stats();
    char message[160];snprintf(message,sizeof(message),"H1 XM shared module: track bytes %u -> %u; samples %u.",
        pvzp_music_diagnostic[1],pvzp_music_diagnostic[2],pvzp_music_diagnostic[3]);pvzp_log_line(message);
    return true;
}
static void release_module(Mix_Music *music){
    SharedTracker *entry=music->shared;if(!entry)return;music->shared=nullptr;
    if(--entry->readers)return;
    pvzh1_xmp_module_free(entry->module);
    modules.erase(std::remove(modules.begin(),modules.end(),entry),modules.end());delete entry;update_stats();
}
static void close_player(Mix_Music *music){
    music->playing=music->paused=false; // Releasing a tracker may service other audio streams.
    if(music->module){pvzh1_xmp_module_free_player(music->module);music->module=nullptr;update_stats();}
    if(music->pcm){fclose(music->pcm);music->pcm=nullptr;}music->playing=music->paused=music->title_pcm=music->title_streaming=false;
}
static bool start_player(Mix_Music *music){
    if(music->tracker){
        if(!acquire_module(music))return false;
        music->module=pvzh1_xmp_module_create_player(music->shared->module);
        if(!music->module)return false;
        if(xmp_start_player(music->module,16000,XMP_FORMAT_MONO)<0){close_player(music);return false;}
        xmp_set_player(music->module,XMP_PLAYER_INTERP,XMP_INTERP_LINEAR);
        xmp_set_player(music->module,XMP_PLAYER_DSP,0);
        for(int i=0;i<64;i++)xmp_channel_vol(music->module,i,music->channels[i]*100/128);
        update_stats();
        return true;
    }
    music->pcm=fopen(music->path.c_str(),"rb");if(!music->pcm)return false;
    if(fseek(music->pcm,16,SEEK_SET)){fclose(music->pcm);music->pcm=nullptr;return false;}
    music->pcm_position=0;return true;
}
extern "C" Mix_Music *Mix_LoadMUS(const char *filename){
    if(!filename)return nullptr;std::string base(filename);size_t dot=base.rfind('.');
    std::string ext=dot==std::string::npos?"":base.substr(dot);if(dot!=std::string::npos)base.resize(dot);
    bool tracker=ext==".mo3"||ext==".xm";
    std::string path=Sexy::GetResourcePath(base+(tracker?".xm":".h1pcm"));FILE *f=fopen(path.c_str(),"rb");
    if(!f){pvzp_log_line(("Music input missing: "+path).c_str());return nullptr;}
    unsigned char header[24]={0};size_t n=fread(header,1,sizeof(header),f);fclose(f);
    if(tracker){if(n<17||memcmp(header,"Extended Module: ",17))return nullptr;}
    else if(n<16||memcmp(header,"H1PC",4)||little32(header+4)!=1||little32(header+8)!=16000)return nullptr;
    Mix_Music *music=new Mix_Music;music->path=path;music->tracker=tracker;
    if(!tracker)music->pcm_samples=little32(header+12);streams.push_back(music);return music;
}
extern "C" Mix_Music *Mix_LoadMUS_RW(SDL_RWops *source,int close_source){
    /* All H1 music loads retain their source filename through the reviewed
       Music::PvzpLoadMusic IO patch. Do not guess music identity from bytes. */
    if(close_source)delete source;pvzp_log_line("Unexpected memory-only music load");return nullptr;
}
extern "C" void Mix_FreeMusic(Mix_Music *music){if(!music)return;close_player(music);release_module(music);streams.erase(std::remove(streams.begin(),streams.end(),music),streams.end());delete music;}
extern "C" int Mix_PlayMusicStream(Mix_Music *music,int loops){
    if(!music)return -1;close_player(music);music->loops=loops;music->pending_order=false;
    // The original tracker API supplies the tune/order immediately afterwards.
    // Wait for that order so the menu can use the sample-exact offline loop.
    if(music->tracker)return 0;
    if(!start_player(music)){pvzp_log_line(("Tracker/PCM load failed: "+music->path).c_str());return -1;}
    music->playing=true;return 0;
}
extern "C" int Mix_HaltMusicStream(Mix_Music *music){if(music)close_player(music);return 0;}
extern "C" int Mix_HaltMusic(void){return 0;} // Upstream uses only independent multi-music streams.
extern "C" void Mix_PauseMusicStream(Mix_Music *music){if(music)music->paused=true;}
extern "C" void Mix_ResumeMusicStream(Mix_Music *music){if(music)music->paused=false;}
extern "C" int Mix_PlayingMusicStream(Mix_Music *music){return music&&music->playing;}
extern "C" int Mix_PausedMusicStream(Mix_Music *music){return music&&music->paused;}
extern "C" int Mix_VolumeMusicStream(Mix_Music *music,int volume){if(!music)return -1;int old=music->volume;if(volume>=0)music->volume=std::clamp(volume,0,128);return old;}
extern "C" int Mix_VolumeMusic(int volume){int old=global_volume;if(volume>=0)global_volume=std::clamp(volume,0,128);return old;}
extern "C" int Mix_VolumeMusicGeneral(int volume){return Mix_VolumeMusic(volume);}
extern "C" int Mix_ModMusicStreamJumpToOrder(Mix_Music *music,int order){
    if(!music)return -1;int position=order&65535,row=(unsigned)order>>16;
    music->requested_order=order;
    if(music->tracker&&position==0x98&&row==0){
        close_player(music);
        if(!music->title_samples.empty()){
            // Cached title PCM has the same ownership rule as its cold path:
            // halted game streams retain descriptors, not the parsed XM data.
            for(auto* stream:streams)if(!stream->module&&stream->shared)release_module(stream);
            music->pcm_samples=music->title_samples.size();music->pcm_position=0;music->title_pcm=true;music->playing=true;music->requested_order=order;
            return 0;
        }
        auto path=music->path;auto dot=path.rfind('.');if(dot!=std::string::npos)path.resize(dot);path+="-title.h1pcm";
        FILE* file=fopen(path.c_str(),"rb");unsigned char header[16];
        if(file&&fread(header,1,16,file)==16&&!memcmp(header,"H1PC",4)&&little32(header+4)==1&&little32(header+8)==16000){
            unsigned count=little32(header+12);
            if(count&&count<=16000*600){
                // No title sample needs the parsed tracker payload. Release
                // halted shared players before allocating its cached PCM.
                for(auto* stream:streams)if(!stream->module&&stream->shared)release_module(stream);
                bool valid=music->title_samples.Load(file,count);fclose(file);file=nullptr;
                if(valid){music->pcm_samples=count;music->pcm_position=0;music->title_pcm=true;music->playing=true;music->requested_order=order;
                    pvzp_music_diagnostic[6]++;return 0;}
                music->title_samples.Clear();
            }
        }if(file)fclose(file);
    }
    if(music->module&&music->shared&&music->shared->path!=module_path(music))close_player(music);
    if(!music->module){if(!start_player(music))return -1;music->playing=true;}
    if(xmp_set_position(music->module,position)<=-2)return -1;
    if(xmp_set_row(music->module,row)<0)return -1;
    xmp_play_buffer(music->module,nullptr,0,0);music->requested_order=order;music->pending_order=true;return 0;
}
extern "C" int Mix_ModMusicStreamGetOrder(Mix_Music *music,int *order){
    if(!order)return -1;*order=-1;if(!music)return -1;
    if(music->title_pcm){*order=0x98;return 0;}if(!music->module)return -1;
    if(music->pending_order){*order=music->requested_order;return 0;}
    xmp_frame_info frame{};xmp_get_frame_info(music->module,&frame);*order=(frame.pos&65535)|((frame.row&65535)<<16);return 0;
}
extern "C" int Mix_ModMusicStreamSetChannelVolume(Mix_Music *music,int channel,int volume){
    if(!music||channel<0||channel>=64)return -1;int old=music->channels[channel];music->channels[channel]=std::clamp(volume,0,128);
    if(music->module)xmp_channel_vol(music->module,channel,music->channels[channel]*100/128);return old;
}
extern "C" void pvzp_music_mix(int32_t *output,unsigned count){
#ifdef PVZ_NATIVE_PROFILE
    PVZProfileScope profile(PVZ_PROFILE_MUSIC);
#endif
    if(count>256)return;int16_t samples[256];
    for(Mix_Music *music:streams){
        if(!music->playing||music->paused)continue;unsigned produced=count;
        if(music->tracker&&!music->title_pcm){
            if(!music->module)continue;
            if(xmp_play_buffer(music->module,samples,(int)count*2,music->loops<0?0:1)<0){music->playing=false;continue;}
            music->pending_order=false;
        }else if(music->title_pcm){
            produced=0;
            while(produced<count){unsigned n=std::min(count-produced,music->pcm_samples-music->pcm_position);
                if(music->title_streaming){unsigned got=(unsigned)fread(samples+produced,2,n,music->pcm);if(got!=n){music->playing=false;break;}}
                else music->title_samples.Copy(samples+produced,music->pcm_position,n);produced+=n;music->pcm_position+=n;
                if(music->pcm_position==music->pcm_samples){music->pcm_position=0;if(music->title_streaming)fseek(music->pcm,16,SEEK_SET);}
            }
        }else{
            produced=0;int empty_reads=0;
            while(produced<count){unsigned got=(unsigned)fread(samples+produced,2,count-produced,music->pcm);produced+=got;music->pcm_position+=got;
                if(!got&&++empty_reads>1){music->playing=false;break;}
                if(produced<count){if(music->loops<0&&music->pcm_samples){fseek(music->pcm,16,SEEK_SET);music->pcm_position=0;}else{music->playing=false;break;}}
            }
        }
        int gain=music->volume*global_volume;
        for(unsigned i=0;i<produced;i++)output[i]+=(int32_t)samples[i]*gain/(128*128);
    }
}
extern "C" int pvzp_music_has_audio(){
    for(auto* music:streams)if(music->playing&&!music->paused)return 1;return 0;
}
extern "C" void pvzp_music_shutdown(void){while(!streams.empty())Mix_FreeMusic(streams.back());}
/* Parse the shared game module while the board is loading, before its intro
 * starts. Starting playback later still uses the original orders/channels. */
extern "C" void pvzh1_music_warm_game(){
    // The title PCM and in-game XM share the same descriptor in the game.
    // Acquiring its module also works while title PCM is still playing; it
    // does not replace the player or change its current order.
    for(auto* music:streams)if(music->tracker){
        if(music->title_pcm&&!music->title_streaming&&!music->title_samples.empty()){
            auto path=music->path;auto dot=path.rfind('.');if(dot!=std::string::npos)path.resize(dot);path+="-title.h1pcm";
            FILE* file=fopen(path.c_str(),"rb");unsigned char header[16];
            if(file&&fread(header,1,16,file)==16&&!memcmp(header,"H1PC",4)&&little32(header+4)==1&&little32(header+8)==16000&&little32(header+12)==music->pcm_samples&&fseek(file,16+music->pcm_position*2,SEEK_SET)==0){
                // Preserve playback position while releasing the menu-only
                // 2.79 MB cache before board dependencies compete for RAM.
                music->pcm=file;music->title_streaming=true;music->title_samples.Clear();
            }else if(file)fclose(file);
        }
        const int previous=music->requested_order;music->requested_order=0;
        acquire_module(music);music->requested_order=previous;return;
    }
}
/* Idle title PCM is a useful return-to-menu cache, but less valuable than
 * active game textures if an actual heap allocation needs its span. */
extern "C" size_t pvzh1_music_release_idle(size_t){
    for(auto* music:streams)if(!music->playing&&!music->title_pcm&&!music->title_samples.busy()&&!music->title_samples.empty()){
        const size_t bytes=music->title_samples.capacity_bytes();
        music->title_samples.Clear();return bytes;
    }
    return 0;
}
#ifdef PVZ_HOST_RUNNER
extern "C" unsigned pvzh1_music_title_position(Mix_Music* music){return music->pcm_position;}
extern "C" unsigned pvzh1_music_title_streaming(Mix_Music* music){return music->title_streaming&&!music->title_samples.capacity_bytes();}
#endif
