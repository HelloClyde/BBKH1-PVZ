#ifndef PVZ_NATIVE_PROFILER_H
#define PVZ_NATIVE_PROFILER_H
#include <stdint.h>

enum {
    PVZ_PROFILE_UPDATE=0, PVZ_PROFILE_DRAW=1, PVZ_PROFILE_PRESENT=2,
    PVZ_PROFILE_AUDIO=3, PVZ_PROFILE_MUSIC=4, PVZ_PROFILE_FRAME=5,
    PVZ_PROFILE_REPEAT_TRIANGLE=6, PVZ_PROFILE_SW_DRAW_SHAPE=7,
    PVZ_PROFILE_SAVE_COMPARE=8, PVZ_PROFILE_SAVE_WRITE=9,
    PVZ_PROFILE_SAVE_CLOSE=10, PVZ_PROFILE_SAVE_COMMIT=11,
    PVZ_PROFILE_SHUTDOWN=12,
    PVZ_PROFILE_STAGE_COUNT=13
};

#ifdef __cplusplus
extern "C" {
#endif
#ifdef PVZ_NATIVE_PROFILE
/* Each stage: calls, elapsed timer low/high, maximum single-call timer delta.
   PVZ_NATIVE_PROFILE_RAW_TICKS uses the calibrated native raw clock (40Hz on
   this H1, 25ms granularity). The legacy CP0 Count backend is not reliable on
   this hardware and is retained only to identify its earlier failed evidence. */
extern volatile uint32_t pvz_runtime_profile_diagnostic[PVZ_PROFILE_STAGE_COUNT*4];
/* sequence-start, native raw tick, timer low/high, native Hz, sequence-end. */
extern volatile uint32_t pvz_runtime_profile_clock[6];
uint32_t pvz_profile_enter(unsigned stage);
void pvz_profile_leave(unsigned stage,uint32_t started);
void pvz_profile_mark_frame(void);
#else
static inline void pvz_profile_mark_frame(void) {}
#endif
#ifdef __cplusplus
}
class PVZProfileScope {
#ifdef PVZ_NATIVE_PROFILE
    unsigned stage;
    uint32_t started;
#endif
public:
    explicit PVZProfileScope(unsigned value)
#ifdef PVZ_NATIVE_PROFILE
        : stage(value),started(pvz_profile_enter(value))
#endif
    { (void)value; }
    ~PVZProfileScope() {
#ifdef PVZ_NATIVE_PROFILE
        pvz_profile_leave(stage,started);
#endif
    }
    PVZProfileScope(const PVZProfileScope&)=delete;
    PVZProfileScope& operator=(const PVZProfileScope&)=delete;
};
#endif
#endif
