/* Optional, offline-sampled selector display. LGPL-3.0-or-later. */
#pragma once
#include <cstdint>
class GameSelector;
class NewLawnButton;
class Reanimation;
namespace Sexy { class Graphics; }

/* These hooks only replace stable, fully-open physical-screen drawing. Updates,
 * hit testing, dialogs and transitions remain the original game. */
bool H1SelectorOfflineDrawGroup(GameSelector*, Sexy::Graphics*, Reanimation*, int);
bool H1SelectorOfflineDrawButton(NewLawnButton*, Sexy::Graphics*);
bool H1SelectorSkipClippedAchievements(Sexy::Graphics*);
bool H1SelectorOfflineWelcomeBegin(GameSelector*, Sexy::Graphics*);
void H1SelectorOfflineWelcomeEnd(GameSelector*, Sexy::Graphics*);
void H1SelectorOfflineRelease(GameSelector*);
#ifdef PVZ_HOST_RUNNER
void H1SelectorOfflineTestEnable(bool);
void H1SelectorOfflineTestRejectVisibility(bool);
#endif

extern "C" {
/* version, loaded entries/bytes, peak bytes, group/button/welcome hits,
 * fallback/missing/malformed/load failures, load count, missing entries,
 * welcome rebuilds, visibility-plan builds, occluded original pixels. */
extern volatile uint32_t pvzh1_selector_offline_diagnostic[16];
/* fully clipped achievement-page draws skipped; remaining words reserved. */
extern volatile uint32_t pvzh1_selector_cull_diagnostic[4];
}
