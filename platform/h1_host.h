#ifndef PVZ_H1_HOST_H
#define PVZ_H1_HOST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int pvzp_window_open(void);
void pvzp_window_close(void);
void pvzp_present_argb(const uint32_t *,int,int);
int pvzp_input_poll(int *kind,int *key,int *x,int *y);
void pvzp_pointer_set(int x,int y);
uint32_t pvzp_ticks_ms(void);
void pvzp_log_line(const char *);
void pvzp_audio_pump(void);
void pvzp_audio_close(void);
extern volatile uint32_t pvzp_diagnostic[32];
#ifdef __cplusplus
}
#endif
#endif
