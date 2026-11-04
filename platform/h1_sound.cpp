/* LGPL-3.0-or-later. Streaming original sound effects, retaining upstream ABI. */
#include "sound/SDLSoundManager.h"
#include "sound/SDLSoundInstance.h"
#include "h1_host.h"
#ifdef PVZ_NATIVE_PROFILE
#include "profiler.h"
#endif
#include <stdio.h>
#include <cmath>
#include <algorithm>
#include <map>
#include <string>
#include <zlib.h>
#include <vector>
extern "C" int pvzp_pcm_open();
extern "C" unsigned pvzp_pcm_writable();
extern "C" void pvzp_pcm_write(const int16_t*,unsigned);
extern "C" void pvzp_music_mix(int32_t*,unsigned);
extern "C" int pvzp_music_has_audio();
extern "C" void pvzp_pcm_idle() __attribute__((weak));
extern "C" unsigned pvzp_pcm_pending_tail() __attribute__((weak));
extern "C" void pvzp_pcm_buffer_target(unsigned) __attribute__((weak));
using namespace Sexy;
struct Voice{FILE *file=nullptr;const int16_t* cached=nullptr;Mix_Chunk *chunk=nullptr;uint64_t position=0;unsigned step=65536;
    unsigned buffer_start=0,buffer_count=0;int16_t buffer[512];int volume=256;bool loop=false;SDLSoundInstance *owner=nullptr;};
