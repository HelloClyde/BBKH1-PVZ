/* Existing bank body keys: hidden hats and visible draw groups. New hat layers have separate pixel replay tests. */
#include "actors/ActorPoseFormat.h"
#define H1ActorPose H1LegacyActorPose
#include "fixtures/ActorPoseFormat-legacy.h"
#undef H1ActorPose
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" void pvzh1_actor_signature_probe(){
 for(auto type:{REANIM_PEASHOOTER,REANIM_PEASHOOTER,REANIM_SUNFLOWER,REANIM_ZOMBIE,REANIM_SODROLL,REANIM_SUN})
  gLawnApp->mEffectSystem->mReanimationHolder->AllocReanimation(0,0,0,type);
 unsigned comparisons=0,accepted=0,rejected=0,outsideLegacyDomain=0;uint32_t random=0x14295367;
 auto compare=[&](Reanimation* r){for(int group:{0,1,2})for(bool blend:{false,true})for(bool flash:{false,true}){if(r->mReanimationType==REANIM_ZOMBIE){bool extended=false;
 for(int i=0;i<r->mDefinition->mTracks.count;++i)if(H1ActorPose::ZombieHatTrack(r->mDefinition->mTracks.tracks[i].mName)){
  const auto& t=r->mTrackInstances[i];extended|=t.mRenderGroup!=RENDER_GROUP_HIDDEN||t.mImageOverride!=nullptr;
 }
 if(extended){++outsideLegacyDomain;continue;}}
 uint64_t a=0,b=0;bool aa=H1LegacyActorPose::Signature(r,group,a,blend,flash),bb=H1ActorPose::Signature(r,group,b,blend,flash);if(aa!=bb||(aa&&a!=b))throw std::runtime_error("legacy actor identity changed");++comparisons;aa?++accepted:++rejected;}};
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){if(!H1ActorPose::Supported(r->mReanimationType))continue;
  // The existing zombie body bank is authored without visible hats. The
  // dedicated hat packets cover visible cone/bucket and damage states.
  if(r->mReanimationType==REANIM_ZOMBIE)for(int i=0;i<r->mDefinition->mTracks.count;++i)
   if(H1ActorPose::ZombieHatTrack(r->mDefinition->mTracks.tracks[i].mName))r->mTrackInstances[i].mRenderGroup=RENDER_GROUP_HIDDEN;
  compare(r);
  for(unsigned n=0;n<8192;++n){random=random*1664525u+1013904223u;int i=random%r->mDefinition->mTracks.count;auto saved=r->mTrackInstances[i];auto matrix=r->mOverlayMatrix;auto color=r->mColorOverride;auto filter=r->mFilterEffect;auto base=r->mFrameBasePose;bool dead=r->mDead,add=r->mEnableExtraAdditiveDraw,overlay=r->mEnableExtraOverlayDraw;auto& t=r->mTrackInstances[i];
   switch(n%12){case 0:t.mRenderGroup=int(random>>16)%5-1;break;case 1:t.mBlendCounter=random%21;break;case 2:t.mShakeX=(random&1)?0:.25f;break;case 3:t.mTrackColor=Sexy::Color(random);break;case 4:r->mColorOverride=Sexy::Color(random);break;case 5:r->mEnableExtraAdditiveDraw=random&1;break;case 6:r->mEnableExtraOverlayDraw=random&1;break;case 7:r->mFilterEffect=FilterEffect(int(random%4)-1);break;case 8:r->mFrameBasePose=int(random%60)-1;break;case 9:r->mOverlayMatrix.m00=std::bit_cast<float>(random);break;case 10:r->mDead=random&1;break;case 11:t.mImageOverride=(random&1)?Sexy::IMAGE_REANIM_ZOMBIE_OUTERARM_UPPER2:Sexy::IMAGE_REANIM_WALLNUT_CRACKED1;break;}
   compare(r);t=saved;r->mOverlayMatrix=matrix;r->mColorOverride=color;r->mFilterEffect=filter;r->mFrameBasePose=base;r->mDead=dead;r->mEnableExtraAdditiveDraw=add;r->mEnableExtraOverlayDraw=overlay;
  }
 }
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"actor-signature-compatibility-verification.json");out<<"{\"passed\":true,\"comparisons\":"<<comparisons<<",\"accepted\":"<<accepted<<",\"rejected\":"<<rejected<<",\"outside_legacy_body_domain\":"<<outsideLegacyDomain<<"}\n";
}
