/* H1-only exact numerical memoization for the pinned serial libxmp player.
 * No sample, phase, interpolation or voice state is stored here.
 * SPDX-License-Identifier: MIT */
#ifndef PVZH1_XMP_NUMERIC_CACHE_H
#define PVZH1_XMP_NUMERIC_CACHE_H
#include <stdint.h>

typedef struct { uint64_t calls,hits,stores,rejected; } H1XmpNumericStats;
/* Test/diagnostic functions live in the two generated translation units.
 * Reset only between serial playback calls; no allocation or Image pointers. */
void pvzh1_xmp_note_cache_reset(void);
void pvzh1_xmp_step_cache_reset(void);
H1XmpNumericStats pvzh1_xmp_note_cache_stats(void);
H1XmpNumericStats pvzh1_xmp_step_cache_stats(void);

#ifdef H1_PVZ_PORTABLE
typedef struct { int32_t note,bend; uint64_t result; } H1XmpNoteEntry;
typedef struct { int32_t c5spd,freq; uint64_t period,result; } H1XmpStepEntry;
typedef struct {
    H1XmpNoteEntry entries[128]; uint32_t valid[4],victim[2];
#ifdef PVZH1_XMP_NUMERIC_STATS
    H1XmpNumericStats stats;
#endif
} H1XmpNoteCache;
typedef struct {
    H1XmpStepEntry entries[64]; uint32_t valid[2],victim[1];
#ifdef PVZH1_XMP_NUMERIC_STATS
    H1XmpNumericStats stats;
#endif
} H1XmpStepCache;

static __attribute__((always_inline)) inline uint64_t h1_xmp_bits(double value) {
    uint64_t bits; __builtin_memcpy(&bits,&value,sizeof bits); return bits;
}
static __attribute__((always_inline)) inline double h1_xmp_double(uint64_t bits) {
    double value; __builtin_memcpy(&value,&bits,sizeof value); return value;
}
static __attribute__((always_inline)) inline int h1_xmp_positive_normal(uint64_t bits) {
    const uint32_t high=(uint32_t)(bits>>32),exponent=(high>>20)&0x7ffu;
    return !(high&0x80000000u)&&exponent!=0&&exponent!=0x7ffu;
}
static __attribute__((always_inline)) inline uint32_t h1_xmp_hash(uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    uint32_t h=0x932a54d3u;
    h^=a+0x9e3779b9u+(h<<6)+(h>>2);h^=b+0x9e3779b9u+(h<<6)+(h>>2);
    h^=c+0x9e3779b9u+(h<<6)+(h>>2);h^=d+0x9e3779b9u+(h<<6)+(h>>2);
    h^=h>>16;h*=0x85ebca6bu;h^=h>>13;return h;
}
static __attribute__((always_inline)) inline int h1_xmp_valid(const uint32_t *bits,unsigned entry) {
    return (bits[entry>>5]>>(entry&31u))&1u;
}
static __attribute__((always_inline)) inline void h1_xmp_mark(uint32_t *bits,unsigned entry) {
    bits[entry>>5]|=1u<<(entry&31u);
}
static __attribute__((always_inline)) inline void h1_xmp_victim(uint32_t *bits,unsigned set,unsigned way) {
    const uint32_t bit=1u<<(set&31u);uint32_t *word=&bits[set>>5];
    *word=way?(*word|bit):(*word&~bit);
}
#ifdef PVZH1_XMP_NUMERIC_STATS
#define H1_XMP_STAT(cache,field) (++(cache)->stats.field)
#else
#define H1_XMP_STAT(cache,field) ((void)0)
#endif
static inline int h1_xmp_note_read(H1XmpNoteCache *cache,int note,int bend,double *out) {
    const unsigned set=h1_xmp_hash((uint32_t)note,(uint32_t)bend,0,0)&63u,base=set*2u;
    unsigned way;H1_XMP_STAT(cache,calls);
    for(way=0;way<2;++way){const H1XmpNoteEntry *entry=&cache->entries[base+way];
        if(h1_xmp_valid(cache->valid,base+way)&&entry->note==note&&entry->bend==bend){
            *out=h1_xmp_double(entry->result);h1_xmp_victim(cache->victim,set,1u-way);H1_XMP_STAT(cache,hits);return 1;
        }
    }
    return 0;
}
static inline void h1_xmp_note_write(H1XmpNoteCache *cache,int note,int bend,double value) {
    const uint64_t result=h1_xmp_bits(value);unsigned set,base,way;
    if(!h1_xmp_positive_normal(result)){H1_XMP_STAT(cache,rejected);return;}
    set=h1_xmp_hash((uint32_t)note,(uint32_t)bend,0,0)&63u;base=set*2u;
    way=!h1_xmp_valid(cache->valid,base)?0:!h1_xmp_valid(cache->valid,base+1)?1:(cache->victim[set>>5]>>(set&31u))&1u;
    cache->entries[base+way]=(H1XmpNoteEntry){note,bend,result};h1_xmp_mark(cache->valid,base+way);
    h1_xmp_victim(cache->victim,set,1u-way);H1_XMP_STAT(cache,stores);
}
static inline int h1_xmp_step_read(H1XmpStepCache *cache,int c5spd,int freq,double period,double *out) {
    const uint64_t bits=h1_xmp_bits(period);unsigned set,base,way;H1_XMP_STAT(cache,calls);
    if(c5spd<=0||freq<=0||!h1_xmp_positive_normal(bits))return 0;
    set=h1_xmp_hash((uint32_t)c5spd,(uint32_t)freq,(uint32_t)bits,(uint32_t)(bits>>32))&31u;base=set*2u;
    for(way=0;way<2;++way){const H1XmpStepEntry *entry=&cache->entries[base+way];
        if(h1_xmp_valid(cache->valid,base+way)&&entry->c5spd==c5spd&&entry->freq==freq&&entry->period==bits){
            *out=h1_xmp_double(entry->result);h1_xmp_victim(cache->victim,set,1u-way);H1_XMP_STAT(cache,hits);return 1;
        }
    }
    return 0;
}
static inline void h1_xmp_step_write(H1XmpStepCache *cache,int c5spd,int freq,double period,double value) {
    const uint64_t bits=h1_xmp_bits(period),result=h1_xmp_bits(value);unsigned set,base,way;
    if(c5spd<=0||freq<=0||!h1_xmp_positive_normal(bits)||!h1_xmp_positive_normal(result)){H1_XMP_STAT(cache,rejected);return;}
    set=h1_xmp_hash((uint32_t)c5spd,(uint32_t)freq,(uint32_t)bits,(uint32_t)(bits>>32))&31u;base=set*2u;
    way=!h1_xmp_valid(cache->valid,base)?0:!h1_xmp_valid(cache->valid,base+1)?1:(cache->victim[set>>5]>>(set&31u))&1u;
    cache->entries[base+way]=(H1XmpStepEntry){c5spd,freq,bits,result};h1_xmp_mark(cache->valid,base+way);
    h1_xmp_victim(cache->victim,set,1u-way);H1_XMP_STAT(cache,stores);
}
#endif
#endif
