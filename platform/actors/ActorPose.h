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
void H1ActorTrackStateChanged(Reanimation*);
void H1ActorAttachmentsChanged();
bool H1PlantShadowDraw(Sexy::Graphics*,Sexy::Image*,float,float,int,float,float);
bool H1ActorPoseDraw(Reanimation*,Sexy::Graphics*,int);
bool H1ActorPosePrepare(bool pea=true,bool zombie=true,bool sod=true);
bool H1PlantPosePrepare(ReanimationType,bool allFilters=false);
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
#endif
