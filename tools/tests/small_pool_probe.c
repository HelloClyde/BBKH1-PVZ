#include "small_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NATIVE 4096u
#define GUARD 16u
static unsigned long long checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct NativeBlock {void *raw;unsigned char *memory;size_t requested,reserved;int active;} NativeBlock;
typedef struct Provider {
    NativeBlock blocks[MAX_NATIVE];
    size_t calls,releases,current,peak,active,fail_next;
    int sentinel,reenter;
    PvzSmallPool *pool;
} Provider;
static Provider provider;
static PvzSmallPool pool;
typedef struct Live {unsigned char *memory;PvzSmallPoolOwner *owner;size_t size;unsigned char pattern;} Live;
static Live live[30000];
static uint32_t random_state=123456789u;
static uint32_t random_number(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static void reentry(Provider *p){
    if(p->reenter){PvzSmallPoolOwner *owner=(void*)(uintptr_t)16;
        CHECK(!pvz_small_pool_allocate(p->pool,32,8,&owner)&&!owner);
        CHECK(pvz_small_pool_trim(p->pool)==0);
    }
}
static void *acquire(void *context,size_t bytes,size_t *reserved){
    Provider *p=context;unsigned i;unsigned char *raw,*memory;size_t span;
    ++p->calls;reentry(p);
    if(p->fail_next){--p->fail_next;return p->sentinel?(void*)(uintptr_t)UINT32_MAX:0;}
    for(i=0;i<MAX_NATIVE&&p->blocks[i].active;++i){}
    CHECK(i<MAX_NATIVE);span=bytes+((p->calls%3u)*100u);
    raw=malloc(span+96u);CHECK(raw!=0);
    /* Simulate SDK raw memory with only four-byte alignment. */
    memory=(unsigned char *)(((uintptr_t)raw+GUARD+31u)&~(uintptr_t)31u)+4;
    memset(memory-GUARD,0x6d,GUARD);memset(memory,0xa7,span);memset(memory+span,0x39,GUARD);
    p->blocks[i]=(NativeBlock){raw,memory,bytes,span,1};
    p->current+=span;++p->active;if(p->current>p->peak)p->peak=p->current;
    *reserved=span;return memory;
}
static void release(void *context,void *memory,size_t reserved){
    Provider *p=context;unsigned i,j;NativeBlock *block;
    for(i=0;i<MAX_NATIVE;++i)if(p->blocks[i].active&&p->blocks[i].memory==memory)break;
    CHECK(i<MAX_NATIVE);block=&p->blocks[i];CHECK(reserved==block->reserved);
    for(j=0;j<GUARD;++j){CHECK(block->memory[-(int)GUARD+(int)j]==0x6d);CHECK(block->memory[reserved+j]==0x39);}
    for(j=(unsigned)block->requested;j<reserved;++j)CHECK(block->memory[j]==0xa7);
    p->current-=reserved;--p->active;++p->releases;block->active=0;
    reentry(p);free(block->raw);
}
static void initialize(void){
    CHECK(provider.active==0&&provider.current==0);
    memset(&provider,0,sizeof(provider));provider.pool=&pool;
    pvz_small_pool_init(&pool,acquire,release,&provider);
}
static void finish(void){
    PvzSmallPoolStats stats;
    CHECK(pvz_small_pool_dispose(&pool));pvz_small_pool_get_stats(&pool,&stats);
    CHECK(stats.pages==0&&stats.live_allocations==0&&stats.live_class_bytes==0);
    CHECK(stats.reserved_bytes==0&&stats.requested_bytes==0);
    CHECK(provider.active==0&&provider.current==0&&provider.calls==provider.releases+stats.acquire_failures);
}
static void fill_live(Live *item,size_t size,size_t alignment,unsigned char pattern){
    item->memory=pvz_small_pool_allocate(&pool,size,alignment,&item->owner);
    CHECK(item->memory&&item->owner);CHECK(((uintptr_t)item->memory&(alignment-1u))==0);
    CHECK(((uintptr_t)item->owner&31u)==0);item->size=size;item->pattern=pattern;
    memset(item->memory,pattern,size);
}
static void verify_live(const Live *item){size_t j;for(j=0;j<item->size;++j)CHECK(item->memory[j]==item->pattern);}
static void free_live(Live *item){verify_live(item);CHECK(pvz_small_pool_free(&pool,item->memory,item->owner));item->memory=0;}

int main(void){
    size_t size,alignment,i,j;PvzSmallPoolOwner *owner;PvzSmallPoolStats stats;size_t tiny_calls,tiny_peak;
    initialize();
    CHECK(pvz_small_pool_capacity(1,8)==8&&pvz_small_pool_capacity(129,8)==160);
    CHECK(pvz_small_pool_capacity(1025,8)==1280&&pvz_small_pool_capacity(4096,32)==4096);
    {void *zero=pvz_small_pool_allocate(&pool,0,32,&owner);CHECK(zero&&owner&&((uintptr_t)zero&31u)==0);CHECK(pvz_small_pool_free(&pool,zero,owner));}
    for(alignment=8;alignment<=32;alignment*=2)for(size=0;size<=4096;++size){
        Live item={0};size_t n=size?size:1;
        fill_live(&item,n,alignment,(unsigned char)(size+alignment));free_live(&item);
    }
    pvz_small_pool_get_stats(&pool,&stats);CHECK(stats.pages==56&&stats.live_allocations==0);
    CHECK(pvz_small_pool_trim(&pool)==provider.peak&&provider.active==0);finish();

    /* Exhaust full pages, free in interleaved order, preserve all live bytes. */
    initialize();for(i=0;i<257;++i)fill_live(&live[i],4096,32,(unsigned char)i);
    CHECK(!pvz_small_pool_dispose(&pool));
    for(i=0;i<257;++i){j=(i*17u)%257u;free_live(&live[j]);}
    pvz_small_pool_get_stats(&pool,&stats);CHECK(stats.pages==1&&provider.active==1);
    finish();

    initialize();memset(live,0,sizeof(live));
    for(i=0;i<60000;++i){
        j=random_number()%512u;
        if(live[j].memory)free_live(&live[j]);
        else {
            size_t n=1u+random_number()%4096u;alignment=8u<<(random_number()%3u);
            fill_live(&live[j],n,alignment,(unsigned char)random_number());
            {size_t k;uintptr_t a=(uintptr_t)live[j].memory;
                for(k=0;k<512;++k)if(k!=j&&live[k].memory){
                    uintptr_t b=(uintptr_t)live[k].memory;
                    CHECK(a+n<=b||b+live[k].size<=a);
                }
            }
        }
        if(i%137u==0){for(j=0;j<512;++j)if(live[j].memory)verify_live(&live[j]);pvz_small_pool_trim(&pool);}
    }
    for(j=0;j<512;++j)if(live[j].memory)free_live(&live[j]);
    finish();

    /* Failure leaves owner clear, existing live bytes valid, and permits caller
     * fallback. Large/unsupported requests never invoke the native page API. */
    initialize();provider.fail_next=1;owner=(void*)(uintptr_t)32;
    CHECK(!pvz_small_pool_allocate(&pool,64,8,&owner)&&!owner&&provider.active==0);
    provider.fail_next=1;provider.sentinel=1;
    CHECK(!pvz_small_pool_allocate(&pool,64,8,&owner)&&!owner&&provider.active==0);
    CHECK(provider.calls==2);
    for(size=4097;size<=8192;size+=4095)CHECK(!pvz_small_pool_allocate(&pool,size,8,&owner)&&!owner);
    for(alignment=0;alignment<=64;alignment+=4)if(alignment!=8&&alignment!=16&&alignment!=32)
        CHECK(!pvz_small_pool_allocate(&pool,64,alignment,&owner)&&!owner);
    CHECK(provider.calls==2);
    {size_t span=0;void *fallback=acquire(&provider,8192,&span);CHECK(fallback!=0);release(&provider,fallback,span);}
    fill_live(&live[0],64,32,0x93);provider.fail_next=1;
    CHECK(!pvz_small_pool_allocate(&pool,4096,8,&owner)&&!owner);verify_live(&live[0]);
    free_live(&live[0]);finish();

    /* Wrong slot/double-free while the owner page is still retained. */
    initialize();fill_live(&live[0],128,16,0x81);fill_live(&live[1],128,16,0x82);
    CHECK(!pvz_small_pool_free(&pool,live[0].memory+8,live[0].owner));
    {unsigned char *raw=live[0].memory;owner=live[0].owner;free_live(&live[0]);CHECK(!pvz_small_pool_free(&pool,raw,owner));}
    verify_live(&live[1]);free_live(&live[1]);finish();

    initialize();provider.reenter=1;fill_live(&live[0],32,32,0x12);free_live(&live[0]);finish();

    /* A concrete 30,000-small-object workload: every allocation would otherwise
     * call the native provider once. Only page refills do so here. */
    initialize();for(i=0;i<30000;++i)fill_live(&live[i],32,8,(unsigned char)i);
    tiny_calls=provider.calls;tiny_peak=provider.peak;CHECK(tiny_calls<100);
    for(i=0;i<30000;++i)free_live(&live[(i*7919u)%30000u]);
    pvz_small_pool_get_stats(&pool,&stats);CHECK(stats.pages==1);finish();
    printf("{\"passed\":true,\"checks\":%llu,\"tiny_objects\":30000,\"tiny_native_acquires\":%lu,\"tiny_native_reserved_peak\":%lu,\"classes\":56,\"device_used\":false}\n",
        checks,(unsigned long)tiny_calls,(unsigned long)tiny_peak);
    return 0;
}
