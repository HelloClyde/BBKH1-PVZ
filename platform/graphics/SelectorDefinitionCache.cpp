/* Selective retirement of the original, reloadable SelectorScreen definition.
 * String ownership is captured before original missing-frame inheritance.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

/* Starts, ready generations, collections, captured strings, freed strings,
 * current owned strings, loading, ready, disabled, latest freed payload. */
extern "C" { volatile uint32_t pvzh1_selector_definition_diagnostic[10]; }
extern "C" { volatile uint32_t pvzh1_definition_retirement_diagnostic[4]; }
namespace {
struct DefinitionOwners {DefinitionOwners* next;ReanimatorDefinition* definition;const char** strings;unsigned count;std::size_t bytes;};
DefinitionOwners* definitions=nullptr;
void CaptureDefinition(ReanimatorDefinition* definition){
    for(auto* p=definitions;p;p=p->next)if(p->definition==definition)return;
    unsigned capacity=definition->mTracks.count;
    std::size_t bytes=capacity*sizeof(ReanimatorTrack);
    for(int t=0;t<definition->mTracks.count;++t){const auto& track=definition->mTracks.tracks[t];bytes+=track.mTransforms.count*sizeof(ReanimatorTransform);
        for(int f=0;f<track.mTransforms.count;++f)if(*track.mTransforms.mTransforms[f].mText)++capacity;}
    auto* list=new(std::nothrow) const char*[capacity];if(!list)return;
    auto* record=new(std::nothrow) DefinitionOwners{definitions,definition,list,0,bytes};if(!record){delete[] list;return;}
    auto capture=[&](const char* s){if(!s||!*s)return;for(unsigned i=0;i<record->count;++i)if(list[i]==s)return;list[record->count++]=s;record->bytes+=std::strlen(s)+1;};
    // This hook runs BEFORE the original missing-frame inheritance. Pointers
    // are exactly the owned string fields produced by the original loader.
    for(int t=0;t<definition->mTracks.count;++t){const auto& track=definition->mTracks.tracks[t];capture(track.mName);for(int f=0;f<track.mTransforms.count;++f)capture(track.mTransforms.mTransforms[f].mText);}
    definitions=record;++pvzh1_definition_retirement_diagnostic[3];
}
void ReleaseDefinition(ReanimatorDefinition* definition){
    auto** link=&definitions;while(*link&&(*link)->definition!=definition)link=&(*link)->next;
    if(auto* p=*link){*link=p->next;for(unsigned i=0;i<p->count;++i)delete[] const_cast<char*>(p->strings[i]);
        ++pvzh1_definition_retirement_diagnostic[0];pvzh1_definition_retirement_diagnostic[1]+=p->bytes;--pvzh1_definition_retirement_diagnostic[3];delete[] p->strings;delete p;}
    *definition=ReanimatorDefinition{};
}
struct Owners {
    ReanimatorDefinition *definition=nullptr;
    const char *strings[512]{};
    unsigned count=0;
    std::size_t arrayBytes=0;
    bool loading=false,parsed=false,ready=false,disabled=false,collecting=false;
} owners;
ReanimatorDefinition *Selector(){
    const unsigned index=(unsigned)ReanimationType::REANIM_SELECTOR_SCREEN;
    return gReanimatorDefArray&&gReanimatorDefCount>index?&gReanimatorDefArray[index]:nullptr;
}
void Status(){
    pvzh1_selector_definition_diagnostic[5]=owners.count;
    pvzh1_selector_definition_diagnostic[6]=owners.loading;
    pvzh1_selector_definition_diagnostic[7]=owners.ready;
    pvzh1_selector_definition_diagnostic[8]=owners.disabled;
}
void Capture(const char *value){
    if(!value||!*value||owners.disabled)return;
    for(unsigned i=0;i<owners.count;++i)if(owners.strings[i]==value)return;
    if(owners.count==512){owners.disabled=true;return;}
    owners.strings[owners.count++]=value;
    pvzh1_selector_definition_diagnostic[3]=pvzh1_selector_definition_diagnostic[3]+1;
}
}
void H1SelectorLoadBegin(ReanimatorDefinition *definition){
    if(!definition||definition!=Selector())return;
    /* A legitimate reload follows original FreeDefinition, which clears this
     * table. Do not overwrite unknown prior-generation ownership. */
    if(owners.count)owners.disabled=true;
    else {owners.definition=definition;owners.disabled=false;owners.arrayBytes=0;}
    owners.loading=true;owners.parsed=false;owners.ready=false;
    pvzh1_selector_definition_diagnostic[0]=pvzh1_selector_definition_diagnostic[0]+1;Status();
}
void H1SelectorParsed(ReanimatorDefinition *definition){
    if(definition!=Selector()){CaptureDefinition(definition);return;}
    if(definition!=owners.definition||!owners.loading||owners.disabled)return;
    if(definition->mTracks.count<0||(definition->mTracks.count&&!definition->mTracks.tracks)){owners.disabled=true;Status();return;}
    owners.arrayBytes=(std::size_t)definition->mTracks.count*sizeof(ReanimatorTrack);
    for(int t=0;t<definition->mTracks.count;++t){
        const ReanimatorTrack& track=definition->mTracks.tracks[t];Capture(track.mName);
        if(track.mTransforms.count<0||(track.mTransforms.count&&!track.mTransforms.mTransforms)){owners.disabled=true;break;}
        owners.arrayBytes+=(std::size_t)track.mTransforms.count*sizeof(ReanimatorTransform);
        for(int f=0;f<track.mTransforms.count;++f)Capture(track.mTransforms.mTransforms[f].mText);
    }
    owners.parsed=!owners.disabled;Status();
}
void H1SelectorLoadReady(ReanimatorDefinition *definition){
    if(definition!=owners.definition)return;
    owners.loading=false;owners.ready=owners.parsed&&!owners.disabled;
    if(owners.ready)pvzh1_selector_definition_diagnostic[1]=pvzh1_selector_definition_diagnostic[1]+1;
    Status();
}
void H1SelectorLoadFailed(ReanimatorDefinition *definition){
    if(definition!=owners.definition)return;
    owners.loading=false;owners.ready=false;owners.disabled=true;Status();
}
void H1SelectorFreed(ReanimatorDefinition *definition){
    if(definition!=Selector()){ReleaseDefinition(definition);return;}
    if(!definition||definition!=owners.definition)return;
    /* Original FreeMap has finished traversing arrays and clearing strings.
     * Each captured allocation is ours exactly once, independent of aliases. */
    std::size_t bytes=owners.arrayBytes;
    for(unsigned i=0;i<owners.count;++i){
        bytes+=std::strlen(owners.strings[i])+1;
        delete[] const_cast<char*>(owners.strings[i]);owners.strings[i]=nullptr;
        pvzh1_selector_definition_diagnostic[4]=pvzh1_selector_definition_diagnostic[4]+1;
    }
    owners.count=0;owners.arrayBytes=0;owners.definition=nullptr;
    owners.loading=owners.parsed=owners.ready=owners.disabled=false;
    *definition=ReanimatorDefinition{}; // Original FreeArray does not clear count.
    pvzh1_selector_definition_diagnostic[9]=(uint32_t)bytes;Status();
}
void H1ReanimationDefinitionsTrimForMenu(){
    if(!gLawnApp||!gLawnApp->mEffectSystem||!gLawnApp->mEffectSystem->mReanimationHolder)return;
    auto* p=definitions;
    while(p){auto* next=p->next;bool used=false;
        for(auto* animation:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)if(animation->mDefinition==p->definition){used=true;break;}
        if(!used)ReanimationFreeDefinition(p->definition);else ++pvzh1_definition_retirement_diagnostic[2];
        p=next;
    }
}
extern "C" void pvzh1_selector_definition_trim(void*){
    if(owners.collecting||owners.loading||!owners.ready||owners.disabled||!gLawnApp||
       !gLawnApp->mLoadingThreadCompleted||!gLawnApp->mBoard||gLawnApp->mGameSelector||gLawnApp->mCreditScreen||
       gLawnApp->mGameScene==GameScenes::SCENE_CREDIT||!gLawnApp->mEffectSystem||
       !gLawnApp->mEffectSystem->mReanimationHolder)return;
    ReanimatorDefinition *definition=Selector();
    if(!definition||definition!=owners.definition)return;
    /* DataArray traversal includes dead entries until original deletion. */
    for(Reanimation *animation:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)
        if(animation->mDefinition==definition)return;
    owners.collecting=true;owners.ready=false;Status();
    ReanimationFreeDefinition(definition); // Tail hook frees captured strings/reset.
    *definition=ReanimatorDefinition{};
    owners.collecting=false;
    pvzh1_selector_definition_diagnostic[2]=pvzh1_selector_definition_diagnostic[2]+1;
}
