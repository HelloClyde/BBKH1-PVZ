/* Shared loaded XM payloads, pinned libxmp 06efa94d463d6202e9d553ab45aa797ad6b7a0a3.
 * This integration uses the private context layout; vendor sources stay intact.
 * Copyright (c) 2026. SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "xmp_shared.h"
#include "../vendor/libxmp/src/common.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if !defined(LIBXMP_CORE_PLAYER) || !defined(LIBXMP_CORE_DISABLE_IT)
#error xmp_shared requires the audited libxmp-lite CORE_PLAYER / DISABLE_IT configuration
#endif

struct pvzh1_xmp_module {
    struct context_data *owner;
    unsigned char *aliases;
    pvzh1_xmp_module_stats stats;
    int closing;
};
struct borrowed_player {
    xmp_context player;
    pvzh1_xmp_module *module;
    struct borrowed_player *next;
};
static struct borrowed_player *players;

/* phase(1 open,2 buffer,3 libxmp parse+scan,4 dedup,5 ready,6 failed),
 * attempts, start-ms, libxmp-ms, dedup-ms, FILE buffer bytes (0=default),
 * file length, total-ms, failures, fclose count, libxmp result, reserved.
 * Optional clock keeps the standalone PCM regression free of hardware stubs.
 * Update only at module boundaries; never emit per-read/sample log traffic. */
volatile uint32_t pvzh1_xmp_load_diagnostic[12];
extern uint32_t pvzp_ticks_ms(void) __attribute__((weak));
extern void pvzp_audio_service(void) __attribute__((weak));
static uint32_t load_ticks(void) { return pvzp_ticks_ms ? pvzp_ticks_ms() : 0; }
/* Module loading/rendering is serialized on the foreground thread. Reuse a
 * small, non-owning FILE buffer without occupying the heap's large sample
 * holes. fclose finishes using it before another module can be loaded. */
static char module_read_buffer[16384];

static int load_buffered_module(struct context_data *ctx, const char *path)
{
    FILE *input;
    int result;
    uint32_t started;
    pvzh1_xmp_load_diagnostic[0] = 1;
    input = fopen(path, "rb");
    if (!input) {
        pvzh1_xmp_load_diagnostic[10] = (uint32_t)-XMP_ERROR_SYSTEM;
        return -XMP_ERROR_SYSTEM;
    }
    pvzh1_xmp_load_diagnostic[0] = 2;
    pvzh1_xmp_load_diagnostic[5] =
        setvbuf(input, module_read_buffer, _IOFBF, sizeof(module_read_buffer)) == 0
        ? (uint32_t)sizeof(module_read_buffer) : 0;
    pvzh1_xmp_load_diagnostic[0] = 3;
    started = load_ticks();
    result = xmp_load_module_from_file((xmp_context)ctx, input, 0);
    pvzh1_xmp_load_diagnostic[3] = load_ticks() - started;
    pvzh1_xmp_load_diagnostic[6] = ctx->m.size > 0 ? (uint32_t)ctx->m.size : 0;
    fclose(input);
    pvzh1_xmp_load_diagnostic[9] = pvzh1_xmp_load_diagnostic[9] + 1;
    pvzh1_xmp_load_diagnostic[10] = (uint32_t)result;
    return result;
}

static uint32_t hash_bytes(uint32_t h, const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    while (size--) { h ^= *p++; h *= UINT32_C(16777619); }
    return h;
}

static size_t track_size(const struct xmp_track *track)
{
    if (!track || track->rows < 0 ||
        (size_t)track->rows > (SIZE_MAX - offsetof(struct xmp_track, event)) / sizeof(struct xmp_event))
        return 0;
    return offsetof(struct xmp_track, event) + (size_t)track->rows * sizeof(struct xmp_event);
}

static void destroy_module(pvzh1_xmp_module *module)
{
    if (module->owner) {
        struct xmp_module *mod = &module->owner->m.mod;
        int i;
        /* Standard libxmp release owns canonical allocations exactly once. */
        if (module->aliases && mod->xxt)
            for (i = 0; i < mod->trk; ++i)
                if (module->aliases[i >> 3] & (1u << (i & 7))) mod->xxt[i] = NULL;
        xmp_free_context((xmp_context)module->owner);
    }
    free(module->aliases);
    free(module);
}

