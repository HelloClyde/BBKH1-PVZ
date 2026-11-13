/* Optional offline LCD actor poses. LGPL-3.0-or-later. */
#pragma once
class Reanimation;
class Board;
class ReanimatorTransform;
struct ReanimatorFrameTime;
#include "ConstEnums.h"
namespace Sexy{class Graphics;class Image;class SexyTransform2D;}
bool H1ActorBaseInverse(Reanimation*,int,Sexy::SexyTransform2D&,bool store=false);
double H1ActorAnimIncrement(Reanimation*);
bool H1ActorFrameTime(Reanimation*,ReanimatorFrameTime&,bool store=false);
int H1ActorNextActiveTrack(Reanimation*,int after);
bool H1ActorHasAttachments(Reanimation*);
bool H1ActorColorPropagationUnchanged(Reanimation*);
void H1ActorTrackStateChanged(Reanimation*);
void H1ActorAttachmentsChanged(const void* attachmentSlot);
bool H1PlantShadowDraw(Sexy::Graphics*,Sexy::Image*,float,float,int,float,float);
bool H1ActorPoseDraw(Reanimation*,Sexy::Graphics*,int);
bool H1ActorPosePrepare(bool pea=true,bool zombie=true,bool sod=true,bool normal=true,bool sun=true);
bool H1PlantPosePrepare(ReanimationType,bool allFilters=false);
bool H1ExtraPosePrepare(ReanimationType);
bool H1SunflowerGlowPrepare();
void H1ActorPoseFinishSod();
void H1ActorPoseForget(Reanimation*);
struct ReanimatorDefinition;
void H1ActorPoseForgetDefinition(ReanimatorDefinition*);
void H1PlantPoseDiscardUnneeded(const bool* types,const bool* filters);
void H1ActorPoseNoteBlend(Reanimation*,int);
bool H1SodOfflineBegin(Board*);
bool H1SodOfflineDraw(Board*,Sexy::Graphics*);
extern "C" void pvzh1_actor_pose_shutdown();
#ifdef PVZ_HOST_RUNNER
void H1ActorPoseTestEnable(bool);
void H1ActorMatrixTestEnable(bool);
void H1ActorColorMemoTestEnable(bool);
#endif

#ifdef PVZ_HOST_RUNNER
void H1ExtraPoseTestIdleEnable(bool enabled);
void H1ExtraPoseTestAllEnable(bool enabled);
#endif

#ifdef PVZH1_ACTOR_DETAIL_PROFILE
uint32_t H1ActorLiveDrawBegin();
void H1ActorLiveDrawEnd(Reanimation*,uint32_t);
struct H1ActorLiveDrawScope {
    Reanimation* actor;uint32_t start;
    explicit H1ActorLiveDrawScope(Reanimation* value):actor(value),start(H1ActorLiveDrawBegin()){}
    ~H1ActorLiveDrawScope(){H1ActorLiveDrawEnd(actor,start);}
};
#endif
