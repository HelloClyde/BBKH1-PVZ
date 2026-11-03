/* Shared, immutable XM payloads for the pinned libxmp-lite core. */
#ifndef PVZH1_XMP_SHARED_H
#define PVZH1_XMP_SHARED_H
#include <stddef.h>
#include <stdint.h>
#include <xmp.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct pvzh1_xmp_module pvzh1_xmp_module;
typedef struct pvzh1_xmp_module_stats {
    size_t original_track_bytes, unique_track_bytes, sample_bytes;
    unsigned tracks, unique_tracks, players;
} pvzh1_xmp_module_stats;

/* Only completely loaded/scanned FT2/XM modules are accepted. No channels,
 * events, orders, sample precision or sample frames are removed. */
pvzh1_xmp_module *pvzh1_xmp_module_load(const char *path);
xmp_context pvzh1_xmp_module_create_player(pvzh1_xmp_module *module);
void pvzh1_xmp_module_free_player(xmp_context player);
/* Defers destruction until outstanding players are freed. */
void pvzh1_xmp_module_free(pvzh1_xmp_module *module);
int pvzh1_xmp_module_get_stats(const pvzh1_xmp_module *module,
                             pvzh1_xmp_module_stats *stats);
/* Diagnostic hash of the shared logical tracks and sample bytes. */
uint32_t pvzh1_xmp_module_payload_hash(const pvzh1_xmp_module *module);

/* Players must be destroyed by free_player, never xmp_free_context or
 * xmp_release_module. Calls must run serially on the foreground thread:
 * libxmp's mixer temporarily adjusts shared sample loop padding, restoring it
 * before returning. No concurrent rendering/loading or sound-effect sample
 * injection is supported. Ordinary player controls and rescanning are private.
 */
#ifdef __cplusplus
}
#endif
#endif
