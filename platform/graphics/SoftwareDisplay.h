#ifndef PVZH1_SOFTWARE_DISPLAY_H
#define PVZH1_SOFTWARE_DISPLAY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called with the complete, original-size AARRGGBB game frame. Returning zero
 * reports a failed display transfer. Register before GLInterface::Init. */
typedef int (*PvzH1Present)(const uint32_t *argb, int width, int height, void *context);
void pvzh1_graphics_set_present(PvzH1Present callback, void *context);
void pvzh1_graphics_set_display_size(int width, int height);
/* Optional cache notification, called only after the present callback returns.
 * The resource backend may evict unpinned source images at this point. */
typedef void (*PvzH1FrameComplete)(void *context);
void pvzh1_graphics_set_frame_complete(PvzH1FrameComplete callback, void *context);
#ifdef __cplusplus
}
#endif
#endif
