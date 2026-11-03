#include "xmp_numeric_cache.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
extern double libxmp_note_to_period_mix(int,int);
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"unit failed line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static double plain(int n,int b){double d=(double)n+(double)b/12800;return 13696.0/pow(2,d/12);}
int main(void){
    H1XmpNoteCache notes={0};H1XmpStepCache steps={0};double out=0;unsigned i;uint32_t random=0x12345678u;
    CHECK(sizeof(H1XmpNoteEntry)==16&&sizeof(H1XmpStepEntry)==24);
    pvzh1_xmp_note_cache_reset();pvzh1_xmp_step_cache_reset();
    for(i=0;i<120000;++i){int n,b;double actual,expected;
        random=random*1664525u+1013904223u;n=(int)((random>>16)%140u)-10;
        random=random*1664525u+1013904223u;b=(int)((random>>16)%204800u)-102400;
        if(i%3==0){n=60;b=0;}expected=plain(n,b);actual=libxmp_note_to_period_mix(n,b);
        CHECK(h1_xmp_bits(expected)==h1_xmp_bits(actual));
    }
    /* Direct collision/replacement and all key fields, without approximating
       int pairs or comparing period by floating equality. */
    for(i=0;i<8000;++i){int n=(int)i-4000,b=(int)(i*137u);double value=(double)i+1;
        h1_xmp_note_write(&notes,n,b,value);CHECK(h1_xmp_note_read(&notes,n,b,&out));CHECK(h1_xmp_bits(out)==h1_xmp_bits(value));
        CHECK(!h1_xmp_note_read(&notes,n,b+1,&out));
    }
    for(i=0;i<8000;++i){double p=(double)(i%1024u)+1;double value=428.0*8363/16000/p;
        h1_xmp_step_write(&steps,8363,16000,p,value);CHECK(h1_xmp_step_read(&steps,8363,16000,p,&out));CHECK(h1_xmp_bits(out)==h1_xmp_bits(value));
        CHECK(!h1_xmp_step_read(&steps,8364,16000,p,&out));CHECK(!h1_xmp_step_read(&steps,8363,16001,p,&out));
        CHECK(!h1_xmp_step_read(&steps,8363,16000,h1_xmp_double(h1_xmp_bits(p)+1),&out));
    }
    {const uint64_t bad[]={0,0x8000000000000000ull,1,0x000fffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,0x7ff8000012345678ull,0xbff0000000000000ull};
        for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i){double value=h1_xmp_double(bad[i]);
            h1_xmp_note_write(&notes,INT_MIN+(int)i,INT_MAX,value);CHECK(!h1_xmp_note_read(&notes,INT_MIN+(int)i,INT_MAX,&out));
            h1_xmp_step_write(&steps,8363,16000,value,1);CHECK(!h1_xmp_step_read(&steps,8363,16000,value,&out));
            h1_xmp_step_write(&steps,8363,16000,2000+(double)i,value);CHECK(!h1_xmp_step_read(&steps,8363,16000,2000+(double)i,&out));
        }
    }
    h1_xmp_step_write(&steps,0,16000,428,1);CHECK(!h1_xmp_step_read(&steps,0,16000,428,&out));
    h1_xmp_step_write(&steps,8363,0,428,1);CHECK(!h1_xmp_step_read(&steps,8363,0,428,&out));
    {H1XmpNumericStats before=pvzh1_xmp_note_cache_stats(),after;
        const int extreme[]={INT_MIN,INT_MAX};for(i=0;i<2;++i){double expected=plain(extreme[i],0),actual=libxmp_note_to_period_mix(extreme[i],0);CHECK(h1_xmp_bits(actual)==h1_xmp_bits(expected));}
        after=pvzh1_xmp_note_cache_stats();CHECK(after.rejected>=before.rejected+2);
        pvzh1_xmp_note_cache_reset();pvzh1_xmp_step_cache_reset();after=pvzh1_xmp_note_cache_stats();CHECK(!after.calls&&!after.hits&&!after.stores&&!after.rejected);
    }
    printf("{\"passed\":true,\"checks\":%u,\"note_entry_bytes\":%zu,\"step_entry_bytes\":%zu,\"note_table_with_stats\":%zu,\"step_table_with_stats\":%zu}\n",checks,sizeof(H1XmpNoteEntry),sizeof(H1XmpStepEntry),sizeof(H1XmpNoteCache),sizeof(H1XmpStepCache));return 0;
}