static Voice voices[MAX_CHANNELS];
static std::map<Mix_Chunk*,std::vector<int16_t>> cached_menu_sounds;
static bool pcm_ready;
static bool audio_pumping;
static bool audio_loading=true;
static uint32_t last_service;
static bool has_audio_sources(){
    if(pvzp_music_has_audio())return true;
    for(const auto& voice:voices)if(voice.file||voice.cached)return true;
    return false;
}
extern "C" int pvzp_audio_is_loading(){return audio_loading;}
extern "C" void pvzp_audio_loading_begin(){
    if(!pcm_ready||audio_pumping)return;
    audio_loading=true;
    if(pvzp_pcm_buffer_target)pvzp_pcm_buffer_target(32768);
    for(unsigned i=0;i<8;i++)pvzp_audio_pump();
    // A paused song can leave a short audible tail queued. Finish that tail
    // before entering a synchronous silent task instead of letting it wrap.
    if(!has_audio_sources()&&pvzp_pcm_pending_tail){
        const uint32_t start=pvzp_ticks_ms();
        while(pvzp_pcm_pending_tail()&&pvzp_ticks_ms()-start<1100)pvzp_audio_pump();
        if(pvzp_pcm_idle)pvzp_pcm_idle();
    }
}
extern "C" void pvzp_audio_loading_end(){audio_loading=false;if(pvzp_pcm_buffer_target)pvzp_pcm_buffer_target(8192);pvzp_audio_pump();}
extern "C" void pvzp_audio_service(){
    if(!pcm_ready||audio_pumping)return;
    uint32_t now=pvzp_ticks_ms();if(now-last_service<25)return;
    last_service=now;pvzp_audio_pump();
}
static unsigned u32le(const unsigned char *p){return p[0]|p[1]<<8|p[2]<<16|p[3]<<24;}
static std::map<std::string,unsigned> sound_index;
extern "C" { volatile uint32_t pvzh1_sound_index_diagnostic[4]; }
static bool sound_index_attempted;
static void load_sound_index(){
    if(sound_index_attempted)return;sound_index_attempted=true;
    FILE* f=fopen(GetResourcePath("sounds/index.h1si").c_str(),"rb");if(!f)return;
    unsigned char header[16];bool ok=fread(header,1,16,f)==16&&!memcmp(header,"H1SI",4)&&u32le(header+4)==1;
    unsigned size=ok?u32le(header+8):0;ok=ok&&size&&size<=131072;std::vector<unsigned char> data;
    if(ok){data.resize(size);ok=fread(data.data(),1,size,f)==size&&fgetc(f)==EOF&&crc32(0,data.data(),size)==u32le(header+12);}fclose(f);if(!ok)return;
    unsigned at=0;while(at+8<=size){unsigned length=u32le(data.data()+at),samples=u32le(data.data()+at+4);at+=8;
        if(!length||length>512||at+length>size){sound_index.clear();return;}
        sound_index.emplace(std::string((const char*)data.data()+at,length),samples);at+=length;
    }if(at!=size)sound_index.clear();pvzh1_sound_index_diagnostic[0]=sound_index.size();
}
static Mix_Chunk *open_chunk(const std::string& filename){
    std::string path=filename;size_t dot=path.rfind('.');if(dot!=std::string::npos&&path.find('/',dot)==std::string::npos)path.resize(dot);
    path+=".h1pcm";load_sound_index();auto found=sound_index.find(path);
    unsigned samples=0;if(found!=sound_index.end())samples=found->second;
    path=GetResourcePath(path);
    if(found!=sound_index.end()){++pvzh1_sound_index_diagnostic[1];auto* chunk=new Mix_Chunk{};SDL_strlcpy(chunk->path,path.c_str(),sizeof(chunk->path));chunk->rate=16000;chunk->samples=samples;return chunk;}
    ++pvzh1_sound_index_diagnostic[2];
    FILE *f=fopen(path.c_str(),"rb");if(!f)return nullptr;
    unsigned char b[16];bool ok=fread(b,1,16,f)==16&&!memcmp(b,"H1PC",4)&&u32le(b+4)==1&&u32le(b+8)==16000;fclose(f);
    if(!ok)return nullptr;auto *chunk=new Mix_Chunk{};SDL_strlcpy(chunk->path,path.c_str(),sizeof(chunk->path));chunk->rate=16000;chunk->samples=u32le(b+12);return chunk;
}
static void stop_voice(int n){if(n<0||n>=MAX_CHANNELS)return;Voice &v=voices[n];if(v.file)fclose(v.file);v=Voice{};}
static bool voice_active(int n,const SDLSoundInstance *self){return n>=0&&n<MAX_CHANNELS&&(voices[n].file||voices[n].cached)&&voices[n].owner==self;}
extern "C" void pvzp_audio_pump(){
#ifdef PVZ_NATIVE_PROFILE
    PVZProfileScope profile(PVZ_PROFILE_AUDIO);
#endif
    if(!pcm_ready||audio_pumping)return;audio_pumping=true;
    struct Guard{~Guard(){audio_pumping=false;}} guard;
    // Drain a real tail, then stop submitting an empty looping descriptor.
    // Silent loading must not leave old samples repeating around the ring.
    if(!has_audio_sources()){if(pvzp_pcm_idle)pvzp_pcm_idle();return;}
    unsigned count=pvzp_pcm_writable();if(count<128)return;
    // After a blocking load, replace the entire consumed interval. Limiting
    // this to 128 ms lets successive long tasks drain even a primed ring.
    if(!audio_loading&&count>2048)count=2048;
    while(count){unsigned frames=std::min(count,256u);int32_t mix[256]={0};int16_t output[256];
        for(int channel=0;channel<MAX_CHANNELS;channel++){
            Voice &v=voices[channel];if(!v.file&&!v.cached)continue;
            for(unsigned i=0;i<frames;i++){
                unsigned position=(unsigned)(v.position>>16);
                if(position>=v.chunk->samples){
                    if(v.loop&&v.chunk->samples){v.position%=((uint64_t)v.chunk->samples<<16);position=(unsigned)(v.position>>16);}
                    else {if(v.file)fclose(v.file);v.file=nullptr;v.cached=nullptr;break;}
                }
                if(!v.cached&&(position<v.buffer_start||position>=v.buffer_start+v.buffer_count)){
                    if(fseek(v.file,16+(long)position*2,SEEK_SET)){fclose(v.file);v.file=nullptr;break;}
                    v.buffer_start=position;v.buffer_count=(unsigned)fread(v.buffer,2,512,v.file);
                    if(!v.buffer_count){fclose(v.file);v.file=nullptr;break;}
                }
                const int sample=v.cached?v.cached[position]:v.buffer[position-v.buffer_start];
                mix[i]+=(int32_t)sample*v.volume/256;v.position+=v.step;
            }
        }
        pvzp_music_mix(mix,frames);
        for(unsigned i=0;i<frames;i++)output[i]=(int16_t)std::clamp(mix[i],-32768,32767);
        pvzp_pcm_write(output,frames);count-=frames;
    }
}
SDLSoundManager::SDLSoundManager():mInitializedMixer(false),mMasterVolume(1.0),mLastReleaseTick(0),mMixerFreq(16000),mMixerFormat(AUDIO_S16SYS),mMixerChannels(1){
    for(int i=0;i<MAX_SOURCE_SOUNDS;i++){mSourceSounds[i]=nullptr;mBaseVolumes[i]=1;mBasePans[i]=0;}
    pcm_ready=pvzp_pcm_open()!=0;mInitializedMixer=pcm_ready;
}
SDLSoundManager::~SDLSoundManager(){ReleaseChannels();ReleaseSounds();pvzp_audio_close();pcm_ready=false;sound_index.clear();sound_index_attempted=false;}
bool SDLSoundManager::Initialized(){return mInitializedMixer;}
bool SDLSoundManager::LoadSound(intptr_t id,const std::string& name){
    if(id<0||id>=MAX_SOURCE_SOUNDS)return false;if(mSourceSounds[id])return true;
    Mix_Chunk *c=open_chunk(name);if(!c){pvzp_log_line(("Missing converted sound: "+name).c_str());return false;}
    std::string key=name;for(auto& ch:key)if(ch>='A'&&ch<='Z')ch+=32;
    if(key=="sounds/losemusic"||key=="sounds/evillaugh"||key=="sounds/dirt_rise"){
        FILE* file=fopen(c->path,"rb");if(file){
            auto& samples=cached_menu_sounds[c];samples.resize(c->samples);
            bool ok=fseek(file,16,SEEK_SET)==0&&fread(samples.data(),2,c->samples,file)==c->samples;fclose(file);
            if(!ok)cached_menu_sounds.erase(c);
        }
    }
    mSourceSounds[id]=c;mSourceFileNames[id]=name;return true;
}
intptr_t SDLSoundManager::LoadSound(const std::string& name){for(int i=0;i<MAX_SOURCE_SOUNDS;i++)if(mSourceFileNames[i]==name)return i;
    intptr_t id=GetFreeSoundId();return id>=0&&LoadSound(id,name)?id:-1;}
