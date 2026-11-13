/* Actual shared-XM loader: reused static FILE buffer ownership and original-player PCM. */
#include "xmp_shared.h"
#include "../../vendor/libxmp/src/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
extern volatile uint32_t pvzh1_xmp_load_diagnostic[12];
static FILE *tracked;
static void *tracked_buffer;
static int observing,fail_setvbuf,installed;
static unsigned opens,closes,buffer_frees,setvbuf_calls;
static unsigned reads,seeks,zero_cur_seeks,byte_reads,scan_calls;
static unsigned long long read_bytes;
static uint32_t scan_ms;
uint32_t pvzp_ticks_ms(void){return (uint32_t)((double)clock()*1000/CLOCKS_PER_SEC);}
void *__real_malloc(size_t);void __real_free(void *);
int __real_setvbuf(FILE *,char *,int,size_t);
FILE *__real_fopen(const char *,const char *);int __real_fclose(FILE *);
size_t __real_fread(void *,size_t,size_t,FILE *);int __real_fseek(FILE *,long,int);int __real_fgetc(FILE *);
int __real_libxmp_scan_sequences(struct context_data *);
void *__wrap_malloc(size_t n){return __real_malloc(n);}
void __wrap_free(void *p){
    if(p&&p==tracked_buffer){++buffer_frees;CHECK(0);}
    __real_free(p);
}
int __wrap_setvbuf(FILE *f,char *p,int mode,size_t n){
    if(observing&&f==tracked){
        ++setvbuf_calls;CHECK(p&&mode==_IOFBF&&n==16384u);
        if(tracked_buffer)CHECK(p==tracked_buffer);else tracked_buffer=p;
        if(fail_setvbuf)return -1;
        {int result=__real_setvbuf(f,p,mode,n);if(!result)installed=1;return result;}
    }
    return __real_setvbuf(f,p,mode,n);
}
FILE *__wrap_fopen(const char *path,const char *mode){
    FILE *f=__real_fopen(path,mode);
    if(observing){CHECK(!tracked);if(f){tracked=f;++opens;}}
    return f;
}
int __wrap_fclose(FILE *f){
    if(observing&&f==tracked){++closes;tracked=NULL;}
    return __real_fclose(f);
}
size_t __wrap_fread(void *p,size_t size,size_t count,FILE *f){
    size_t result=__real_fread(p,size,count,f);
    if(observing&&f==tracked){++reads;read_bytes+=(unsigned long long)size*result;}
    return result;
}
int __wrap_fseek(FILE *f,long offset,int origin){
    if(observing&&f==tracked){++seeks;if(!offset&&origin==SEEK_CUR)++zero_cur_seeks;}
    return __real_fseek(f,offset,origin);
}
int __wrap_fgetc(FILE *f){if(observing&&f==tracked)++byte_reads;return __real_fgetc(f);}
int __wrap_libxmp_scan_sequences(struct context_data *ctx){
    uint32_t start=pvzp_ticks_ms();int result=__real_libxmp_scan_sequences(ctx);
    if(observing){++scan_calls;scan_ms+=pvzp_ticks_ms()-start;}
    return result;
}
static void begin(int mode){
    CHECK(!tracked);observing=1;fail_setvbuf=mode==1;installed=0;
    opens=closes=buffer_frees=setvbuf_calls=0;
    reads=seeks=zero_cur_seeks=byte_reads=scan_calls=scan_ms=0;read_bytes=0;
}
static void compare(xmp_context reference,xmp_context candidate){
    unsigned i;short a[256],b[256];
    ((struct context_data *)reference)->rng.state=((struct context_data *)candidate)->rng.state=0x714ac39u;
    CHECK(!xmp_start_player(reference,16000,XMP_FORMAT_MONO));
    CHECK(!xmp_start_player(candidate,16000,XMP_FORMAT_MONO));
    CHECK(!xmp_set_player(reference,XMP_PLAYER_INTERP,XMP_INTERP_LINEAR));
    CHECK(!xmp_set_player(candidate,XMP_PLAYER_INTERP,XMP_INTERP_LINEAR));
    for(i=0;i<800;++i){
        struct xmp_frame_info fa,fb;
        if(i%137==0){int p=(i/137*17)%((struct context_data *)reference)->m.mod.len;
            CHECK(xmp_set_position(reference,p)==xmp_set_position(candidate,p));
            CHECK(xmp_set_row(reference,(int)(i%31))==xmp_set_row(candidate,(int)(i%31)));
            CHECK(xmp_play_buffer(reference,NULL,0,0)==xmp_play_buffer(candidate,NULL,0,0));}
        CHECK(xmp_play_buffer(reference,a,sizeof(a),0)==xmp_play_buffer(candidate,b,sizeof(b),0));
        CHECK(!memcmp(a,b,sizeof(a)));xmp_get_frame_info(reference,&fa);xmp_get_frame_info(candidate,&fb);
        CHECK(fa.pos==fb.pos&&fa.row==fb.row&&fa.frame==fb.frame);
    }
    xmp_end_player(reference);xmp_end_player(candidate);
}
int main(int argc,char **argv){
    xmp_context reference;uint32_t expected=0;unsigned mode;
    CHECK(argc==3);reference=xmp_create_context();CHECK(reference);
    CHECK(!xmp_load_module(reference,argv[1]));
    printf("{\"test_only\":true,\"modes\":[");
    for(mode=0;mode<3;++mode){
        pvzh1_xmp_module *module;xmp_context player;uint32_t payload;unsigned prior_close;
        begin((int)mode);prior_close=pvzh1_xmp_load_diagnostic[9];
        module=pvzh1_xmp_module_load(argv[1]);observing=0;CHECK(module);
        CHECK(opens==1&&closes==1&&!tracked&&tracked_buffer);
        CHECK(buffer_frees==0&&setvbuf_calls==1);
        CHECK(pvzh1_xmp_load_diagnostic[0]==5&&pvzh1_xmp_load_diagnostic[9]==prior_close+1);
        CHECK(pvzh1_xmp_load_diagnostic[5]==(mode==1?0u:16384u));
        CHECK(pvzh1_xmp_load_diagnostic[6]>0&&!pvzh1_xmp_load_diagnostic[10]);
        payload=pvzh1_xmp_module_payload_hash(module);if(!mode)expected=payload;CHECK(payload==expected);
        player=pvzh1_xmp_module_create_player(module);CHECK(player);compare(reference,player);
        printf("%s{\"mode\":%u,\"buffer_bytes\":%u,\"file_bytes\":%u,\"fread_calls\":%u,\"fread_payload_bytes\":%llu,\"fgetc_calls\":%u,\"fseek_calls\":%u,\"zero_current_seeks\":%u,\"scan_calls\":%u,\"host_scan_ms\":%u,\"pcm_order_blocks_equal\":800}",
            mode?",":"",mode,mode==1?0u:16384u,(unsigned)pvzh1_xmp_load_diagnostic[6],reads,read_bytes,byte_reads,seeks,zero_cur_seeks,scan_calls,scan_ms);
        pvzh1_xmp_module_free_player(player);pvzh1_xmp_module_free(module);
    }
    begin(0);CHECK(!pvzh1_xmp_module_load("build/xmp-buffered-io-test/nonexistent.xm"));observing=0;
    CHECK(!opens&&!closes&&!tracked&&buffer_frees==0&&pvzh1_xmp_load_diagnostic[0]==6);
    begin(0);CHECK(!pvzh1_xmp_module_load(argv[2]));observing=0;
    CHECK(opens==1&&closes==1&&buffer_frees==0&&!tracked&&tracked_buffer&&pvzh1_xmp_load_diagnostic[0]==6);
    xmp_free_context(reference);puts("],\"static_buffer_reused_and_never_freed\":true,\"missing_and_invalid_file_closed\":true,\"all_passed\":true}");return 0;
}
