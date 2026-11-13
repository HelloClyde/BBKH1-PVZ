#pragma once
#include <cstdint>
class PvzpParticleSystem;
namespace Sexy {class Graphics;struct H1ImageReplayContext;}
bool H1CombatSpritesPrepare();
bool H1CombatFlatDraw(const Sexy::H1ImageReplayContext&);
bool H1CombatHitDraw(PvzpParticleSystem*,Sexy::Graphics*);
void H1CombatHitBegin(PvzpParticleSystem*);
void H1CombatHitUpdate(PvzpParticleSystem*);
void H1CombatHitForget(PvzpParticleSystem*);
void H1CombatFinishLoading();
#ifdef PVZ_HOST_RUNNER
void H1CombatSpritesTestEnable(bool);
bool H1CombatSpritesTestDraw(uint64_t,unsigned,Sexy::Graphics*,int,int);
void H1CombatSpritesTestVariant(PvzpParticleSystem*,unsigned);
#endif