void SDLSoundManager::ReleaseSound(intptr_t id){if(id<0||id>=MAX_SOURCE_SOUNDS)return;auto *c=mSourceSounds[id].load();
    for(int i=0;i<MAX_CHANNELS;i++)if(voices[i].chunk==c)stop_voice(i);cached_menu_sounds.erase(c);delete c;mSourceSounds[id]=nullptr;mSourceFileNames[id].clear();}
void SDLSoundManager::SetVolume(double volume){mMasterVolume=volume;for(auto &s:mPlayingSounds)if(s)s->RehupVolume();}
bool SDLSoundManager::SetBaseVolume(intptr_t id,double volume){if(id<0||id>=MAX_SOURCE_SOUNDS)return false;mBaseVolumes[id]=volume;return true;}
bool SDLSoundManager::SetBasePan(intptr_t id,int pan){if(id<0||id>=MAX_SOURCE_SOUNDS)return false;mBasePans[id]=pan;return true;}
SoundInstance *SDLSoundManager::GetSoundInstance(intptr_t id){
    if(id<0||id>=MAX_SOURCE_SOUNDS||!mSourceSounds[id])return nullptr;int channel=FindFreeChannel();if(channel<0)return nullptr;
    mPlayingSounds[channel]=std::make_unique<SDLSoundInstance>(this,mSourceSounds[id],channel);
    auto *s=mPlayingSounds[channel].get();s->SetBaseVolume(mBaseVolumes[id]);s->SetBasePan(mBasePans[id]);return s;
}
int SDLSoundManager::FindFreeChannel(){for(int i=0;i<MAX_CHANNELS;i++)if(!mPlayingSounds[i]||mPlayingSounds[i]->IsReleased())return i;return -1;}
void SDLSoundManager::ReleaseSounds(){for(int i=0;i<MAX_SOURCE_SOUNDS;i++)ReleaseSound(i);}
void SDLSoundManager::ReleaseChannels(){for(auto &s:mPlayingSounds)s.reset();}
double SDLSoundManager::GetMasterVolume(){return mMasterVolume;}
void SDLSoundManager::SetMasterVolume(double volume){SetVolume(volume);}
void SDLSoundManager::Flush(){pvzp_audio_pump();}
void SDLSoundManager::StopAllSounds(){for(auto &s:mPlayingSounds)if(s)s->Stop();}
intptr_t SDLSoundManager::GetFreeSoundId(){for(int i=0;i<MAX_SOURCE_SOUNDS;i++)if(mSourceFileNames[i].empty())return i;return -1;}
int SDLSoundManager::GetNumSounds(){int n=0;for(const auto &name:mSourceFileNames)if(!name.empty())n++;return n;}
bool SDLSoundManager::LoadAUSound(intptr_t id,const std::string& s){return LoadSound(id,s);}
bool SDLSoundManager::DecodeSound(intptr_t id,const std::string& s){return LoadSound(id,s);}
void SDLSoundManager::ReleaseFreeChannels(){for(auto &s:mPlayingSounds)if(s&&s->IsReleased())s.reset();}

