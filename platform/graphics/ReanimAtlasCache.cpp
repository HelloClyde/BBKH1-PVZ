/* Reclaim reconstructible atlas caches without changing original animations.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/ReanimAtlas.h"
#include "graphics/MemoryImage.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace {
uint64_t freedAtlases=0;
size_t freedPixelBytes=0;
size_t ImageBytes(const Sexy::MemoryImage *image){
    if(!image)return 0;
    const size_t area=(size_t)image->mWidth*image->mHeight;
    return (image->mBits?area*4+4:0)+(image->mColorIndices?area:0)
        +(image->mColorTable?1024:0)+(image->mNativeAlphaData?area*4:0)
        +(image->mRLAlphaData?area:0)+(image->mRLAdditiveData?area:0);
}
}

extern "C" void pvzh1_reanim_atlas_cache_stats(uint64_t *count,size_t *bytes){
    if(count)*count=freedAtlases;if(bytes)*bytes=freedPixelBytes;
}

extern "C" void pvzh1_reanim_atlas_end_frame(void*){
    if(!gLawnApp||!gLawnApp->mEffectSystem||!gLawnApp->mEffectSystem->mReanimationHolder||
       !gReanimatorDefArray||gReanimatorDefCount>(unsigned)ReanimationType::NUM_REANIMS)return;
    std::array<bool,(size_t)ReanimationType::NUM_REANIMS> active{};
    /* All cross-frame instances in this pinned engine belong to this holder.
     * Temporary stack instances have finished before this frame callback. Read
     * the real definitions, including those restored by the original save code. */
    for(Reanimation *animation:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
        for(unsigned i=0;i<gReanimatorDefCount;++i)
            if(animation->mDefinition==&gReanimatorDefArray[i])active[i]=true;
    }
    for(unsigned i=0;i<gReanimatorDefCount;++i){
        ReanimatorDefinition& definition=gReanimatorDefArray[i];
        ReanimAtlas *atlas=definition.mReanimAtlas;
        if(!atlas||active[i])continue;
        /* The original atlas builder encodes array indexes as Image*. Restore
         * those handles before freeing the atlas. A later initialization uses
         * the original builder, dimensions, packing, images and edge sanding. */
        for(int t=0;t<definition.mTracks.count;++t){
            ReanimatorTrack& track=definition.mTracks.tracks[t];
            for(int f=0;f<track.mTransforms.count;++f){
                Sexy::Image *&image=track.mTransforms.mTransforms[f].mImage;
                ReanimAtlasImage *encoded=atlas->GetEncodedReanimAtlas(image);
                if(encoded)image=encoded->mOriginalImage;
            }
        }
        freedPixelBytes+=ImageBytes(atlas->mMemoryImage.get());
        ++freedAtlases;
        definition.mReanimAtlas=nullptr;
        delete atlas;
    }
}
