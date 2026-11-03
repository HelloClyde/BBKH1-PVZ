#ifndef PVZ_H1_TIME_H
#define PVZ_H1_TIME_H
#include_next <time.h>
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 1
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 4
#endif
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(clockid_t,struct timespec *);
int nanosleep(const struct timespec *,struct timespec *);
#ifdef __cplusplus
}
#endif
#endif
