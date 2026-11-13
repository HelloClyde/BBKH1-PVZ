/* Leaf animation clocks still update at the original logic rate. Only matrix
 * arithmetic is deferred; every observable matrix is the exact eager result.
 * No parent/child dependency graph is retained: the parent matrix is captured
 * before the real Attachment::Update, preserving its one-update ordering.
 * LGPL-3.0-or-later. */
#include "DeferredOverlay.h"
#include "ActorPose.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/Attachment.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/ReanimTrack.h"
#include <new>
#include <cstdint>

extern "C" {volatile uint32_t pvzh1_deferred_overlay_diagnostic[8];}
namespace {
constexpr unsigned Sets=32,Ways=4;
struct Sample {
 Reanimation* child=nullptr;Reanimation* parent=nullptr;
 ReanimatorDefinition* definition=nullptr;
 float animTime=0;int frameStart=0,frameCount=0;ReanimLoopType loop=REANIM_LOOP;
 ReanimatorTransform blend;
 Sexy::SexyTransform2D overlay,inverse,offset;
 int track=0,counter=0,blendTime=0;unsigned stamp=0;
 bool truncate=false,pending=false;
};
Sample (*samples)[Ways]=nullptr;
unsigned overlayClock=0;
struct DefinitionFlags {ReanimatorDefinition* definition=nullptr;bool noScripts=false;} definitions[2];
#ifdef PVZ_HOST_RUNNER
bool enabled=true;
#endif
unsigned Bucket(const Reanimation* p){auto n=reinterpret_cast<uintptr_t>(p);return ((n>>4)^(n>>13))&(Sets-1);}
Sample* Find(Reanimation* actor){
 if(samples&&actor)for(auto& s:samples[Bucket(actor)])if(s.child==actor)return &s;
 return nullptr;
}
bool Ordinary(Reanimation* r){return r&&(r->mReanimationType==REANIM_PEASHOOTER||r->mReanimationType==REANIM_SUNFLOWER);}
bool Leaf(Reanimation* r){
 auto& f=definitions[r->mReanimationType==REANIM_SUNFLOWER];
 if(f.definition!=r->mDefinition){
  f.definition=r->mDefinition;f.noScripts=true;
  for(int i=0;i<r->mDefinition->mTracks.count;++i)
   if(H1IsAttacherTrack(r->mDefinition->mTracks.tracks[i].mName)){f.noScripts=false;break;}
 }
 if(!f.noScripts)return false;
 return !H1ActorHasAttachments(r);
}
void Complete(Sample& s){
 if(!s.pending)return;
 s.pending=false;--pvzh1_deferred_overlay_diagnostic[5];
 // Sampling the frame interpolant is also visual work. Capture its inputs
 // at the original update, and only calculate it when this matrix is used.
 // GetFrameTime keeps the original arithmetic and the exact clock memo key.
 auto* parent=s.parent;
 const float timeNow=parent->mAnimTime;const int startNow=parent->mFrameStart,countNow=parent->mFrameCount;
 const auto loopNow=parent->mLoopType;
 parent->mAnimTime=s.animTime;parent->mFrameStart=s.frameStart;parent->mFrameCount=s.frameCount;parent->mLoopType=s.loop;
 ReanimatorFrameTime time;parent->GetFrameTime(&time);
 parent->mAnimTime=timeNow;parent->mFrameStart=startNow;parent->mFrameCount=countNow;parent->mLoopType=loopNow;
 ++pvzh1_deferred_overlay_diagnostic[3];
 ReanimatorTransform transform;
 s.parent->GetTransformAtTime(s.track,&transform,&time);
 // GetTransformAtTime reads one mutable track flag. Restore the value at the
 // original call, rather than a flag changed before this delayed observation.
 const auto& frames=s.definition->mTracks.tracks[s.track].mTransforms;
 const auto& before=frames.mTransforms[time.mAnimFrameBeforeInt];
 const auto& after=frames.mTransforms[time.mAnimFrameAfterInt];
 transform.mFrame=before.mFrame!=-1.f&&after.mFrame==-1.f&&time.mFraction>0.f&&s.truncate?-1.f:before.mFrame;
 if(FloatRoundToInt(transform.mFrame)>=0&&s.counter>0)
  BlendTransform(&transform,transform,s.blend,s.counter/static_cast<float>(s.blendTime));
 Sexy::SexyTransform2D matrix;
 Reanimation::MatrixFromTransform(transform,matrix);
 SexyMatrix3Multiply(matrix,s.overlay,matrix);
 const Sexy::SexyTransform2D result=matrix*s.inverse;
 s.child->mOverlayMatrix=result*s.offset;
 ++pvzh1_deferred_overlay_diagnostic[1];
}
Sample& Destination(Reanimation* child){
 auto& set=samples[Bucket(child)];Sample* best=&set[0];
 for(auto& s:set){if(s.child==child)return s;if(!s.child)best=&s;else if(best->child&&s.stamp<best->stamp)best=&s;}
 if(best->pending){Complete(*best);++pvzh1_deferred_overlay_diagnostic[4];}
 return *best;
}
}
bool H1DeferredOverlayPrepare(){
 if(!samples)samples=new(std::nothrow) Sample[Sets][Ways];
 pvzh1_deferred_overlay_diagnostic[7]=samples?sizeof(Sample)*Sets*Ways:0;
 return samples!=nullptr;
}
bool H1DeferredAttachmentUpdate(Reanimation* parent,int track){
#ifdef PVZ_HOST_RUNNER
 if(!enabled)return false;
#endif
 if(!samples||!Ordinary(parent))return false;
 auto& instance=parent->mTrackInstances[track];
 auto* attachment=gEffectSystem->mAttachmentHolder->mAttachments.DataArrayTryToGet(static_cast<unsigned>(instance.mAttachmentID));
 if(!attachment||attachment->mNumEffects!=1)return false;
 auto& effect=attachment->mEffectArray[0];
 if(effect.mEffectType!=EFFECT_REANIM)return false;
 auto* child=gEffectSystem->mReanimationHolder->mReanimations.DataArrayTryToGet(effect.mEffectID);
 if(!Ordinary(child)||child->mDead||child->mReanimationType!=parent->mReanimationType||!Leaf(child))return false;
 H1ResolveActorOverlay(parent);
 Sample pending;
 pending.parent=parent;pending.child=child;pending.definition=parent->mDefinition;pending.track=track;
 pending.animTime=parent->mAnimTime;pending.frameStart=parent->mFrameStart;pending.frameCount=parent->mFrameCount;pending.loop=parent->mLoopType;
 pending.overlay=parent->mOverlayMatrix;pending.counter=instance.mBlendCounter;
 pending.blendTime=instance.mBlendTime;pending.truncate=instance.mTruncateDisappearingFrames;
 if(pending.counter)pending.blend=instance.mBlendTransform;
 if(!H1ActorBaseInverse(parent,track,pending.inverse)){
  Sexy::SexyTransform2D base;parent->GetTrackBasePoseMatrix(track,base);
  SexyMatrix3Inverse(base,pending.inverse);H1ActorBaseInverse(parent,track,pending.inverse,true);
 }
 // The eager implementation computes the parent matrix, updates the child
 // using its PREVIOUS overlay, then assigns the new overlay and effect offset.
 attachment->Update();
 if(attachment->mNumEffects){
  pending.offset=attachment->mEffectArray[0].mOffset;
  auto& destination=Destination(child);
  if(destination.pending)++pvzh1_deferred_overlay_diagnostic[2];
  else{++pvzh1_deferred_overlay_diagnostic[5];
   if(pvzh1_deferred_overlay_diagnostic[5]>pvzh1_deferred_overlay_diagnostic[6])pvzh1_deferred_overlay_diagnostic[6]=pvzh1_deferred_overlay_diagnostic[5];}
  pending.pending=true;pending.stamp=++overlayClock;destination=pending;
  ++pvzh1_deferred_overlay_diagnostic[0];
 }
 return true;
}
void H1ResolveActorOverlay(Reanimation* actor){if(auto* s=Find(actor))Complete(*s);}
void H1DropActorOverlay(Reanimation* actor){if(auto* s=Find(actor);s&&s->pending){s->pending=false;--pvzh1_deferred_overlay_diagnostic[5];}}
void H1DeferredOverlayForget(Reanimation* actor){
 if(!samples)return;
 for(unsigned i=0;i<Sets;++i)for(auto& s:samples[i])if(s.child==actor||s.parent==actor){Complete(s);s.child=nullptr;}
}
void H1DeferredOverlayForgetDefinition(ReanimatorDefinition* definition){
 if(samples)for(unsigned i=0;i<Sets;++i)for(auto& s:samples[i])if(s.definition==definition){Complete(s);s.child=nullptr;}
 for(auto& f:definitions)if(f.definition==definition)f={};
}
void H1DeferredOverlayFlush(){if(samples)for(unsigned i=0;i<Sets;++i)for(auto& s:samples[i])Complete(s);}
void H1DeferredOverlayShutdown(){H1DeferredOverlayFlush();delete[]samples;samples=nullptr;overlayClock=0;for(auto& f:definitions)f={};pvzh1_deferred_overlay_diagnostic[7]=0;}
std::size_t H1DeferredOverlayResident(){return samples?sizeof(Sample)*Sets*Ways:0;}
#ifdef PVZ_HOST_RUNNER
void H1DeferredOverlayTestEnable(bool value){H1DeferredOverlayFlush();enabled=value;}
#endif