static int deduplicate_tracks(pvzh1_xmp_module *module)
{
    struct xmp_module *mod = &module->owner->m.mod;
    unsigned *table;
    size_t capacity = 1, i;
    while (capacity < (size_t)mod->trk * 2) {
        if (capacity > SIZE_MAX / 2) return -1;
        capacity *= 2;
    }
    table = (unsigned *)calloc(capacity, sizeof(*table));
    module->aliases = (unsigned char *)calloc(((size_t)mod->trk + 7) / 8, 1);
    if (!table || !module->aliases) { free(table); return -1; }
    for (i = 0; i < (size_t)mod->trk; ++i) {
        if(pvzp_audio_service)pvzp_audio_service();
        struct xmp_track *track = mod->xxt[i];
        size_t bytes = track_size(track), slot;
        if (!bytes) { free(table); return -1; }
        module->stats.original_track_bytes += bytes;
        slot = hash_bytes(UINT32_C(2166136261), track, bytes) & (capacity - 1);
        while (table[slot]) {
            unsigned canonical = table[slot] - 1;
            struct xmp_track *other = mod->xxt[canonical];
            if (other->rows == track->rows && !memcmp(other, track, bytes)) {
                free(track);
                mod->xxt[i] = other;
                module->aliases[i >> 3] |= (unsigned char)(1u << (i & 7));
                break;
            }
            slot = (slot + 1) & (capacity - 1);
        }
        if (!table[slot]) {
            table[slot] = (unsigned)i + 1;
            module->stats.unique_track_bytes += bytes;
            ++module->stats.unique_tracks;
        }
    }
    free(table);
    module->stats.tracks = (unsigned)mod->trk;
    for (i = 0; i < (size_t)mod->smp; ++i)
        if (mod->xxs[i].data && mod->xxs[i].len > 0)
            module->stats.sample_bytes += (size_t)mod->xxs[i].len * ((mod->xxs[i].flg & XMP_SAMPLE_16BIT) ? 2 : 1);
    return 0;
}

pvzh1_xmp_module *pvzh1_xmp_module_load(const char *path)
{
    pvzh1_xmp_module *module;
    struct context_data *ctx;
    struct xmp_module *mod;
    int i;
    if (!path) return NULL;
    pvzh1_xmp_load_diagnostic[1] = pvzh1_xmp_load_diagnostic[1] + 1;
    pvzh1_xmp_load_diagnostic[2] = load_ticks();
    pvzh1_xmp_load_diagnostic[3] = 0;
    pvzh1_xmp_load_diagnostic[4] = 0;
    pvzh1_xmp_load_diagnostic[5] = 0;
    pvzh1_xmp_load_diagnostic[6] = 0;
    pvzh1_xmp_load_diagnostic[10] = UINT32_C(0x80000000);
    module = (pvzh1_xmp_module *)calloc(1, sizeof(*module));
    if (!module) goto fail_without_module;
    module->owner = ctx = (struct context_data *)xmp_create_context();
    if (!ctx || load_buffered_module(ctx, path) < 0) goto fail;
    mod = &ctx->m.mod;
    if (ctx->state != XMP_STATE_LOADED || ctx->m.read_event_type != READ_EVENT_FT2 ||
        mod->len <= 0 || mod->trk <= 0 || !mod->xxt || !mod->xxp ||
        !ctx->m.scan_cnt || !ctx->p.scan || ctx->m.num_sequences <= 0) goto fail;
    for (i = 0; i < mod->len; ++i)
        if (!ctx->m.scan_cnt[i] || (mod->xxo[i] < mod->pat && !mod->xxp[mod->xxo[i]])) goto fail;
    pvzh1_xmp_load_diagnostic[0] = 4;
    {
        uint32_t started = load_ticks();
        if (deduplicate_tracks(module) < 0) goto fail;
        pvzh1_xmp_load_diagnostic[4] = load_ticks() - started;
    }
    pvzh1_xmp_load_diagnostic[7] = load_ticks() - pvzh1_xmp_load_diagnostic[2];
    pvzh1_xmp_load_diagnostic[0] = 5;
    return module;
fail:
    destroy_module(module);
fail_without_module:
    pvzh1_xmp_load_diagnostic[8] = pvzh1_xmp_load_diagnostic[8] + 1;
    pvzh1_xmp_load_diagnostic[7] = load_ticks() - pvzh1_xmp_load_diagnostic[2];
    pvzh1_xmp_load_diagnostic[0] = 6;
    return NULL;
}

/* Copy scan allocations explicitly: both the scan API and start_player can
 * update them. All remaining module metadata is already copied by value. */
