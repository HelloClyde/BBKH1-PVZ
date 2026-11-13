/* Shared asset-compiler/runtime actor-state identity; LGPL-3.0-or-later. */
#pragma once
#include "PvzpLib/Reanimator.h"
#include "Resources.h"
#include "PlantPoseTypes.h"
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
inline bool PerTrackType(ReanimationType type){return int(type)>=0&&int(type)<NUM_REANIMS;}
inline bool ExtraType(ReanimationType type){return PerTrackType(type)&&!PlantType(type)&&!Supported(type)&&type!=REANIM_SELECTOR_SCREEN;}
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
inline bool ZombieHatTrack(const char* name){
 return !std::strcmp(name,"anim_cone")||!std::strcmp(name,"anim_bucket");
}
inline unsigned PlantImageOverrideKey(Sexy::Image* image){
 if(!image)return 0;
 const Sexy::Image* choices[]={Sexy::IMAGE_REANIM_WALLNUT_CRACKED1,Sexy::IMAGE_REANIM_WALLNUT_CRACKED2,
  Sexy::IMAGE_REANIM_TALLNUT_CRACKED1,Sexy::IMAGE_REANIM_TALLNUT_CRACKED2,
  Sexy::IMAGE_REANIM_GARLIC_BODY2,Sexy::IMAGE_REANIM_GARLIC_BODY3,
  Sexy::IMAGE_REANIM_PUMPKIN_DAMAGE1,Sexy::IMAGE_REANIM_PUMPKIN_DAMAGE3};
 for(unsigned i=0;i<8;++i)if(image==choices[i])return i+1;return UINT32_MAX;
}
template<class Emit> inline bool VisitSignature(const Reanimation* r,int group,Emit emit,bool allowBlend,bool allowFlash,bool perTrack){
 if(!r||(!Supported(r->mReanimationType)&&!(perTrack&&PerTrackType(r->mReanimationType)))||r->mDead||!r->mDefinition||!r->mTrackInstances||
    r->mDefinition->mTracks.count<=0||r->mDefinition->mTracks.count>256||
    (!perTrack&&r->mColorOverride!=Sexy::Color::White)||(r->mEnableExtraAdditiveDraw&&!allowFlash)||r->mEnableExtraOverlayDraw||
    (!perTrack&&r->mFilterEffect!=FilterEffect::FILTER_EFFECT_NONE))return false;
 emit(uint32_t(r->mReanimationType));emit(perTrack?0u:uint32_t(group));
 if(perTrack){emit(uint32_t(r->mColorOverride.ToInt()));emit(uint32_t(r->mFilterEffect));}
 int base=r->mFrameBasePose;
 // Base poses affect attachment UPDATE matrices, not this actor's DrawTrack.
 // First blink permanently fills these fields; its pixels remain identical.
 if((r->mReanimationType==ReanimationType::REANIM_SUNFLOWER&&base==4)||
    (r->mReanimationType==ReanimationType::REANIM_PEASHOOTER&&base==29))base=-1;
 emit(uint32_t(r->mDefinition->mTracks.count));emit(perTrack?0u:uint32_t(base));
 for(float f:{r->mOverlayMatrix.m00,r->mOverlayMatrix.m01,r->mOverlayMatrix.m10,r->mOverlayMatrix.m11}){
  const uint32_t bits=LinearBits(f);if((bits&0x7f800000u)==0x7f800000u)return false;emit(bits);
 }
 if((std::bit_cast<uint32_t>(r->mOverlayMatrix.m20)&0x7fffffffu)||
    (std::bit_cast<uint32_t>(r->mOverlayMatrix.m21)&0x7fffffffu)||r->mOverlayMatrix.m22!=1)return false;
 for(int i=0;i<r->mDefinition->mTracks.count;++i){const auto& t=r->mTrackInstances[i];
  const bool hat=!perTrack&&r->mReanimationType==ReanimationType::REANIM_ZOMBIE&&ZombieHatTrack(r->mDefinition->mTracks.tracks[i].mName);
  // The ordinary bank has no hat pixels. Reuse its exact body signature only
  // for hats drawn after every visible body track, preserving paint order.
  if(hat&&t.mRenderGroup==group)for(int j=i+1;j<r->mDefinition->mTracks.count;++j)
   if(r->mTrackInstances[j].mRenderGroup==group&&!ZombieHatTrack(r->mDefinition->mTracks.tracks[j].mName))return false;
  if((t.mBlendCounter&&!allowBlend)||t.mShakeX!=0||t.mShakeY!=0)return false;
  unsigned overrideKey=0;if(perTrack){overrideKey=PlantImageOverrideKey(t.mImageOverride);if(overrideKey==UINT32_MAX)return false;}
  else if(t.mImageOverride&&!hat&&(r->mReanimationType!=ReanimationType::REANIM_ZOMBIE||t.mImageOverride!=Sexy::IMAGE_REANIM_ZOMBIE_OUTERARM_UPPER2||std::strcmp(r->mDefinition->mTracks.tracks[i].mName,"Zombie_outerarm_upper")))return false;
  // Zombie packets contain each authored track. Runtime visibility selects
  // cosmetic heads/arms and lost body parts without duplicating whole poses.
  emit(perTrack?0u:hat?uint32_t(RENDER_GROUP_HIDDEN):r->mReanimationType==ReanimationType::REANIM_ZOMBIE&&ZombieVisibilityVaries(r->mDefinition->mTracks.tracks[i].mName)?0u:uint32_t(t.mRenderGroup));emit(perTrack?0u:t.mAttachmentID!=AttachmentID::ATTACHMENTID_NULL);
  if(perTrack)emit(overrideKey);
  emit(uint32_t(t.mTrackColor.ToInt()));emit(std::bit_cast<uint32_t>(t.mShakeOverride));
  emit(unsigned(t.mIgnoreClipRect)|(unsigned(t.mTruncateDisappearingFrames)<<1)|
      (unsigned(t.mIgnoreColorOverride)<<2)|(unsigned(t.mIgnoreExtraAdditiveColor)<<3));
 }
 return true;
}
constexpr unsigned SignatureWords=16+256*6;
inline bool Signature(const Reanimation* r,int group,uint64_t& hash,bool allowBlend=false,bool allowFlash=false,bool perTrack=false){
 hash=14695981039346656037ull;return VisitSignature(r,group,[&](uint32_t word){Mix(hash,word);},allowBlend,allowFlash,perTrack);
}
}
