#ifndef PVZ_H1_RUNTIME_H
#define PVZ_H1_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
/* V1.41 emulator evidence used 80 Hz; this H1 hardware measures 40 Hz.
   Hardware profiles select -DPVZ_H1_RAW_TICK_HZ=40 explicitly. */
#ifndef PVZ_H1_RAW_TICK_HZ
#define PVZ_H1_RAW_TICK_HZ 80
#endif
#if PVZ_H1_RAW_TICK_HZ < 1 || PVZ_H1_RAW_TICK_HZ > 1000
#error PVZ_H1_RAW_TICK_HZ must be a calibrated positive frequency <= 1000
#endif
#ifdef __cplusplus
extern "C" {
#endif
int pvz_native_path(const char *path,char *output,size_t capacity);
void pvz_runtime_delay_ms(unsigned int milliseconds);
uint32_t pvz_runtime_ticks(void);
void pvz_runtime_shutdown(void);
void pvz_runtime_abort(int code) __attribute__((noreturn));
extern volatile uint32_t pvz_runtime_diagnostic[16];
extern char pvz_runtime_last_path[512];
extern volatile uint32_t pvz_runtime_new_failure_caller;
#ifdef __cplusplus
}
#endif
#endif