static int copy_scan(struct context_data *ctx, const struct context_data *owner)
{
    const struct xmp_module *mod = &owner->m.mod;
    int i;
    ctx->m.scan_cnt = (uint8 **)calloc((size_t)mod->len, sizeof(*ctx->m.scan_cnt));
    ctx->p.scan = (struct scan_data *)malloc((size_t)owner->m.num_sequences * sizeof(*ctx->p.scan));
    if (!ctx->m.scan_cnt || !ctx->p.scan) return -1;
    memcpy(ctx->p.scan, owner->p.scan, (size_t)owner->m.num_sequences * sizeof(*ctx->p.scan));
    for (i = 0; i < mod->len; ++i) {
        const struct xmp_pattern *pat = mod->xxo[i] < mod->pat ? mod->xxp[mod->xxo[i]] : NULL;
        size_t rows = pat && pat->rows ? (size_t)pat->rows : 1;
        ctx->m.scan_cnt[i] = (uint8 *)malloc(rows);
        if (!ctx->m.scan_cnt[i]) return -1;
        memcpy(ctx->m.scan_cnt[i], owner->m.scan_cnt[i], rows);
    }
    return 0;
}

static void destroy_player_context(struct context_data *ctx)
{
    if (ctx->state > XMP_STATE_LOADED) xmp_end_player((xmp_context)ctx);
    libxmp_free_scan(ctx);
    free(ctx->m.instrument_path);
    /* Never pass borrowed payload pointers to standard release_module. */
    memset(&ctx->m, 0, sizeof(ctx->m));
    ctx->state = XMP_STATE_UNLOADED;
    xmp_free_context((xmp_context)ctx);
}

xmp_context pvzh1_xmp_module_create_player(pvzh1_xmp_module *module)
{
    struct context_data *ctx, *owner;
    struct borrowed_player *record;
    if (!module || module->closing) return NULL;
    owner = module->owner;
    if (owner->state != XMP_STATE_LOADED || owner->p.xc_data || owner->p.flow.loop ||
        owner->p.virt.virt_channel || owner->p.virt.voice_array ||
        owner->s.buffer || owner->s.buf32 || owner->smix.xxi || owner->smix.xxs) return NULL;
    record = (struct borrowed_player *)malloc(sizeof(*record));
    if (!record) return NULL;
    ctx = (struct context_data *)xmp_create_context();
    if (!ctx) { free(record); return NULL; }
    ctx->m = owner->m;
    ctx->p = owner->p;
    ctx->s = owner->s;
    /* Keep the fresh context's independent RNG; it has the same initialization
     * semantics as two ordinary xmp_create_context calls. */
    ctx->m.scan_cnt = NULL;
    ctx->m.instrument_path = NULL;
    ctx->p.scan = NULL;
    ctx->state = XMP_STATE_LOADED;
    if (owner->m.instrument_path) {
        size_t len = strlen(owner->m.instrument_path) + 1;
        ctx->m.instrument_path = (char *)malloc(len);
        if (!ctx->m.instrument_path) goto fail;
        memcpy(ctx->m.instrument_path, owner->m.instrument_path, len);
    }
    if (copy_scan(ctx, owner) < 0) goto fail;
    record->player = (xmp_context)ctx;
    record->module = module;
    record->next = players;
    players = record;
    ++module->stats.players;
    return (xmp_context)ctx;
fail:
    destroy_player_context(ctx);
    free(record);
    return NULL;
}

void pvzh1_xmp_module_free_player(xmp_context player)
{
    struct borrowed_player **link = &players, *record;
    pvzh1_xmp_module *module;
    if (!player) return;
    while (*link && (*link)->player != player) link = &(*link)->next;
    if (!*link) return;
    record = *link;
    *link = record->next;
    module = record->module;
    destroy_player_context((struct context_data *)player);
    free(record);
    if (--module->stats.players == 0 && module->closing) destroy_module(module);
}

void pvzh1_xmp_module_free(pvzh1_xmp_module *module)
{
    if (!module) return;
    module->closing = 1;
    if (!module->stats.players) destroy_module(module);
}

int pvzh1_xmp_module_get_stats(const pvzh1_xmp_module *module, pvzh1_xmp_module_stats *stats)
{
    if (!module || !stats) return -1;
    *stats = module->stats;
    return 0;
}

uint32_t pvzh1_xmp_module_payload_hash(const pvzh1_xmp_module *module)
{
    const struct xmp_module *mod;
    uint32_t h = UINT32_C(2166136261);
    int i;
    if (!module) return 0;
    mod = &module->owner->m.mod;
    for (i = 0; i < mod->trk; ++i) h = hash_bytes(h, mod->xxt[i], track_size(mod->xxt[i]));
    for (i = 0; i < mod->smp; ++i)
        if (mod->xxs[i].data && mod->xxs[i].len > 0)
            h = hash_bytes(h, mod->xxs[i].data, (size_t)mod->xxs[i].len * ((mod->xxs[i].flg & XMP_SAMPLE_16BIT) ? 2 : 1));
    return h;
}
