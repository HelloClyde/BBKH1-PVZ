/* Instrumented host-only operation counts. SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "Resources.h"
#include "PvzpLib/Definition.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpParticle.h"
#include "graphics/MemoryImageCache.h"
#include "sound/SoundManager.h"
#include "sound/SoundInstance.h"
#include "h1_host.h"
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <filesystem>
#include <fstream>
extern "C" {int pvzh1_audit_drain=0;}
extern "C" void pvzh1_sound_warmup();
static unsigned opens,seeks,reads,closes;
static unsigned long long recounts,visits;
extern "C" FILE* pvzh1_audit_fopen(const char* p,const char* m){++opens;return std::fopen(p,m);}
extern "C" int pvzh1_audit_fseek(FILE* f,long p,int w){++seeks;return std::fseek(f,p,w);}
extern "C" size_t pvzh1_audit_fread(void* b,size_t n,size_t c,FILE* f){++reads;return std::fread(b,n,c,f);}
extern "C" int pvzh1_audit_fclose(FILE* f){++closes;return std::fclose(f);}
extern "C" void pvzh1_audit_recount(std::size_t n){++recounts;visits+=n;}
static void Sound(std::ofstream& out,const char* name,intptr_t id){
    opens=seeks=reads=closes=0;pvzh1_audit_drain=1;
    for(int i=0;i<20;i++){
        auto* sound=gLawnApp->mSoundManager->GetSoundInstance(id);assert(sound&&sound->Play(false,false));
        for(int j=0;j<16;j++)pvzp_audio_pump();sound->Release();
    }
    pvzh1_audit_drain=0;
    out<<'"'<<name<<"\":{\"plays\":20,\"opens\":"<<opens<<",\"seeks\":"<<seeks<<",\"reads\":"<<reads<<",\"closes\":"<<closes<<'}';
}
extern "C" void pvzh1_work_audit(){
    assert(gLawnApp&&gReanimatorDefArray&&gParticleDefArray);
    ReanimatorDefinition raw{};assert(DefinitionLoadXML("reanim/SelectorScreen.reanim",&gReanimatorDefMap,&raw));
    unsigned transforms=0,missing=0;
    for(int i=0;i<raw.mTracks.count;i++)for(int j=0;j<raw.mTracks.tracks[i].mTransforms.count;j++){
        const auto& t=raw.mTracks.tracks[i].mTransforms.mTransforms[j];
        transforms++;const float fields[]={t.mTransX,t.mTransY,t.mSkewX,t.mSkewY,t.mScaleX,t.mScaleY,t.mFrame,t.mAlpha};
        for(float v:fields)if(v==DEFAULT_FIELD_PLACEHOLDER)missing++;
    }
    std::ofstream out(std::filesystem::path(std::getenv("PVZ_HOST_OUTPUT"))/"runtime-work.json");
    pvzh1_sound_warmup();
    out<<"{\"sounds\":{";Sound(out,"throw",Sexy::SOUND_THROW);out<<',';Sound(out,"splat",Sexy::SOUND_SPLAT);out<<',';Sound(out,"cached_dirt_rise",Sexy::SOUND_DIRT_RISE);out<<"},";
    (void)Sexy::H1ImageCacheGetStats();
    recounts=visits=0;for(int i=0;i<100;i++)Sexy::H1ImageCacheEndFrame();
    out<<"\"cache_accounting\":{\"frame_end_calls\":100,\"recounts\":"<<recounts<<",\"source_visits\":"<<visits<<"},";
    out<<"\"startup\":{\"particle_definitions\":"<<gParticleDefCount<<",\"selector_tracks\":"<<raw.mTracks.count<<",\"selector_transforms\":"<<transforms<<",\"sparse_float_fields_before_inheritance\":"<<missing<<"}}\n";
    out.close();ReanimationFreeDefinition(&raw);
}
