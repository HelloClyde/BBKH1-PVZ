/* Regression: exact PCM against independently loaded unmodified libxmp players. */
#include "xmp_shared.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"CHECK failed line %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static uint32_t pcm_hash = 2166136261u;
static unsigned blocks;
static void same_control(xmp_context a, xmp_context b, unsigned step, unsigned player)
{
    struct context_data *ctx = (struct context_data *)a;
    int order = ctx->m.mod.len > 1 ? (int)((step / 137 + player * 41) % ctx->m.mod.len) : 0;
    if (step % 137 == 0) {
        CHECK(xmp_set_position(a,order) == xmp_set_position(b,order));
        CHECK(xmp_set_row(a,(int)(step % 31)) == xmp_set_row(b,(int)(step % 31)));
        CHECK(xmp_play_buffer(a,NULL,0,0) == xmp_play_buffer(b,NULL,0,0));
    }
    if (step % 79 == 0) {
        int channel=(int)((step/79+player)%ctx->m.mod.chn);
        CHECK(xmp_channel_vol(a,channel,(int)(step%101)) == xmp_channel_vol(b,channel,(int)(step%101)));
    }
    if (step % 211 == 0) {
        int channel=(int)((step/211+player)%ctx->m.mod.chn);
        CHECK(xmp_channel_mute(a,channel,(int)((step/211)&1)) == xmp_channel_mute(b,channel,(int)((step/211)&1)));
    }
    if (step == 503) {
        /* Both scanners must own their counters and scan arrays. */
        xmp_scan_module(b); xmp_scan_module(a);
    }
    if (step == 727) {
        CHECK(xmp_set_tempo_factor(a,0.91) == xmp_set_tempo_factor(b,0.91));
    }
    if (step == 997) {
        xmp_restart_module(a); xmp_restart_module(b);
    }
}
static void start(xmp_context a, xmp_context b, int interpolation, int format)
{
    ((struct context_data *)a)->rng.state = ((struct context_data *)b)->rng.state = 0x1234abcd;
    CHECK(xmp_start_player(a,16000,format)==0);
    CHECK(xmp_start_player(b,16000,format)==0);
    CHECK(xmp_set_player(a,XMP_PLAYER_INTERP,interpolation)==0);
    CHECK(xmp_set_player(b,XMP_PLAYER_INTERP,interpolation)==0);
    CHECK(xmp_set_player(a,XMP_PLAYER_DSP,0)==0);
    CHECK(xmp_set_player(b,XMP_PLAYER_DSP,0)==0);
}
int main(int argc,char **argv)
{
    pvzh1_xmp_module *module;
    pvzh1_xmp_module_stats stats;
    xmp_context independent[2], shared[2], extra;
    uint32_t payload;
    unsigned phase,i,j,k;
    CHECK(argc==2);
    module=pvzh1_xmp_module_load(argv[1]); CHECK(module);
    CHECK(pvzh1_xmp_module_get_stats(module,&stats)==0);
    printf("{\"tracks\":%u,\"unique_tracks\":%u,\"original_track_bytes\":%zu,\"unique_track_bytes\":%zu,\"sample_bytes\":%zu,",
           stats.tracks,stats.unique_tracks,stats.original_track_bytes,stats.unique_track_bytes,stats.sample_bytes);
    payload=pvzh1_xmp_module_payload_hash(module);
    for(i=0;i<2;++i){
        independent[i]=xmp_create_context(); CHECK(independent[i]);
        CHECK(xmp_load_module(independent[i],argv[1])==0);
        shared[i]=pvzh1_xmp_module_create_player(module); CHECK(shared[i]);
        CHECK(((struct context_data *)independent[i])->m.mod.chn==((struct context_data *)shared[i])->m.mod.chn);
        CHECK(((struct context_data *)independent[i])->m.mod.len==((struct context_data *)shared[i])->m.mod.len);
        CHECK(i==0 || ((struct context_data *)shared[i])->m.scan_cnt != ((struct context_data *)shared[i^1])->m.scan_cnt);
        {
            const struct xmp_module *a=&((struct context_data *)independent[i])->m.mod;
            const struct xmp_module *b=&((struct context_data *)shared[i])->m.mod;
            int t;
            CHECK(a->trk==b->trk && a->pat==b->pat && a->smp==b->smp && a->ins==b->ins);
            CHECK(!memcmp(a->xxo,b->xxo,sizeof(a->xxo)));
            for(t=0;t<a->trk;++t){
                CHECK(a->xxt[t]->rows==b->xxt[t]->rows);
                CHECK(!memcmp(a->xxt[t]->event,b->xxt[t]->event,(size_t)a->xxt[t]->rows*sizeof(struct xmp_event)));
            }
            for(t=0;t<a->pat;++t){
                CHECK(a->xxp[t]->rows==b->xxp[t]->rows);
                CHECK(!memcmp(a->xxp[t]->index,b->xxp[t]->index,(size_t)a->chn*sizeof(int)));
            }
            for(t=0;t<a->smp;++t){
                CHECK(a->xxs[t].len==b->xxs[t].len && a->xxs[t].flg==b->xxs[t].flg);
                if(a->xxs[t].data){
                    CHECK(b->xxs[t].data);
                    CHECK(!memcmp(a->xxs[t].data,b->xxs[t].data,(size_t)a->xxs[t].len*((a->xxs[t].flg&XMP_SAMPLE_16BIT)?2:1)));
                }
            }
        }
    }
    CHECK(((struct context_data *)shared[0])->m.mod.xxt==((struct context_data *)shared[1])->m.mod.xxt);
    CHECK(((struct context_data *)shared[0])->p.scan!=((struct context_data *)shared[1])->p.scan);
    for(phase=0;phase<3;++phase){
        int format=phase==2?0:XMP_FORMAT_MONO;
        for(i=0;i<2;++i)start(independent[i],shared[i],(int)phase,format);
        for(j=0;j<1200;++j)for(i=0;i<2;++i){
            unsigned char expected[1024],actual[1024];
            int bytes=(j&1)?512:1024;
            same_control(independent[i],shared[i],j,i);
            memset(expected,0xa5,sizeof(expected));memset(actual,0xa5,sizeof(actual));
            CHECK(xmp_play_buffer(independent[i],expected,bytes,0)==xmp_play_buffer(shared[i],actual,bytes,0));
            if(memcmp(expected,actual,sizeof(expected))){fprintf(stderr,"PCM mismatch phase%u step%u player%u\n",phase,j,i);return 1;}
            for(k=0;k<(unsigned)bytes;++k){pcm_hash^=actual[k];pcm_hash*=16777619u;}
            ++blocks;
        }
        for(i=0;i<2;++i){xmp_end_player(independent[i]);xmp_end_player(shared[i]);}
        CHECK(pvzh1_xmp_module_payload_hash(module)==payload);
    }
    /* Halt/recreate preserves owner; free-owner-before-player is deferred. */
    pvzh1_xmp_module_free_player(shared[1]);
    extra=pvzh1_xmp_module_create_player(module); CHECK(extra);
    CHECK(pvzh1_xmp_module_get_stats(module,&stats)==0 && stats.players==2);
    pvzh1_xmp_module_free(module);
    CHECK(!pvzh1_xmp_module_create_player(module));
    start(independent[0],shared[0],XMP_INTERP_LINEAR,XMP_FORMAT_MONO);
    { short a[256],b[256];CHECK(xmp_play_buffer(independent[0],a,sizeof(a),0)==xmp_play_buffer(shared[0],b,sizeof(b),0));CHECK(!memcmp(a,b,sizeof(a))); }
    for(i=0;i<2;++i)xmp_free_context(independent[i]);
    pvzh1_xmp_module_free_player(extra);
    pvzh1_xmp_module_free_player(shared[0]);
    printf("\"compared_blocks\":%u,\"pcm_fnv1a\":\"%08x\",\"payload_fnv1a\":\"%08x\",\"passed\":true}\n",blocks,pcm_hash,payload);
    return 0;
}
