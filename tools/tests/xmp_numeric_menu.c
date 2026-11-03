#include "common.h"
#include "xmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <io.h>
#include <fcntl.h>
#ifdef PVZH1_CANDIDATE
#include "xmp_numeric_cache.h"
#endif
static uint64_t pow_calls;
extern double __real_pow(double,double);
double __wrap_pow(double a,double b){++pow_calls;return __real_pow(a,b);}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"menu failed line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
int main(int argc,char **argv){xmp_context player;unsigned i;uint32_t hash=2166136261u;int low=INT_MAX,high=-1;
    _setmode(_fileno(stdout),_O_BINARY);
    CHECK(argc==2);player=xmp_create_context();CHECK(player);CHECK(!xmp_load_module(player,argv[1]));((struct context_data*)player)->rng.state=0x1234abcd;
    CHECK(!xmp_start_player(player,16000,XMP_FORMAT_MONO));CHECK(!xmp_set_player(player,XMP_PLAYER_INTERP,XMP_INTERP_LINEAR));CHECK(!xmp_set_player(player,XMP_PLAYER_DSP,0));CHECK(xmp_set_position(player,152)>=0);CHECK(!xmp_set_row(player,0));
    for(i=0;i<7200;++i){unsigned char pcm[1024];int bytes=(i&1)?512:1024,code;struct xmp_frame_info frame;uint32_t header[9];unsigned k;
        memset(pcm,0xa5,sizeof(pcm));code=xmp_play_buffer(player,pcm,bytes,0);CHECK(!code);xmp_get_frame_info(player,&frame);
        header[0]=i;header[1]=(uint32_t)bytes;header[2]=(uint32_t)frame.pos;header[3]=(uint32_t)frame.row;header[4]=(uint32_t)frame.frame;header[5]=(uint32_t)frame.speed;header[6]=(uint32_t)frame.bpm;header[7]=(uint32_t)frame.time;header[8]=(uint32_t)frame.loop_count;
        CHECK(fwrite(header,sizeof(header),1,stdout)==1&&fwrite(pcm,sizeof(pcm),1,stdout)==1);
        if(frame.pos<low)low=frame.pos;
        if(frame.pos>high)high=frame.pos;
        for(k=0;k<(unsigned)bytes;++k){hash^=pcm[k];hash*=16777619u;}
    }
    xmp_free_context(player);
    fprintf(stderr,"{\"passed\":true,\"blocks\":7200,\"pcm_fnv1a\":\"%08x\",\"order_min\":%d,\"order_max\":%d,\"pow_calls\":%llu",hash,low,high,(unsigned long long)pow_calls);
#ifdef PVZH1_CANDIDATE
    {H1XmpNumericStats note=pvzh1_xmp_note_cache_stats(),step=pvzh1_xmp_step_cache_stats();fprintf(stderr,",\"note_calls\":%llu,\"note_hits\":%llu,\"step_calls\":%llu,\"step_hits\":%llu",(unsigned long long)note.calls,(unsigned long long)note.hits,(unsigned long long)step.calls,(unsigned long long)step.hits);}
#endif
    fprintf(stderr,"}\n");return 0;
}
