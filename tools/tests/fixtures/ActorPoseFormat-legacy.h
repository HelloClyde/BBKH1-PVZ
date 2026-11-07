/* Shared asset-compiler/runtime actor-state identity; LGPL-3.0-or-later. */
#pragma once
#include "PvzpLib/Reanimator.h"
#include "Resources.h"
#include <bit>
#include <cstdint>
#include <cstring>
namespace H1ActorPose {
constexpr uint32_t Magic=0x50413148,Version=1,HeaderBytes=64,EntryBytes=24;
constexpr unsigned Width=480,Height=272,PageBytes=4096;
inline bool Supported(ReanimationType type){
 return type==ReanimationType::REANIM_PEASHOOTER||type==ReanimationType::REANIM_SUNFLOWER||
        type==ReanimationType::REANIM_ZOMBIE||type==ReanimationType::REANIM_SODROLL||type==ReanimationType::REANIM_SUN;
}
inline void Mix(uint64_t& hash,uint32_t word){for(unsigned i=0;i<4;++i){hash^=(word>>(i*8))&255;hash*=1099511628211ull;}}
inline uint32_t LinearBits(float f){
 uint32_t bits=std::bit_cast<uint32_t>(f),absolute=bits&0x7fffffffu;
 // Inverting an attachment base pose can leave a few ULPs around identity.
 // This is below 0.002 LCD pixels even across the full logical canvas.
 if(absolute<0x35000000u)return 0;
 if(absolute>=0x3f7ffff0u&&absolute<=0x3f800010u)return (bits&0x80000000u)|0x3f800000u;
 return bits;
}
inline bool ZombieVisibilityVaries(const char* name){
 return !std::strncmp(name,"anim_head",9)||!std::strncmp(name,"anim_innerarm",13)||
        !std::strcmp(name,"anim_hair")||!std::strcmp(name,"anim_tongue")||
        !std::strcmp(name,"Zombie_outerarm_upper")||!std::strcmp(name,"Zombie_outerarm_lower")||!std::strcmp(name,"Zombie_outerarm_hand");
}
inline bool Signature(const Reanimation* r,int group,uint64_t& hash,bool allowBlend=false,bool allowFlash=false){
 if(!r||!Supported(r->mReanimationType)||r->mDead||!r->mDefinition||!r->mTrackInstances||
    r->mDefinition->mTracks.count<=0||r->mDefinition->mTracks.count>256||
    r->mColorOverride!=Sexy::Color::White||(r->mEnableExtraAdditiveDraw&&!allowFlash)||r->mEnableExtraOverlayDraw||
    r->mFilterEffect!=FilterEffect::FILTER_EFFECT_NONE)return false;
 hash=14695981039346656037ull;Mix(hash,uint32_t(r->mReanimationType));Mix(hash,uint32_t(group));
 int base=r->mFrameBasePose;
 // Base poses affect attachment UPDATE matrices, not this actor's DrawTrack.
 // First blink permanently fills these fields; its pixels remain identical.
 if((r->mReanimationType==ReanimationType::REANIM_SUNFLOWER&&base==4)||
    (r->mReanimationType==ReanimationType::REANIM_PEASHOOTER&&base==29))base=-1;
 Mix(hash,uint32_t(r->mDefinition->mTracks.count));Mix(hash,uint32_t(base));
 for(float f:{r->mOverlayMatrix.m00,r->mOverlayMatrix.m01,r->mOverlayMatrix.m10,r->mOverlayMatrix.m11}){
  const uint32_t bits=LinearBits(f);if((bits&0x7f800000u)==0x7f800000u)return false;Mix(hash,bits);
 }
 if((std::bit_cast<uint32_t>(r->mOverlayMatrix.m20)&0x7fffffffu)||
    (std::bit_cast<uint32_t>(r->mOverlayMatrix.m21)&0x7fffffffu)||r->mOverlayMatrix.m22!=1)return false;
 for(int i=0;i<r->mDefinition->mTracks.count;++i){const auto& t=r->mTrackInstances[i];
  if((t.mBlendCounter&&!allowBlend)||t.mShakeX!=0||t.mShakeY!=0)return false;
  if(t.mImageOverride&&(r->mReanimationType!=ReanimationType::REANIM_ZOMBIE||t.mImageOverride!=Sexy::IMAGE_REANIM_ZOMBIE_OUTERARM_UPPER2||std::strcmp(r->mDefinition->mTracks.tracks[i].mName,"Zombie_outerarm_upper")))return false;
  // Zombie packets contain each authored track. Runtime visibility selects
  // cosmetic heads/arms and lost body parts without duplicating whole poses.
  Mix(hash,r->mReanimationType==ReanimationType::REANIM_ZOMBIE&&ZombieVisibilityVaries(r->mDefinition->mTracks.tracks[i].mName)?0u:uint32_t(t.mRenderGroup));Mix(hash,t.mAttachmentID!=AttachmentID::ATTACHMENTID_NULL);
  Mix(hash,uint32_t(t.mTrackColor.ToInt()));Mix(hash,std::bit_cast<uint32_t>(t.mShakeOverride));
  Mix(hash,unsigned(t.mIgnoreClipRect)|(unsigned(t.mTruncateDisappearingFrames)<<1)|
      (unsigned(t.mIgnoreColorOverride)<<2)|(unsigned(t.mIgnoreExtraAdditiveColor)<<3));
 }
 return true;
}
}
