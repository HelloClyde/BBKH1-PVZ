/* Optional, allocation-free boundary observations. No timers, audio calls,
   scheduling changes or per-pixel/sample measurements are introduced. */
#include "profiler.h"
#include "runtime.h"
#ifdef PVZ_NATIVE_PROFILE
volatile uint32_t pvz_runtime_profile_diagnostic[PVZ_PROFILE_STAGE_COUNT*4];
volatile uint32_t pvz_runtime_profile_clock[6];
static unsigned depth[PVZ_PROFILE_STAGE_COUNT];
static uint32_t last_count,count_high;
static int clock_started;

static inline uint32_t read_timer(void)
{
#ifdef PVZ_NATIVE_PROFILE_RAW_TICKS
    /* The physical H1's CP0 Count observations were not continuous. Use the
       already calibrated 40Hz native clock, without adding any pump/yield. */
    return pvz_runtime_ticks();
#elif defined(PVZ_PROFILE_TEST)
    extern uint32_t pvz_profile_test_count(void);
    return pvz_profile_test_count();
#elif defined(__mips__)
    uint32_t value;
    __asm__ volatile("mfc0 %0,$9\n\tnop":"=r"(value)::"memory");
    return value;
#else
#error Native profiling needs MIPS CP0 Count or an explicit test counter
#endif
}
uint32_t pvz_profile_enter(unsigned stage)
{
    if(stage>=PVZ_PROFILE_STAGE_COUNT)return 0;
    if(depth[stage]++)return 0; /* Same-stage recursion counts outermost only. */
    return read_timer();
}
void pvz_profile_leave(unsigned stage,uint32_t started)
{
    uint32_t elapsed,low;volatile uint32_t *row;
    if(stage>=PVZ_PROFILE_STAGE_COUNT||!depth[stage])return;
    if(--depth[stage])return;
    elapsed=read_timer()-started;
    row=pvz_runtime_profile_diagnostic+stage*4u;
    ++row[0];low=row[1];row[1]=low+elapsed;
    if(row[1]<low)++row[2];
    if(elapsed>row[3])row[3]=elapsed;
}
void pvz_profile_mark_frame(void)
{
    uint32_t current=read_timer(),sequence=(pvz_runtime_profile_clock[0]&~1u)+2u;
    if(clock_started&&current<last_count)++count_high;
    clock_started=1;last_count=current;
    pvz_runtime_profile_clock[0]=sequence-1u;
    pvz_runtime_profile_clock[1]=pvz_runtime_ticks();
    pvz_runtime_profile_clock[2]=current;
    pvz_runtime_profile_clock[3]=count_high;
    pvz_runtime_profile_clock[4]=PVZ_H1_RAW_TICK_HZ;
    pvz_runtime_profile_clock[5]=sequence;
    pvz_runtime_profile_clock[0]=sequence;
}
#endif
