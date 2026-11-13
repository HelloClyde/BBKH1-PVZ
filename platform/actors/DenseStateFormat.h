/* Identity of the selectively baked dense-scene states. LGPL-3.0-or-later. */
#pragma once
#include <cstdint>
namespace H1ActorPose {
inline unsigned ZombieBlendFrame(unsigned sourceHalf,unsigned step,unsigned direction,unsigned rateBucket){return 0xb0000000u|(direction<<26)|(rateBucket<<24)|(sourceHalf<<5)|step;}
inline unsigned ZombieDisplayBlendStep(unsigned step){return step==19?19:(step+1u)&~1u;}
constexpr float ZombieWalkRates[]={6,12,18};
}
