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
    PVZ_PROFILE_PLANT_UPDATE=13,
    PVZ_PROFILE_ZOMBIE_UPDATE=14,
    PVZ_PROFILE_PROJECTILE_UPDATE=15,
    PVZ_PROFILE_COIN_UPDATE=16,
    PVZ_PROFILE_PARTICLE_UPDATE=17,
    PVZ_PROFILE_BOARD_UPDATE=18,
    PVZ_PROFILE_PLANT_DRAW=19,
    PVZ_PROFILE_ZOMBIE_DRAW=20,
    PVZ_PROFILE_PROJECTILE_DRAW=21,
    PVZ_PROFILE_COIN_DRAW=22,
    PVZ_PROFILE_OBJECTS_DRAW=23,
    PVZ_PROFILE_WIDGET_UPDATE=24,
    PVZ_PROFILE_PARTICLE_DRAW=25,
    PVZ_PROFILE_BACKDROP_DRAW=26,
    PVZ_PROFILE_UITOP_DRAW=27,
    PVZ_PROFILE_PROJECTILE_SHADOW=28,
    PVZ_PROFILE_ZOMBIE_SHADOW=29,
    PVZ_PROFILE_PLANT_SHADOW=30,
    PVZ_PROFILE_BOARD_DRAW=31,
    PVZ_PROFILE_BOARDGAME_UPDATE=32,
    PVZ_PROFILE_GAMEOBJECTS_UPDATE=33,
    PVZ_PROFILE_ACTOR_GUARD=34,
    PVZ_PROFILE_ACTOR_SIGNATURE=35,
    PVZ_PROFILE_ACTOR_PACKET=36,
    PVZ_PROFILE_ACTOR_PAGE=37,
    PVZ_PROFILE_ACTOR_PIXELS=38,
    PVZ_PROFILE_ACTOR_ATTACHMENTS=39,
    PVZ_PROFILE_ACTOR_COMMIT=40,
    PVZ_PROFILE_ACTOR_DISPATCH=41,
    PVZ_PROFILE_ACTOR_LOOP=42,
    PVZ_PROFILE_PLANT_ABILITIES=43,
    PVZ_PROFILE_PLANT_ANIMATE=44,
    PVZ_PROFILE_PLANT_REANIM=45,
    PVZ_PROFILE_PLANT_COLOR=46,
    PVZ_PROFILE_REANIM_UPDATE=47,
    PVZ_PROFILE_STAGE_COUNT=48
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
/* 0: frame clock only, 1: whole-frame boundaries, 2: full attribution.
   Each scope remembers its mode at entry, including across a mode change. */
extern volatile uint32_t pvz_runtime_profile_mode;
static inline int pvz_profile_enabled(unsigned stage) {
    const uint32_t mode=pvz_runtime_profile_mode;
    return mode&&(mode>1||stage<=PVZ_PROFILE_FRAME);
}
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
    bool active;
#endif
public:
    explicit PVZProfileScope(unsigned value)
#ifdef PVZ_NATIVE_PROFILE
        : stage(value),started(0),active(pvz_profile_enabled(value))
#endif
    { (void)value;
#ifdef PVZ_NATIVE_PROFILE
        if(active)started=pvz_profile_enter(value);
#endif
    }
    ~PVZProfileScope() {
#ifdef PVZ_NATIVE_PROFILE
        if(active)pvz_profile_leave(stage,started);
#endif
    }
    PVZProfileScope(const PVZProfileScope&)=delete;
    PVZProfileScope& operator=(const PVZProfileScope&)=delete;
};
#endif
#endif
