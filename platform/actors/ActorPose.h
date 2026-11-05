/* Optional offline LCD actor poses. LGPL-3.0-or-later. */
#pragma once
class Reanimation;
class Board;
namespace Sexy{class Graphics;}
bool H1ActorPoseDraw(Reanimation*,Sexy::Graphics*,int);
void H1ActorPosePrepare();
void H1ActorPoseNoteBlend(Reanimation*,int);
bool H1SodOfflineBegin(Board*);
bool H1SodOfflineDraw(Board*,Sexy::Graphics*);
extern "C" void pvzh1_actor_pose_shutdown();
#ifdef PVZ_HOST_RUNNER
void H1ActorPoseTestEnable(bool);
#endif
