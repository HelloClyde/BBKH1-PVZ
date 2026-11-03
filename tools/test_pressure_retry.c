#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void *pvz_runtime_allocate_with_pressure(size_t,size_t,void *(*)(size_t,size_t,uintptr_t *),
    size_t (*)(size_t),volatile uint32_t [6],int *,uintptr_t *);

enum { CELLS=256, CELL_BYTES=4, RELEASES=8 };
static unsigned char memory[CELLS*CELL_BYTES],used[CELLS];
static unsigned int release_start[RELEASES],release_size[RELEASES],release_count;
static unsigned int callbacks,native_calls,release_index,reenter,all_fail;
static int running;
static volatile uint32_t diagnostic[6];
static size_t expected_user,expected_native;
static void *allocate(size_t,size_t,uintptr_t *);
static size_t reclaim(size_t);

static int failed(const void *p){return !p||(uintptr_t)p==UINT32_MAX;}
static void reset(void)
{
    memset(used,1,sizeof(used));memset((void *)diagnostic,0,sizeof(diagnostic));
    memset(release_start,0,sizeof(release_start));
    memset(release_size,0,sizeof(release_size));
    callbacks=native_calls=release_index=release_count=reenter=all_fail=0;
    running=0;expected_user=expected_native=0;
}
static void free_range(unsigned int start,unsigned int bytes)
{
    assert(!(start%CELL_BYTES)&&!(bytes%CELL_BYTES)&&start+bytes<=sizeof(memory));
    memset(used+start/CELL_BYTES,0,bytes/CELL_BYTES);
}
static void add_release(unsigned int start,unsigned int bytes)
{
    assert(release_count<RELEASES);
    release_start[release_count]=start;release_size[release_count++]=bytes;
}
static void *allocate(size_t user,size_t bytes,uintptr_t *span)
{
    size_t first,n=(bytes+CELL_BYTES-1u)/CELL_BYTES;
    ++native_calls;
    *span=(uintptr_t)(user*2u+1u);
    if(expected_user)assert(user==expected_user||(user==8&&running));
    if(expected_native)assert(bytes==expected_native||(bytes==39&&running));
    /* Both callback and native retry must execute under the same guard. */
    if(native_calls>1)assert(running);
    if(all_fail)return all_fail==2?0:(void *)(uintptr_t)UINT32_MAX;
    if(reenter==2&&running){
        void *nested;uintptr_t nested_span;
        reenter=0;all_fail=1;
        nested=pvz_runtime_allocate_with_pressure(8,39,allocate,reclaim,diagnostic,&running,&nested_span);
        all_fail=0;assert(failed(nested)&&running);
    }
    for(first=0;first+n<=CELLS;++first){
        size_t i;for(i=0;i<n&&!used[first+i];++i);
        if(i==n){memset(used+first,1,n);return memory+first*CELL_BYTES;}
    }
    return (void *)(uintptr_t)UINT32_MAX;
}
static size_t reclaim(size_t requested)
{
    unsigned int index;
    assert(running&&requested==expected_user);++callbacks;
    if(reenter==1){
        void *nested;uintptr_t nested_span;
        reenter=0;all_fail=1;
        nested=pvz_runtime_allocate_with_pressure(8,39,allocate,reclaim,diagnostic,&running,&nested_span);
        all_fail=0;assert(failed(nested)&&running);
    }
    if(release_index==release_count)return 0;
    index=release_index++;free_range(release_start[index],release_size[index]);
    return release_size[index];
}
static void *run(size_t user,size_t raw)
{
    void *p;uintptr_t span;
    expected_user=user;expected_native=raw;
    p=pvz_runtime_allocate_with_pressure(user,raw,allocate,reclaim,diagnostic,&running,&span);
    assert(!running&&callbacks==diagnostic[0]);
    if(!failed(p))assert(span==user*2u+1u);
    return p;
}
static size_t lying_reclaim(size_t requested)
{
    (void)requested;assert(running);++callbacks;return 1;
}

int main(void)
{
    void *p;uintptr_t span;
    /* A normal allocation never enters reclamation. */
    reset();free_range(0,1024);p=run(200,231);
    assert(p==memory&&native_calls==1&&!callbacks&&!diagnostic[1]);

    /* More bytes released than the request still do not imply a fit.  Two
       separate releases and three actual native calls coalesce a 300B span;
       the next eligible image is retained once allocation succeeds. */
    reset();free_range(100,100);free_range(300,100);
    add_release(600,100);add_release(200,100);add_release(800,100);
    p=run(200,231);
    assert(p==memory+100&&native_calls==3&&callbacks==2&&release_index==2);
    assert(diagnostic[1]==2&&diagnostic[2]==1&&diagnostic[3]==200&&diagnostic[4]==200);
    assert(used[800/CELL_BYTES]);

    /* Aggregate free bytes exceed the raw request, yet no contiguous span
       fits.  A terminal zero stops retries without falsely reporting success. */
    reset();free_range(100,100);free_range(400,100);free_range(900,100);
    add_release(700,100);p=run(300,331);
    assert(failed(p)&&native_calls==2&&callbacks==2);
    assert(diagnostic[1]==1&&!diagnostic[2]&&diagnostic[3]==100);

    reset();p=run(20,51);
    assert(failed(p)&&native_calls==1&&callbacks==1&&!diagnostic[1]);
    reset();all_fail=2;p=run(20,51);
    assert(!p&&native_calls==1&&callbacks==1&&!diagnostic[1]);

    /* Nested failures from the callback and from the allocator retry cannot
       recurse into cache reclamation or clear the outer guard. */
    reset();add_release(100,100);reenter=1;p=run(32,63);
    assert(p==memory+100&&native_calls==3&&callbacks==1&&diagnostic[2]==1);
    reset();add_release(100,100);reenter=2;p=run(32,63);
    assert(p==memory+100&&native_calls==3&&callbacks==1&&diagnostic[2]==1);

    /* Native allocation can succeed despite no callback implementation. */
    reset();free_range(0,1024);
    p=pvz_runtime_allocate_with_pressure(16,47,allocate,0,diagnostic,&running,&span);
    assert(p==memory&&!running&&!callbacks);
    reset();p=pvz_runtime_allocate_with_pressure(16,47,allocate,0,diagnostic,&running,&span);
    assert(failed(p)&&!running&&!callbacks);

    /* Saturation is independent of native success and the hard bound stops a
       faulty positive callback that never makes progress. */
    reset();all_fail=1;diagnostic[3]=UINT32_MAX-1;
    p=pvz_runtime_allocate_with_pressure(20,51,allocate,lying_reclaim,diagnostic,&running,&span);
    assert(failed(p)&&!running&&callbacks==65536&&native_calls==65537);
    assert(diagnostic[0]==65536&&diagnostic[1]==65536&&!diagnostic[2]);
    assert(diagnostic[3]==UINT32_MAX);

    puts("pressure retry passed: early success, coalescing, fragmented/terminal, reentry, optional hook, bounded progress");
    return 0;
}
