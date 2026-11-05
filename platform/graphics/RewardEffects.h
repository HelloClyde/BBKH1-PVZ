/* Precomputed level-award light; LGPL-3.0-or-later. */
#pragma once
class PvzpParticleSystem;
class LawnApp;
namespace Sexy {class Graphics;}
void H1RewardEffectsPrepare();
bool H1RewardEffectsBegin(PvzpParticleSystem*);
bool H1RewardEffectsUpdate(PvzpParticleSystem*);
bool H1RewardEffectsDraw(PvzpParticleSystem*,Sexy::Graphics*);
void H1RewardEffectsForget(PvzpParticleSystem*);
PvzpParticleSystem* H1RewardStarburst(LawnApp*,float,float,int);
extern "C" void pvzh1_reward_effect_shutdown();
#ifdef PVZ_HOST_RUNNER
void H1RewardEffectsTestEnable(bool);
bool H1RewardEffectsTestDraw(unsigned,unsigned,Sexy::Graphics*,int,int);
bool H1RewardEffectsTestLoad(const char*);
#endif
