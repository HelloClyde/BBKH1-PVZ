/* Offline rendering is selected by measured hardware cost, not type coverage.
 * LGPL-3.0-or-later. */
#pragma once
#include "PvzpLib/Reanimator.h"

inline bool H1UseExtraActorBank(ReanimationType) {
    // No generic full-timeline bank has justified its cost on physical H1.
    // Add a type only with a recorded real-time baseline and a replay test.
    return false;
}
inline bool H1UsePolePreviewBank(ReanimationType type) {
    return type == REANIM_POLEVAULTER;
}
