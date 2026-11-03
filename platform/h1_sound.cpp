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
extern "C" int pvzp_pcm_open();
extern "C" unsigned pvzp_pcm_writable();
extern "C" void pvzp_pcm_write(const int16_t*,unsigned);
extern "C" void pvzp_music_mix(int32_t*,unsigned);
using namespace Sexy;
struct Voice{FILE *file=nullptr;Mix_Chunk *chunk=nullptr;uint64_t position=0;unsigned step=65536;
    unsigned buffer_start=0,buffer_count=0;int16_t buffer[512];int volume=256;bool loop=false;SDLSoundInstance *owner=nullptr;};
static Voice voices[MAX_CHANNELS];
static bool pcm_ready;
static unsigned u32le(const unsigned char *p){return p[0]|p[1]<<8|p[2]<<16|p[3]<<24;}
static Mix_Chunk *open_chunk(const std::string& filename){
    std::string path=filename;size_t dot=path.rfind('.');if(dot!=std::string::npos&&path.find('/',dot)==std::string::npos)path.resize(dot);
    path=GetResourcePath(path+".h1pcm");FILE *f=fopen(path.c_str(),"rb");if(!f)return nullptr;
    unsigned char b[16];bool ok=fread(b,1,16,f)==16&&!memcmp(b,"H1PC",4)&&u32le(b+4)==1&&u32le(b+8)==16000;fclose(f);
    if(!ok)return nullptr;auto *chunk=new Mix_Chunk{};SDL_strlcpy(chunk->path,path.c_str(),sizeof(chunk->path));chunk->rate=16000;chunk->samples=u32le(b+12);return chunk;
}
static void stop_voice(int n){if(n<0||n>=MAX_CHANNELS)return;Voice &v=voices[n];if(v.file)fclose(v.file);v=Voice{};}
static bool voice_active(int n,const SDLSoundInstance *self){return n>=0&&n<MAX_CHANNELS&&voices[n].file&&voices[n].owner==self;}
extern "C" void pvzp_audio_pump(){
#ifdef PVZ_NATIVE_PROFILE
    PVZProfileScope profile(PVZ_PROFILE_AUDIO);
#endif
    if(!pcm_ready)return;unsigned count=pvzp_pcm_writable();if(count<128)return;
    if(count>2048)count=2048; // 128 ms maximum refill per service invocation.
    while(count){unsigned frames=std::min(count,256u);int32_t mix[256]={0};int16_t output[256];
        for(int channel=0;channel<MAX_CHANNELS;channel++){
            Voice &v=voices[channel];if(!v.file)continue;
            for(unsigned i=0;i<frames;i++){
                unsigned position=(unsigned)(v.position>>16);
                if(position>=v.chunk->samples){
                    if(v.loop&&v.chunk->samples){v.position%=((uint64_t)v.chunk->samples<<16);position=(unsigned)(v.position>>16);}
                    else {fclose(v.file);v.file=nullptr;break;}
                }
                if(position<v.buffer_start||position>=v.buffer_start+v.buffer_count){
                    if(fseek(v.file,16+(long)position*2,SEEK_SET)){fclose(v.file);v.file=nullptr;break;}
                    v.buffer_start=position;v.buffer_count=(unsigned)fread(v.buffer,2,512,v.file);
                    if(!v.buffer_count){fclose(v.file);v.file=nullptr;break;}
                }
                mix[i]+=(int32_t)v.buffer[position-v.buffer_start]*v.volume/256;v.position+=v.step;
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
SDLSoundManager::~SDLSoundManager(){ReleaseChannels();ReleaseSounds();pvzp_audio_close();pcm_ready=false;}
bool SDLSoundManager::Initialized(){return mInitializedMixer;}
bool SDLSoundManager::LoadSound(intptr_t id,const std::string& name){
    if(id<0||id>=MAX_SOURCE_SOUNDS)return false;if(mSourceSounds[id])return true;
    Mix_Chunk *c=open_chunk(name);if(!c){pvzp_log_line(("Missing converted sound: "+name).c_str());return false;}
    mSourceSounds[id]=c;mSourceFileNames[id]=name;return true;
}
intptr_t SDLSoundManager::LoadSound(const std::string& name){for(int i=0;i<MAX_SOURCE_SOUNDS;i++)if(mSourceFileNames[i]==name)return i;
    intptr_t id=GetFreeSoundId();return id>=0&&LoadSound(id,name)?id:-1;}
void SDLSoundManager::ReleaseSound(intptr_t id){if(id<0||id>=MAX_SOURCE_SOUNDS)return;auto *c=mSourceSounds[id].load();
    for(int i=0;i<MAX_CHANNELS;i++)if(voices[i].chunk==c)stop_voice(i);delete c;mSourceSounds[id]=nullptr;mSourceFileNames[id].clear();}
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
    Stop();if(!mMixChunk||mReservedChannel<0)return false;FILE *file=fopen(mMixChunk->path,"rb");if(!file)return false;
    if(fseek(file,16,SEEK_SET)){fclose(file);return false;}mChannel=mReservedChannel;stop_voice(mChannel);Voice &v=voices[mChannel];
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
