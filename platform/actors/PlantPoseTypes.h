/* Plant definition types from the pinned upstream; LGPL-3.0-or-later. */
#pragma once
#include "PvzpLib/Reanimator.h"
namespace H1ActorPose {
inline bool PlantType(ReanimationType type){switch(type){
case ReanimationType::REANIM_BLOVER:
case ReanimationType::REANIM_CABBAGEPULT:
case ReanimationType::REANIM_CACTUS:
case ReanimationType::REANIM_CATTAIL:
case ReanimationType::REANIM_CHERRYBOMB:
case ReanimationType::REANIM_CHOMPER:
case ReanimationType::REANIM_COBCANNON:
case ReanimationType::REANIM_COFFEEBEAN:
case ReanimationType::REANIM_DOOMSHROOM:
case ReanimationType::REANIM_FLOWER_POT:
case ReanimationType::REANIM_FUMESHROOM:
case ReanimationType::REANIM_GARLIC:
case ReanimationType::REANIM_GATLINGPEA:
case ReanimationType::REANIM_GLOOMSHROOM:
case ReanimationType::REANIM_GOLD_MAGNET:
case ReanimationType::REANIM_GRAVE_BUSTER:
case ReanimationType::REANIM_HYPNOSHROOM:
case ReanimationType::REANIM_ICESHROOM:
case ReanimationType::REANIM_IMITATER:
case ReanimationType::REANIM_JALAPENO:
case ReanimationType::REANIM_KERNELPULT:
case ReanimationType::REANIM_LILYPAD:
case ReanimationType::REANIM_MAGNETSHROOM:
case ReanimationType::REANIM_MARIGOLD:
case ReanimationType::REANIM_MELONPULT:
case ReanimationType::REANIM_PEASHOOTER:
case ReanimationType::REANIM_PLANTERN:
case ReanimationType::REANIM_POTATOMINE:
case ReanimationType::REANIM_PUFFSHROOM:
case ReanimationType::REANIM_PUMPKIN:
case ReanimationType::REANIM_REPEATER:
case ReanimationType::REANIM_SCRAREYSHROOM:
case ReanimationType::REANIM_SEASHROOM:
case ReanimationType::REANIM_SNOWPEA:
case ReanimationType::REANIM_SPIKEROCK:
case ReanimationType::REANIM_SPIKEWEED:
case ReanimationType::REANIM_SPLITPEA:
case ReanimationType::REANIM_SQUASH:
case ReanimationType::REANIM_STARFRUIT:
case ReanimationType::REANIM_SUNFLOWER:
case ReanimationType::REANIM_SUNSHROOM:
case ReanimationType::REANIM_TALLNUT:
case ReanimationType::REANIM_TANGLEKELP:
case ReanimationType::REANIM_THREEPEATER:
case ReanimationType::REANIM_TORCHWOOD:
case ReanimationType::REANIM_TWIN_SUNFLOWER:
case ReanimationType::REANIM_UMBRELLALEAF:
case ReanimationType::REANIM_WALLNUT:
case ReanimationType::REANIM_WINTER_MELON:
case ReanimationType::REANIM_ZENGARDEN_SPROUT:
return true;default:return false;}}
}