SDLSoundInstance::SDLSoundInstance(SDLSoundManager *manager,Mix_Chunk *chunk,int channel):
 mSoundManagerP(manager),mMixChunk(chunk),mAutoRelease(false),mHasPlayed(false),mReleased(false),mReservedChannel(channel),mChannel(-1),
 mBasePan(0),mBaseVolume(1),mPan(0),mVolume(1),mPitch(1),mPitchHandler{},mDefaultFrequency(16000),mDemoEndUpdateCount(0),mDemoLooping(false){}
SDLSoundInstance::~SDLSoundInstance(){Stop();}
void SDLSoundInstance::Release(){Stop();mReleased=true;}
void SDLSoundInstance::SetBaseVolume(double v){mBaseVolume=v;RehupVolume();}
void SDLSoundInstance::SetBasePan(int p){mBasePan=p;}
void SDLSoundInstance::AdjustPitch(double steps){mPitch=(float)std::pow(2.0,steps/12.0);if(voice_active(mChannel,this))voices[mChannel].step=(unsigned)std::clamp(mPitch*65536.0f,8192.0f,524288.0f);}
void SDLSoundInstance::SetVolume(double v){mVolume=v;RehupVolume();}
void SDLSoundInstance::SetPan(int p){mPan=p;}
bool SDLSoundInstance::Play(bool loop,bool automatic){
    Stop();if(!mMixChunk||mReservedChannel<0)return false;
    auto cached=cached_menu_sounds.find(mMixChunk);FILE* file=nullptr;
    if(cached==cached_menu_sounds.end()){file=fopen(mMixChunk->path,"rb");if(!file)return false;if(fseek(file,16,SEEK_SET)){fclose(file);return false;}}
    mChannel=mReservedChannel;stop_voice(mChannel);Voice &v=voices[mChannel];
    if(cached!=cached_menu_sounds.end())v.cached=cached->second.data();
    v.file=file;v.chunk=mMixChunk;v.position=0;v.step=(unsigned)std::clamp(mPitch*65536.0f,8192.0f,524288.0f);
    v.loop=loop;v.owner=this;mHasPlayed=true;mReleased=false;mAutoRelease=automatic;RehupVolume();return true;
}
void SDLSoundInstance::Stop(){if(voice_active(mChannel,this))stop_voice(mChannel);mChannel=-1;}
bool SDLSoundInstance::IsPlaying(){return voice_active(mChannel,this);}
bool SDLSoundInstance::IsReleased(){return mReleased||(mAutoRelease&&mHasPlayed&&!IsPlaying());}
double SDLSoundInstance::GetVolume(){return mVolume;}
void SDLSoundInstance::RehupVolume(){if(voice_active(mChannel,this))voices[mChannel].volume=(int)std::clamp(mVolume*mBaseVolume*mSoundManagerP->mMasterVolume*256.0,0.0,256.0);}
void SDLSoundInstance::RehupPan(){} // Native output is mono.
uint16_t SDLSoundInstance::FormatSampleSize(uint16_t){return 2;}
int SDLSoundInstance::GetChunkDurationMS(int bytes){return bytes*1000/32000;}
void SDLSoundInstance::CreateSoundPitchHandler(const Mix_Chunk*,const float*,int,int){}
void SDLSoundInstance::PitchHandlerFuncCallback(int,void*,int,void*){}
