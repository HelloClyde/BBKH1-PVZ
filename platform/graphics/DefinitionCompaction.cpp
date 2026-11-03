/* Retire XML array growth capacity before a reanimation is first published.
 * Every frame/track and its image/font/string pointer is copied verbatim.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "PvzpLib/Reanimator.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

/* Calls, transform arrays, track arrays, actual target bytes saved, failed
 * optional allocations, largest new array, frames examined, tracks examined. */
extern "C" { volatile uint32_t pvzh1_definition_compaction_diagnostic[8]; }
#ifdef PVZH1_DEFINITION_COMPACTION_TEST
extern char *pvzh1_compaction_test_allocate(std::size_t);
#endif
namespace {
template<class T> bool Tighten(T *&pointer,int count,unsigned metric)
{
    static_assert(std::is_trivially_copyable_v<T>);
    if(count<=1)return count==0||pointer!=nullptr;
    if(!pointer)return false;
    const auto n=(std::size_t)count;
    if(n>(std::numeric_limits<std::size_t>::max()/sizeof(T)))return false;
    std::size_t capacity=1;
    while(capacity<n){
        if(capacity>std::numeric_limits<std::size_t>::max()/2)return false;
        capacity*=2;
    }
    if(capacity==n)return true;
    const auto bytes=n*sizeof(T);
#ifdef PVZH1_DEFINITION_COMPACTION_TEST
    char *replacement=pvzh1_compaction_test_allocate(bytes);
#else
    char *replacement=new(std::nothrow) char[bytes];
#endif
    if(!replacement)return false;
    std::memcpy(replacement,pointer,bytes);
    char *previous=reinterpret_cast<char*>(pointer);
    pointer=reinterpret_cast<T*>(replacement);
    delete[] previous; // Matches upstream DefinitionAlloc's raw new[] storage.
    ++pvzh1_definition_compaction_diagnostic[metric];
    pvzh1_definition_compaction_diagnostic[3]+=(uint32_t)((capacity-n)*sizeof(T));
    if(bytes>pvzh1_definition_compaction_diagnostic[5])
        pvzh1_definition_compaction_diagnostic[5]=(uint32_t)bytes;
    return true;
}
}

/* First successful XML load only: before FillInMissingData, atlas creation or
 * any Reanimation borrower. This is not a live-definition relocation API. */
bool H1CompactReanimationDefinition(ReanimatorDefinition& definition)
{
    ++pvzh1_definition_compaction_diagnostic[0];
    if(definition.mReanimAtlas||definition.mTracks.count<0||
       (definition.mTracks.count&&!definition.mTracks.tracks))return false;
    for(int t=0;t<definition.mTracks.count;++t){
        ReanimatorTrack& track=definition.mTracks.tracks[t];
        if(track.mTransforms.count<0||!Tighten(track.mTransforms.mTransforms,track.mTransforms.count,1)){
            ++pvzh1_definition_compaction_diagnostic[4];return false;
        }
        pvzh1_definition_compaction_diagnostic[6]+=(uint32_t)track.mTransforms.count;
    }
    pvzh1_definition_compaction_diagnostic[7]+=(uint32_t)definition.mTracks.count;
    if(!Tighten(definition.mTracks.tracks,definition.mTracks.count,2)){
        ++pvzh1_definition_compaction_diagnostic[4];return false;
    }
    return true;
}
