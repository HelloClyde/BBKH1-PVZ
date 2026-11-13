/* H1/Y100 V1.41 bridges for newlib. No desktop operating system is assumed.
   Native stdio owns the firmware filesystem lock; stat alone needs the native
   semaphore, as independently exercised by h1-debug/files.c. */
#include "runtime.h"
#include "small_pool.h"
#include "asset_index.h"
#include "h1_sdk.h"
#include <errno.h>
#include <fcntl.h>
#include <reent.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/dirent.h>
#include <time.h>
#include <unistd.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 4 /* Newlib's documented POSIX clock identifier. */
#endif

#define NATIVE_PATH_MAX 512
#define FILE_COUNT 64
#define ALLOC_MAGIC 0x50565a41u
static h1_file *files[FILE_COUNT],*log_file;
static char *directory_paths[FILE_COUNT];
static int descriptor_flags[FILE_COUNT];
static char virtual_cwd[NATIVE_PATH_MAX]="/PVZ";
/* Native H1 filesystem names are GBK (the verified H1Debug file protocol also
   passes GBK directly to these SDK services). Keep this fixed prefix as bytes,
   independent of the compiler's UTF-8 source execution character set:
   <drive>:\\[ying yong]\\[shu ju]\\[you xi]\\PVZ. */
#define PVZ_NATIVE_DATA_SUFFIX "\\\xd3\xa6\xd3\xc3\\\xca\xfd\xbe\xdd\\\xd3\xce\xcf\xb7\\PVZ"
static const char native_data_a[]="A:" PVZ_NATIVE_DATA_SUFFIX;
static const char native_data_b[]="B:" PVZ_NATIVE_DATA_SUFFIX;
static const char native_probe_a[]="A:" PVZ_NATIVE_DATA_SUFFIX "\\properties\\resources.xml";
static const char native_probe_b[]="B:" PVZ_NATIVE_DATA_SUFFIX "\\properties\\resources.xml";
static const char *resource_prefix=native_data_a;
static int resource_drive_detected;
static int asset_bucket_state;
static uint32_t last_tick,tick_high;
typedef union Allocation Allocation;
union Allocation {struct {uint32_t magic,size;void *raw;Allocation *next,*prev;uintptr_t raw_span;} f;uint64_t alignment;unsigned char bytes[24];};
_Static_assert(_Alignof(max_align_t)==8,"Review the native malloc alignment contract");
_Static_assert(sizeof(Allocation)==24,"Native allocation header must stay 24 bytes");
static Allocation *allocations;
/* raw_span bit0 distinguishes a stable small-pool owner from a four-byte
   aligned native span. It uses the existing 24-byte o32 allocation header. */
static PvzSmallPool small_pool;
static int small_pool_ready;
static uint32_t small_pool_fallbacks,small_pool_trimmed;
/* magic, pages/peak/live slots/live class bytes, requested/reserved/peak page
   bytes, provider acquire/release/fail, allocations/reuses/invalid frees,
   small native fallback attempts, cumulative actual bytes returned by trim. */
volatile uint32_t pvz_runtime_small_pool_diagnostic[16];
/* Current and peak actual native spans charged to tracked app allocations;
   includes the complete span of a whole-hole allocation, not just user bytes. */
volatile uint32_t pvz_runtime_native_charge[2];
volatile uint32_t pvz_runtime_native_charge_failure;
volatile uint32_t pvz_runtime_native_charge_fault[4];
extern volatile uint32_t pvz_runtime_hole_diagnostic[12];
/* Reclaim callback count (including a terminal zero), allocation retries,
   successful allocations after a retry, released-byte hints (saturated), most
   recent user request, and final allocation failures. Hints can be image
   payload or returned pool pages; NativeCharge alone measures actual spans. */
volatile uint32_t pvz_runtime_pressure_diagnostic[6];
/* Opt-in, bounded first-failure observations BEFORE any cache is reclaimed.
   Each row: read-only native heap snapshot[20], native charge, pool reserved,
   whole-hole allocator status, ordinal. Never overwrites final-OOM evidence. */
volatile uint32_t pvz_runtime_pressure_trace_enabled;
volatile uint32_t pvz_runtime_pressure_trace_count;
volatile uint32_t pvz_runtime_pressure_trace[8][24];
extern void pvz_runtime_heap_snapshot_into(volatile uint32_t *,size_t,size_t);
static int pressure_running;
extern size_t pvzh1_cache_memory_pressure(size_t requested) __attribute__((weak));
extern size_t pvzh1_actor_pose_release_pressure(size_t requested) __attribute__((weak));
extern void *pvz_runtime_allocate_with_pressure(size_t,size_t,void *(*)(size_t,size_t,uintptr_t *),
    size_t (*)(size_t),volatile uint32_t [6],int *,uintptr_t *);
extern void *pvz_runtime_native_allocate_span(size_t,size_t,size_t *);
extern void pvz_runtime_capture_heap_failure(size_t requested,size_t native_requested);
static void first_allocation_failure(size_t requested,uintptr_t caller,uintptr_t native_result){
    if(!pvz_runtime_diagnostic[12]){
        pvz_runtime_native_charge_failure=pvz_runtime_native_charge[0];
        pvz_runtime_diagnostic[12]=(uint32_t)(requested?requested:1);
        pvz_runtime_diagnostic[13]=(uint32_t)caller;
        pvz_runtime_diagnostic[14]=(uint32_t)native_result;
    }
}
static int stat_native(const char *,struct stat *);
extern void pvzp_audio_service(void) __attribute__((weak));
static void audio_io_yield(void){if(pvzp_audio_service)pvzp_audio_service();}
#include "async_save.h"
char pvz_runtime_last_path[NATIVE_PATH_MAX];
/* Read-only observations of the native SDK calls. Raw tick units deliberately
   have no assumed frequency; calibrate against PC wall time on the device.
   open calls/ticks/failures, read calls/requested/returned/ticks/max request,
   seek calls/ticks/failures, read histogram <=1K/<=4K/<=16K/>16K, read failures.
   Counters wrap as uint32_t. No logging, allocation or I/O policy changes. */
volatile uint32_t pvz_runtime_io_diagnostic[16];
/* Write calls/requested/returned/shorts; first short fd/request/return/native
 * errno, max request and raw native write ticks. No retries or policy change. */
volatile uint32_t pvz_runtime_write_diagnostic[10];
/* 0 unprobed, 1 legacy, 2 validated 64-bucket layout; probes/maps/last bucket. */
volatile uint32_t pvz_runtime_asset_layout_diagnostic[4];
static void trace_path(const char *path){size_t n=path?strnlen(path,sizeof(pvz_runtime_last_path)-1):0;if(n)memcpy(pvz_runtime_last_path,path,n);pvz_runtime_last_path[n]=0;}
static h1_file *io_native_open(const char *path,const char *mode){
    uint32_t begin=h1_raw_tick_80hz();h1_file *file;
    pvz_runtime_io_diagnostic[0]++;
    file=h1_fopen(path,mode);
    pvz_runtime_io_diagnostic[1]+=(uint32_t)(h1_raw_tick_80hz()-begin);
    if(!file||(uintptr_t)file==0xffffffffu)pvz_runtime_io_diagnostic[2]++;
    return file;
}
static unsigned io_native_read(void *buffer,unsigned size,h1_file *file){
    uint32_t begin;unsigned n;
    pvz_runtime_io_diagnostic[3]++;pvz_runtime_io_diagnostic[4]+=size;
    if(size>pvz_runtime_io_diagnostic[7])pvz_runtime_io_diagnostic[7]=size;
    pvz_runtime_io_diagnostic[size<=1024?11:size<=4096?12:size<=16384?13:14]++;
    begin=h1_raw_tick_80hz();n=h1_fread(buffer,1,size,file);
    pvz_runtime_io_diagnostic[6]+=(uint32_t)(h1_raw_tick_80hz()-begin);
    if(n>size)pvz_runtime_io_diagnostic[15]++;else pvz_runtime_io_diagnostic[5]+=n;
    return n;
}
static void detect_resource_drive(void){
    h1_file *probe;
    if(resource_drive_detected)return;resource_drive_detected=1;
    trace_path(native_probe_a);pvz_runtime_diagnostic[8]=2;
    probe=io_native_open(native_probe_a,"rb");
    if(probe&&(uintptr_t)probe!=0xffffffffu){h1_fclose(probe);resource_prefix=native_data_a;pvz_runtime_diagnostic[7]='A';return;}
    trace_path(native_probe_b);pvz_runtime_diagnostic[8]=3;
    probe=io_native_open(native_probe_b,"rb");
    if(probe&&(uintptr_t)probe!=0xffffffffu){h1_fclose(probe);resource_prefix=native_data_b;pvz_runtime_diagnostic[7]='B';return;}
    pvz_runtime_diagnostic[7]=0;
}
static int ascii_prefix(const char *value,const char *prefix){
    while(*prefix){unsigned a=(unsigned char)*value++,b=(unsigned char)*prefix++;
        if(a>='A'&&a<='Z')a+='a'-'A';if(b>='A'&&b<='Z')b+='a'-'A';if(a!=b)return 0;
    }return 1;
}
static int asset_file_path(const char *relative){
    const char *leaf=strrchr(relative,'/');const char *dot;
    if(!(ascii_prefix(relative,"data/")||ascii_prefix(relative,"images/")||
         ascii_prefix(relative,"particles/")||ascii_prefix(relative,"reanim/")||
         ascii_prefix(relative,"sounds/")))return 0;
    leaf=leaf?leaf+1:relative;dot=strrchr(leaf,'.');
    return dot&&dot!=leaf&&dot[1];
}
static int asset_buckets_enabled(void){
    static const unsigned char expected[16]={0x48,0x31,0x42,0x4b,1,0,0,0,0x46,0x4e,0x31,0x41,64,0,0,0};
    char path[NATIVE_PATH_MAX];unsigned char marker[16],extra;h1_file *file;int valid;
    if(asset_bucket_state)return asset_bucket_state>0;
    asset_bucket_state=-1;pvz_runtime_asset_layout_diagnostic[0]=1;pvz_runtime_asset_layout_diagnostic[1]++;
    if(strlen(resource_prefix)+sizeof("\\H1DATA.MAP")>sizeof(path))return 0;
    strcpy(path,resource_prefix);strcat(path,"\\H1DATA.MAP");
    file=io_native_open(path,"rb");if(!file||(uintptr_t)file==0xffffffffu)return 0;
    valid=io_native_read(marker,sizeof(marker),file)==sizeof(marker)&&
        !memcmp(marker,expected,sizeof(marker))&&io_native_read(&extra,1,file)==0;
    h1_fclose(file);
    if(valid){asset_bucket_state=1;pvz_runtime_asset_layout_diagnostic[0]=2;}
    return valid;
}

static int native_failure(int fallback){
    uint32_t e=*(volatile uint32_t *)0x8057aefcu;
    errno=e==0x12u?ENOENT:fallback;return -1;
}
static int pvz_map_native_path(const char *path,char *output,size_t capacity,int resource_file){
    char joined[NATIVE_PATH_MAX],clean[NATIVE_PATH_MAX];size_t used=0,i=0,n;
    const char *prefix,*rest;
    trace_path(path);pvz_runtime_diagnostic[8]=1;
    if(!path||!output||capacity<4){errno=EINVAL;return -1;}
    if(path[0]&&path[1]==':'&&(path[2]=='/'||path[2]=='\\')){
        n=strlen(path);if(n>=capacity){errno=ENAMETOOLONG;return -1;}
        for(i=0;i<=n;i++)output[i]=path[i]=='/'?'\\':path[i];return 0;
    }
    if(path[0]=='/'){
        n=strlen(path);if(n>=sizeof(joined)){errno=ENAMETOOLONG;return -1;}memcpy(joined,path,n+1);
    }else{
        n=strlen(virtual_cwd);if(n+strlen(path)+2>sizeof(joined)){errno=ENAMETOOLONG;return -1;}
        memcpy(joined,virtual_cwd,n);joined[n++]='/';strcpy(joined+n,path);
    }
    clean[used++]='/';
    while(joined[i]){
        size_t start,len;while(joined[i]=='/'||joined[i]=='\\')i++;start=i;
        while(joined[i]&&joined[i]!='/'&&joined[i]!='\\')i++;len=i-start;if(!len)break;
        if(len==1&&joined[start]=='.')continue;
        if(len==2&&joined[start]=='.'&&joined[start+1]=='.'){
            if(used>1){while(used>1&&clean[used-1]!='/')used--;if(used>1)used--;}continue;
        }
        if(used>1)clean[used++]='/';memcpy(clean+used,joined+start,len);used+=len;
    }
    clean[used]=0;
    if(ascii_prefix(clean,"/PVZSAVE")&&(clean[8]=='/'||!clean[8])){detect_resource_drive();prefix=resource_prefix;rest=clean+8;}
    else if(ascii_prefix(clean,"/PVZ")&&(clean[4]=='/'||!clean[4])){
        detect_resource_drive();prefix=resource_prefix;rest=clean+4;
        if(resource_file&&rest[0]=='/'&&asset_file_path(rest+1)&&asset_buckets_enabled()){
            static const char hex[]="0123456789abcdef";uint32_t hash=2166136261u;
            const char *relative=rest+1;size_t length=strlen(relative),base=strlen(prefix);
            if(base+11u+length>=capacity){errno=ENAMETOOLONG;return -1;}
            for(i=0;i<length;i++){unsigned c=(unsigned char)relative[i];if(c>='A'&&c<='Z')c+='a'-'A';hash=(hash^c)*16777619u;}
            hash&=63u;memcpy(output,prefix,base);memcpy(output+base,"\\H1DATA\\",8);base+=8;
            output[base++]=hex[hash>>4];output[base++]=hex[hash&15];output[base++]='\\';
            for(i=0;i<length;i++){unsigned c=(unsigned char)relative[i];if(c>='A'&&c<='Z')c+='a'-'A';output[base+i]=c=='/'?'\\':(char)c;}
            output[base+length]=0;pvz_runtime_asset_layout_diagnostic[2]++;pvz_runtime_asset_layout_diagnostic[3]=hash;return 0;
        }
    }
    else if(!strcmp(clean,"/")){prefix="B:\\";rest="";}
    else {prefix="B:";rest=clean;}
    used=strlen(prefix);n=strlen(rest);if(used+n>=capacity){errno=ENAMETOOLONG;return -1;}
    memcpy(output,prefix,used);for(i=0;i<=n;i++)output[used+i]=rest[i]=='/'?'\\':rest[i];return 0;
}
int pvz_native_path(const char *path,char *output,size_t capacity){return pvz_map_native_path(path,output,capacity,1);}

/* Exceptional exits unload the BDA and erase its diagnostic RAM. Persist one
   bounded record before shutdown; the normal frame and exit paths do no I/O.
   Use native stdio directly so even an exhausted C++/newlib heap can report. */
void pvz_runtime_report_failure(int code){
    struct FailureRecord {
        uint32_t magic,version,bytes,status,tick;
        uint32_t runtime[16],termination[4],charge[2],charge_fault[4],heap[20],game[32];
        char message[256],last_path[512];
    };
    extern volatile uint32_t pvz_runtime_termination_diagnostic[4];
    extern char pvz_runtime_termination_message[256];
    extern volatile uint32_t pvz_runtime_heap_failure[20];
    extern volatile uint32_t pvzp_diagnostic[32] __attribute__((weak));
    static struct FailureRecord record;
    static int reported;
    char native[NATIVE_PATH_MAX];h1_file *file;unsigned i;
    if(!code||reported)return;
    reported=1;record.magic=0x58463148u;record.version=1;record.bytes=sizeof(record);
    record.status=(uint32_t)code;record.tick=h1_raw_tick_80hz();
    for(i=0;i<16;++i)record.runtime[i]=pvz_runtime_diagnostic[i];
    for(i=0;i<4;++i){record.termination[i]=pvz_runtime_termination_diagnostic[i];record.charge_fault[i]=pvz_runtime_native_charge_fault[i];}
    for(i=0;i<2;++i)record.charge[i]=pvz_runtime_native_charge[i];
    for(i=0;i<20;++i)record.heap[i]=pvz_runtime_heap_failure[i];
    if(pvzp_diagnostic)for(i=0;i<32;++i)record.game[i]=pvzp_diagnostic[i];
    memcpy(record.message,pvz_runtime_termination_message,sizeof(record.message));
    memcpy(record.last_path,pvz_runtime_last_path,sizeof(record.last_path));
    if(pvz_native_path("/PVZSAVE/H1FAULT.BIN",native,sizeof(native)))return;
    file=io_native_open(native,"wb");
    if(!file||(uintptr_t)file==0xffffffffu)return;
    (void)h1_fwrite(&record,1,sizeof(record),file);
    h1_fclose(file);
}
static PvzAssetIndex asset_index;
static unsigned char *asset_index_data;
static int asset_index_state,asset_index_disabled;
/* state (1 fallback, 2 indexed, 3 invalidated by resource mutation), entries,
   avoided opens, indexed stats, out-of-scope queries, bytes, error, trace count. */
volatile uint32_t pvz_runtime_asset_index_diagnostic[8];
char pvz_runtime_asset_index_paths[32][160];
static uint32_t index_word(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int index_relative(const char *native,char relative[NATIVE_PATH_MAX]){
    size_t base=strlen(resource_prefix),i=0;const char *p;
    if(strlen(native)<=base||!ascii_prefix(native,resource_prefix)||native[base]!='\\')return 0;
    p=native+base+1;
    if(asset_bucket_state>0&&ascii_prefix(p,"H1DATA\\")&&strlen(p)>10&&p[9]=='\\')p+=10;
    for(;p[i];i++){unsigned c=(unsigned char)p[i];if(i+1>=NATIVE_PATH_MAX)return 0;
        relative[i]=c=='\\'?'/':(c>='A'&&c<='Z'?(char)(c+32):(char)c);}
    relative[i]=0;
    /* Direct native paths may contain components not normalized by the VFS. */
    if(strstr(relative,"../")||strstr(relative,"/..")||strstr(relative,"//"))return 0;
    return pvz_asset_index_managed(relative);
}
static void load_asset_index(void){
    char path[NATIVE_PATH_MAX];unsigned char header[32],extra,*data=0;h1_file *file;uint32_t bytes,done=32;int ok=0;
    if(asset_index_state)return;asset_index_state=1;pvz_runtime_asset_index_diagnostic[0]=1;
    strcpy(path,resource_prefix);strcat(path,"\\resource-files.h1fi");file=io_native_open(path,"rb");
    if(!file||(uintptr_t)file==0xffffffffu){pvz_runtime_asset_index_diagnostic[6]=1;return;}
    if(io_native_read(header,32,file)!=32)goto finished;
    bytes=index_word(header+20);if(memcmp(header,"H1FI",4)||bytes>2097120||bytes<12)goto finished;
    bytes+=32;data=malloc(bytes);if(!data)goto finished;memcpy(data,header,32);
    while(done<bytes){unsigned n=bytes-done;if(n>32768)n=32768;
        if(io_native_read(data+done,n,file)!=n)goto finished;done+=n;audio_io_yield();}
    if(io_native_read(&extra,1,file)!=0||!pvz_asset_index_parse(&asset_index,data,bytes))goto finished;
    asset_index_data=data;data=0;asset_index_state=2;pvz_runtime_asset_index_diagnostic[0]=2;
    pvz_runtime_asset_index_diagnostic[1]=asset_index.count;pvz_runtime_asset_index_diagnostic[5]=bytes;ok=1;
finished:
    h1_fclose(file);free(data);if(!ok)pvz_runtime_asset_index_diagnostic[6]=2;
}
static int query_asset_index(const char *native,uint32_t *size,int *directory){
    char relative[NATIVE_PATH_MAX];
    if(asset_index_disabled||!index_relative(native,relative)){pvz_runtime_asset_index_diagnostic[4]++;return -1;}
    load_asset_index();if(asset_index_state!=2)return -1;
    return pvz_asset_index_find(&asset_index,relative,size,directory);
}
static void trace_index_miss(const char *native){
    char relative[NATIVE_PATH_MAX];unsigned count=pvz_runtime_asset_index_diagnostic[7];
    if(!index_relative(native,relative)||strlen(relative)>=160)return;
    for(unsigned i=0;i<count;i++)if(!strcmp(pvz_runtime_asset_index_paths[i],relative))return;
    if(count<32){strcpy(pvz_runtime_asset_index_paths[count],relative);pvz_runtime_asset_index_diagnostic[7]=count+1;}
}
static void invalidate_asset_index(const char *native){
    char relative[NATIVE_PATH_MAX];if(index_relative(native,relative)){
        asset_index_disabled=1;pvz_runtime_asset_index_diagnostic[0]=3;}
}
uint32_t pvz_runtime_ticks(void){return h1_raw_tick_80hz();}
void pvz_runtime_delay_ms(unsigned int milliseconds){
    /* Stock V1.41's OS timer is not initialized. Keep IRQs enabled and wait
       on the calibrated GUI raw clock. */
    extern void pvz_runtime_pump(void) __attribute__((weak));
    uint32_t start=pvz_runtime_ticks();uint32_t ticks=(uint32_t)(((uint64_t)milliseconds*PVZ_H1_RAW_TICK_HZ+999u)/1000u);
    if(pvz_runtime_pump)pvz_runtime_pump();
    while((uint32_t)(pvz_runtime_ticks()-start)<ticks)if(pvz_runtime_pump)pvz_runtime_pump();
}
static void update_small_pool_diagnostic(void){
    const PvzSmallPoolStats *s=&small_pool.stats;
    pvz_runtime_small_pool_diagnostic[0]=0x4831504cu;
    pvz_runtime_small_pool_diagnostic[1]=(uint32_t)s->pages;
    pvz_runtime_small_pool_diagnostic[2]=(uint32_t)s->peak_pages;
    pvz_runtime_small_pool_diagnostic[3]=(uint32_t)s->live_allocations;
    pvz_runtime_small_pool_diagnostic[4]=(uint32_t)s->live_class_bytes;
    pvz_runtime_small_pool_diagnostic[5]=(uint32_t)s->requested_bytes;
    pvz_runtime_small_pool_diagnostic[6]=(uint32_t)s->reserved_bytes;
    pvz_runtime_small_pool_diagnostic[7]=(uint32_t)s->peak_reserved_bytes;
    pvz_runtime_small_pool_diagnostic[8]=(uint32_t)s->acquire_calls;
    pvz_runtime_small_pool_diagnostic[9]=(uint32_t)s->release_calls;
    pvz_runtime_small_pool_diagnostic[10]=(uint32_t)s->acquire_failures;
    pvz_runtime_small_pool_diagnostic[11]=(uint32_t)s->allocations;
    pvz_runtime_small_pool_diagnostic[12]=(uint32_t)s->reuses;
    pvz_runtime_small_pool_diagnostic[13]=(uint32_t)s->invalid_frees;
    pvz_runtime_small_pool_diagnostic[14]=small_pool_fallbacks;
    pvz_runtime_small_pool_diagnostic[15]=small_pool_trimmed;
}
static void charge_native(size_t span){
    if(span>UINT32_MAX-pvz_runtime_native_charge[0]){
        pvz_runtime_native_charge_fault[0]=107;pvz_runtime_native_charge_fault[1]=(uint32_t)span;
        pvz_runtime_native_charge_fault[2]=pvz_runtime_native_charge[0];pvz_runtime_native_charge_fault[3]=(uint32_t)(uintptr_t)__builtin_return_address(0);
        pvz_runtime_abort(107);
    }
    pvz_runtime_native_charge[0]+=(uint32_t)span;
    if(pvz_runtime_native_charge[0]>pvz_runtime_native_charge[1])pvz_runtime_native_charge[1]=pvz_runtime_native_charge[0];
}
static void release_native_charge(size_t span){
    if(span>pvz_runtime_native_charge[0]){
        pvz_runtime_native_charge_fault[0]=108;pvz_runtime_native_charge_fault[1]=(uint32_t)span;
        pvz_runtime_native_charge_fault[2]=pvz_runtime_native_charge[0];pvz_runtime_native_charge_fault[3]=(uint32_t)(uintptr_t)__builtin_return_address(0);
        pvz_runtime_abort(108);
    }
    pvz_runtime_native_charge[0]-=(uint32_t)span;
}
static void *small_pool_acquire(void *context,size_t requested,size_t *reserved){
    void *raw;size_t span;(void)context;*reserved=0;
    /* A pool provider must not enter cache pressure: the pool's metadata is
       busy until this callback returns. Outer allocation owns all retries. */
    raw=pvz_runtime_native_allocate_span(requested,requested,&span);
    if(!raw||(uintptr_t)raw==UINT32_MAX)return 0;
    if(span<requested||(span&3u)){h1_free(raw);return 0;}
    *reserved=span;charge_native(span);return raw;
}
static void small_pool_release(void *context,void *raw,size_t reserved){
    (void)context;release_native_charge(reserved);h1_free(raw);
}
static void initialize_small_pool(void){
    if(!small_pool_ready){pvz_small_pool_init(&small_pool,small_pool_acquire,small_pool_release,0);small_pool_ready=1;}
}
static size_t trim_small_pool(void){
    size_t released;uint32_t before;
    if(!small_pool_ready)return 0;
    released=pvz_small_pool_trim(&small_pool);before=small_pool_trimmed;
    small_pool_trimmed=released>UINT32_MAX-before?UINT32_MAX:before+(uint32_t)released;
    update_small_pool_diagnostic();return released;
}
static size_t reclaim_one(size_t requested){
    size_t released=trim_small_pool();
    if(released)return released;
    if(pvzh1_cache_memory_pressure){
        released=pvzh1_cache_memory_pressure(requested);
        /* A source's small buffers may only have made slab pages empty.
           Return those pages before the very next real native allocation. */
        (void)trim_small_pool();
    }
    if(!released&&pvzh1_actor_pose_release_pressure)
        released=pvzh1_actor_pose_release_pressure(requested);
    return released;
}
static void *native_allocate(size_t requested,size_t size,uintptr_t *span){
    void *raw;PvzSmallPoolOwner *owner=0;*span=0;
    initialize_small_pool();
    if(size<=PVZ_SMALL_POOL_MAX_RAW){
        /* Alignment slack is already part of size. Slots need only eight-byte
           alignment; allocate_aligned still aligns the public pointer below. */
        raw=pvz_small_pool_allocate(&small_pool,size,8,&owner);
        if(raw){*span=(uintptr_t)owner|1u;update_small_pool_diagnostic();return raw;}
        ++small_pool_fallbacks;
    }
    {size_t actual=0;raw=pvz_runtime_native_allocate_span(requested,size,&actual);
    if(raw&&(uintptr_t)raw!=UINT32_MAX){*span=actual;charge_native(actual);}}
    if((!raw||(uintptr_t)raw==UINT32_MAX)&&pvz_runtime_pressure_trace_enabled){
        uint32_t ordinal=pvz_runtime_pressure_trace_count++;
        if(ordinal<8){volatile uint32_t *row=pvz_runtime_pressure_trace[ordinal];
            pvz_runtime_heap_snapshot_into(row,requested,size);
            row[20]=pvz_runtime_native_charge[0];row[21]=pvz_runtime_small_pool_diagnostic[6];
            row[22]=pvz_runtime_hole_diagnostic[3];row[23]=ordinal;
        }
    }
    update_small_pool_diagnostic();return raw;
}
static void *allocate_aligned(size_t size,size_t alignment,uintptr_t caller){
    void *raw;Allocation *p;size_t total;uintptr_t user,span;
    if(alignment<8)alignment=8;
    if(!size)size=1;if(size>0xffffffffu-sizeof(Allocation)-(alignment-1u)){if(!pvz_runtime_diagnostic[12])pvz_runtime_capture_heap_failure(size,0);first_allocation_failure(size,caller,ENOMEM);errno=ENOMEM;return 0;}
    total=size+sizeof(Allocation)+alignment-1u;
    raw=pvz_runtime_allocate_with_pressure(size,total,native_allocate,
        reclaim_one,pvz_runtime_pressure_diagnostic,&pressure_running,&span);
    if(!raw||(uintptr_t)raw==0xffffffffu){pvz_runtime_pressure_diagnostic[5]++;if(!pvz_runtime_diagnostic[12])pvz_runtime_capture_heap_failure(size,total);first_allocation_failure(size,caller,(uintptr_t)raw);errno=ENOMEM;return 0;}
    user=((uintptr_t)raw+sizeof(Allocation)+alignment-1u)&~(uintptr_t)(alignment-1u);
    p=(Allocation *)(user-sizeof(Allocation));
    p->f.magic=ALLOC_MAGIC;p->f.size=(uint32_t)size;p->f.raw=raw;p->f.prev=0;p->f.next=allocations;
    p->f.raw_span=span;
    if(allocations)allocations->f.prev=p;allocations=p;
    pvz_runtime_diagnostic[3]+=(uint32_t)size;
    if(pvz_runtime_diagnostic[3]>pvz_runtime_diagnostic[4])pvz_runtime_diagnostic[4]=pvz_runtime_diagnostic[3];
    return p+1;
}
void *malloc(size_t size){return allocate_aligned(size,8,(uintptr_t)__builtin_return_address(0));}
void free(void *memory){
    Allocation *p;if(!memory)return;p=(Allocation *)memory-1;
    if(p->f.magic!=ALLOC_MAGIC)pvz_runtime_abort(104);
    if(p->f.prev)p->f.prev->f.next=p->f.next;else allocations=p->f.next;
    if(p->f.next)p->f.next->f.prev=p->f.prev;
    pvz_runtime_diagnostic[3]-=p->f.size;
    p->f.magic=0;
    if(p->f.raw_span&1u){
        PvzSmallPoolOwner *owner=(PvzSmallPoolOwner *)(p->f.raw_span&~(uintptr_t)1u);
        if(!pvz_small_pool_free(&small_pool,p->f.raw,owner))pvz_runtime_abort(109);
        update_small_pool_diagnostic();
    }else {release_native_charge(p->f.raw_span);h1_free(p->f.raw);}
}
void *calloc(size_t count,size_t size){void *p;if(size&&count>(size_t)-1/size){errno=ENOMEM;return 0;}size*=count;p=malloc(size);if(p)memset(p,0,size);return p;}
void *realloc(void *memory,size_t size){
    void *p;Allocation *old;if(!memory)return malloc(size);if(!size){free(memory);return 0;}
    old=(Allocation *)memory-1;if(old->f.magic!=ALLOC_MAGIC)pvz_runtime_abort(105);
    p=malloc(size);if(p){memcpy(p,memory,size<old->f.size?size:old->f.size);free(memory);}return p;
}
void *_malloc_r(struct _reent *r,size_t n){(void)r;return malloc(n);}
void *_calloc_r(struct _reent *r,size_t a,size_t b){(void)r;return calloc(a,b);}
void *_realloc_r(struct _reent *r,void *p,size_t n){(void)r;return realloc(p,n);}
void _free_r(struct _reent *r,void *p){(void)r;free(p);}
void *aligned_alloc(size_t alignment,size_t size){
    /* Fundamental malloc/new alignment is 8 on this o32 ABI. Explicit callers
       retain their requested alignment, with the same adjacent tracked header. */
    if(!alignment||(alignment&(alignment-1))||alignment>32||size%alignment){errno=EINVAL;return 0;}
    return allocate_aligned(size,alignment,(uintptr_t)__builtin_return_address(0));
}
int posix_memalign(void **out,size_t alignment,size_t size){
    if(!out||alignment<sizeof(void *)||(alignment&(alignment-1))||alignment>32)return EINVAL;
    *out=allocate_aligned(size,alignment,(uintptr_t)__builtin_return_address(0));return *out?0:ENOMEM;
}
unsigned pvz_runtime_file_occupancy(void){
    unsigned count=0;for(unsigned i=0;i<FILE_COUNT;++i)if(files[i]||directory_paths[i])++count;return count;
}
static int trusted_definition_path(const char *path){
    size_t n=strlen(path);return n>=4&&!strcmp(path+n-4,".h1d");
}
int _open(const char *path,int flags,...){
    char native[NATIVE_PATH_MAX];const char *mode;h1_file *file;int i;
    pvzh1_save_wait(path);
    if(pvz_map_native_path(path,native,sizeof(native),(flags&O_DIRECTORY)==0&&(flags&O_ACCMODE)==O_RDONLY))return -1;
    for(i=0;i<FILE_COUNT&&(files[i]||directory_paths[i]);i++);if(i==FILE_COUNT){errno=EMFILE;return -1;}
    if(flags&O_DIRECTORY){struct stat st;if(stat_native(native,&st))return -1;if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return -1;}directory_paths[i]=strdup(native);if(!directory_paths[i])return -1;descriptor_flags[i]=flags;return i+3;}
    /* Offline definitions are known required resources. Open their canonical
       file directly; an auxiliary existence index must not reject a real
       definition during a later animation reload. Keep negative image-alias
       probes indexed, where avoiding nonexistent SDK opens is valuable. */
    if((flags&O_ACCMODE)==O_RDONLY&&!(flags&(O_CREAT|O_TRUNC|O_APPEND))){
        // A direct definition read bypasses the existence lookup, but it does
        // not mutate resources. Keep the authoritative negative index active.
        if(!trusted_definition_path(path)&&query_asset_index(native,0,0)==0){pvz_runtime_asset_index_diagnostic[2]++;trace_index_miss(native);errno=ENOENT;return -1;}
    }else invalidate_asset_index(native);
    if(flags&O_APPEND)mode=(flags&O_RDWR)?"ab+":"ab";
    else if(flags&O_TRUNC)mode=(flags&O_RDWR)?"wb+":"wb";
    else if((flags&O_ACCMODE)==O_RDONLY)mode="rb";
    else mode="rb+";
    if((flags&O_EXCL)&&(flags&O_CREAT)){
        file=io_native_open(native,"rb");if(file){h1_fclose(file);errno=EEXIST;return -1;}
    }
    trace_path(native);pvz_runtime_diagnostic[8]=10;
    file=io_native_open(native,mode);pvz_runtime_diagnostic[8]=11;
    if(!file&&(flags&O_CREAT)&&!(flags&O_APPEND))file=io_native_open(native,(flags&O_RDWR)?"wb+":"wb");
    if(!file||(uintptr_t)file==0xffffffffu)return native_failure(EIO);
    files[i]=file;descriptor_flags[i]=flags;pvz_runtime_diagnostic[5]++;audio_io_yield();return i+3;
}
int _close(int fd){int result;if(fd<3)return 0;if(fd>=FILE_COUNT+3){errno=EBADF;return -1;}if(directory_paths[fd-3]){free(directory_paths[fd-3]);directory_paths[fd-3]=0;return 0;}if(!files[fd-3]){errno=EBADF;return -1;}result=h1_fclose(files[fd-3]);files[fd-3]=0;return result?native_failure(EIO):0;}
ssize_t _read(int fd,void *buffer,size_t size){
    unsigned n;size_t done=0;if(fd==0)return 0;if(fd<3||fd>=FILE_COUNT+3||!files[fd-3]){errno=EBADF;return -1;}
    pvz_runtime_diagnostic[8]=20;pvz_runtime_diagnostic[9]=(uint32_t)fd;
    while(done<size){unsigned count=(unsigned)((size-done)>16384?16384:size-done);
        n=io_native_read((unsigned char*)buffer+done,count,files[fd-3]);
        if(n>count)return native_failure(EIO);done+=n;audio_io_yield();if(n<count)break;}
    pvz_runtime_diagnostic[8]=21;pvz_runtime_diagnostic[9]=(uint32_t)done;return (ssize_t)done;
}
static int io_native_seek(h1_file *file,int offset,int whence){
    uint32_t begin=h1_raw_tick_80hz();int result;
    pvz_runtime_io_diagnostic[8]++;
    result=h1_fseek(file,offset,whence);
    pvz_runtime_io_diagnostic[9]+=(uint32_t)(h1_raw_tick_80hz()-begin);
    if(result<0)pvz_runtime_io_diagnostic[10]++;
    return result;
}
/* Keep diagnostics in RAM during normal play. Flushing even tiny debug files
   to this firmware can block exit for seconds. Diagnostic builds may opt in
   to persistent logs explicitly; gameplay saves use their normal file FDs. */
volatile uint32_t pvz_runtime_log_diagnostic[3];
unsigned char pvz_runtime_log_tail[256];
void pvz_runtime_log(const void *buffer,size_t size){
    const unsigned char *data=(const unsigned char*)buffer;size_t n=size;
    uint32_t at=pvz_runtime_log_diagnostic[2];
    if(n>256){data+=n-256;n=256;at=(at+(uint32_t)(size-n))&255u;}
    for(size_t i=0;i<n;i++){pvz_runtime_log_tail[at]=data[i];at=(at+1)&255u;}
    ++pvz_runtime_log_diagnostic[0];pvz_runtime_log_diagnostic[1]+=(uint32_t)size;pvz_runtime_log_diagnostic[2]=at;
}
ssize_t _write(int fd,const void *buffer,size_t size){
    unsigned n;size_t done=0;h1_file *file;
    if(fd==1||fd==2){
#ifndef PVZ_FILE_LOGS
        pvz_runtime_log(buffer,size);return (ssize_t)size;
#else
        if(!log_file){char native[NATIVE_PATH_MAX];if(!pvz_native_path("/PVZSAVE/PORT.LOG",native,sizeof(native)))log_file=io_native_open(native,"ab");}
        if(!log_file||(uintptr_t)log_file==0xffffffffu){log_file=0;return (ssize_t)size;}file=log_file;
#endif
    }else{if(fd<3||fd>=FILE_COUNT+3||!files[fd-3]){errno=EBADF;return -1;}file=files[fd-3];}
    pvz_runtime_diagnostic[8]=22;pvz_runtime_diagnostic[9]=(uint32_t)fd;
    /* Native SD writes have substantial per-call overhead. A typical 40KB
       V4 save should be one request, as verified against the firmware API. */
    while(done<size){unsigned count=(unsigned)((size-done)>49152?49152:size-done);
        uint32_t begin=h1_raw_tick_80hz();
        n=h1_fwrite((const unsigned char*)buffer+done,1,count,file);
        pvz_runtime_write_diagnostic[0]++;pvz_runtime_write_diagnostic[1]+=count;
        if(n<=count)pvz_runtime_write_diagnostic[2]+=n;
        if(count>pvz_runtime_write_diagnostic[8])pvz_runtime_write_diagnostic[8]=count;
        pvz_runtime_write_diagnostic[9]+=h1_raw_tick_80hz()-begin;
        if(n<count){
            if(!pvz_runtime_write_diagnostic[3]){
                pvz_runtime_write_diagnostic[4]=(uint32_t)fd;pvz_runtime_write_diagnostic[5]=count;pvz_runtime_write_diagnostic[6]=n;
#ifdef __mips__
                pvz_runtime_write_diagnostic[7]=*(volatile uint32_t*)0x8057aefcu;
#else
                pvz_runtime_write_diagnostic[7]=(uint32_t)errno;
#endif
            }
            pvz_runtime_write_diagnostic[3]++;
        }
        if(n>count)return native_failure(EIO);done+=n;audio_io_yield();if(n<count)break;}
    pvz_runtime_diagnostic[8]=23;pvz_runtime_diagnostic[9]=(uint32_t)done;return (ssize_t)done;
}
off_t _lseek(int fd,off_t offset,int origin){
    int result;if(fd<3||fd>=FILE_COUNT+3||!files[fd-3]){errno=EBADF;return -1;}
    if(offset>0x7fffffffL||offset<(-0x7fffffffL-1)){errno=EOVERFLOW;return -1;}
    pvz_runtime_diagnostic[8]=40;pvz_runtime_diagnostic[9]=(uint32_t)fd;
    result=io_native_seek(files[fd-3],(int)offset,origin);pvz_runtime_diagnostic[8]=41;return result<0?native_failure(EIO):(off_t)result;
}
static int stat_native(const char *native,struct stat *out){
    typedef int (*Stat)(const char *,void *);typedef void (*Pend)(void *,unsigned,unsigned char *);typedef unsigned (*Post)(void *);
    uint32_t st[6]={0};unsigned char error=255;int result;void *sem=*(void *volatile *)0x8071d378u;
    Stat function=(Stat)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x54u);
    trace_path(native);pvz_runtime_diagnostic[8]=30;
    ((Pend)0x8000d484u)(sem,0,&error);if(error){errno=EIO;return -1;}
    pvz_runtime_diagnostic[8]=31;result=function(native,st);pvz_runtime_diagnostic[8]=32;
    ((Post)0x8000d598u)(sem);pvz_runtime_diagnostic[8]=33;
    if(result)return native_failure(ENOENT);
    memset(out,0,sizeof(*out));out->st_mode=(st[0]&0x40000000u)?(S_IFDIR|0777):(S_IFREG|0666);
    out->st_size=(off_t)st[2];out->st_nlink=1;out->st_blksize=4096;out->st_blocks=(st[2]+511u)/512u;return 0;
}
int _stat(const char *path,struct stat *out){
    char native[NATIVE_PATH_MAX];uint32_t size;int directory,found;
    // A just-queued save must exist before the original menu's FileExists
    // decides between Continue and starting a new level.
    pvzh1_save_wait(path);
    if(pvz_native_path(path,native,sizeof(native)))return -1;
    found=query_asset_index(native,&size,&directory);
    if(found>=0){pvz_runtime_asset_index_diagnostic[3]++;if(!found){trace_index_miss(native);errno=ENOENT;return -1;}
        memset(out,0,sizeof(*out));out->st_mode=directory?(S_IFDIR|0777):(S_IFREG|0666);out->st_size=size;
        out->st_nlink=1;out->st_blksize=4096;out->st_blocks=(size+511u)/512u;return 0;}
    return stat_native(native,out);
}
int lstat(const char *path,struct stat *out){return _stat(path,out);}
int _fstat(int fd,struct stat *out){
    int here,end;memset(out,0,sizeof(*out));out->st_nlink=1;
    if(fd>=0&&fd<3){out->st_mode=S_IFCHR|0666;return 0;}
    if(fd>=3&&fd<FILE_COUNT+3&&directory_paths[fd-3])return stat_native(directory_paths[fd-3],out);
    if(fd<3||fd>=FILE_COUNT+3||!files[fd-3]){errno=EBADF;return -1;}
    here=h1_ftell(files[fd-3]);end=io_native_seek(files[fd-3],0,SEEK_END);io_native_seek(files[fd-3],here,SEEK_SET);
    if(here<0||end<0)return native_failure(EIO);
    out->st_mode=S_IFREG|0666;out->st_size=end;out->st_blksize=4096;out->st_blocks=(end+511)/512;return 0;
}
int _isatty(int fd){return fd>=0&&fd<3;}
int _unlink(const char *path){char native[NATIVE_PATH_MAX];pvzh1_save_wait(path);if(pvz_native_path(path,native,sizeof(native)))return -1;invalidate_asset_index(native);return h1_remove(native)?native_failure(EIO):0;}
int rename(const char *from,const char *to){
    typedef int (*Rename)(const char *,const char *);char a[NATIVE_PATH_MAX],b[NATIVE_PATH_MAX];
    if(pvz_native_path(from,a,sizeof(a))||pvz_native_path(to,b,sizeof(b)))return -1;
    invalidate_asset_index(a);invalidate_asset_index(b);
    /* Native rename refuses overwrite. Preserve the old file on failure. */
    Rename fn=(Rename)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x28u);
    return fn(a,b)?native_failure(EIO):0;
}
int mkdir(const char *path,mode_t mode){typedef int (*Fn)(const char *);char native[NATIVE_PATH_MAX];(void)mode;if(pvz_map_native_path(path,native,sizeof(native),0))return -1;invalidate_asset_index(native);Fn fn=(Fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x30u);return fn(native)?native_failure(EIO):0;}
int rmdir(const char *path){typedef int (*Fn)(const char *);char native[NATIVE_PATH_MAX];if(pvz_map_native_path(path,native,sizeof(native),0))return -1;invalidate_asset_index(native);Fn fn=(Fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x34u);return fn(native)?native_failure(EIO):0;}
char *getcwd(char *buffer,size_t capacity){size_t n=strlen(virtual_cwd)+1;if(!buffer){buffer=malloc(capacity?capacity:n);if(!capacity)capacity=n;}if(!buffer)return 0;if(capacity<n){errno=ERANGE;return 0;}memcpy(buffer,virtual_cwd,n);return buffer;}
int chdir(const char *path){struct stat st;char joined[NATIVE_PATH_MAX];if(pvz_map_native_path(path,joined,sizeof(joined),0)||stat_native(joined,&st))return -1;if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return -1;}if(path[0]=='/')strncpy(joined,path,sizeof(joined)-1);else{size_t n=strlen(virtual_cwd);if(n+strlen(path)+2>sizeof(joined)){errno=ENAMETOOLONG;return -1;}memcpy(joined,virtual_cwd,n);joined[n++]='/';strcpy(joined+n,path);}joined[sizeof(joined)-1]=0;strcpy(virtual_cwd,joined);return 0;}
int access(const char *path,int mode){struct stat st;(void)mode;return _stat(path,&st);}
int chmod(const char *path,mode_t mode){struct stat st;(void)mode;return _stat(path,&st);}
int _getpid(void){return 1;}
int _kill(int pid,int signal){(void)pid;(void)signal;errno=ENOSYS;return -1;}
void *_sbrk(ptrdiff_t amount){(void)amount;errno=ENOMEM;return (void *)-1;}
int _gettimeofday(struct timeval *tv,void *zone){
    uint32_t t=pvz_runtime_ticks();(void)zone;if(t<last_tick)tick_high++;last_tick=t;
    if(tv){
        volatile const uint32_t *control=(volatile const uint32_t *)0xb0003000u;
        volatile const uint32_t *seconds=(volatile const uint32_t *)0xb0003004u;
        uint32_t wall=(*control&1u)?*seconds:0;uint64_t ticks=((uint64_t)tick_high<<32)|t;
        int valid=wall>=946684800u&&wall<4102444800u;
        tv->tv_sec=valid?(time_t)wall:(time_t)(1793404800ull+ticks/PVZ_H1_RAW_TICK_HZ);
        tv->tv_usec=(suseconds_t)((ticks%PVZ_H1_RAW_TICK_HZ)*1000000ull/PVZ_H1_RAW_TICK_HZ);pvz_runtime_diagnostic[6]=(uint32_t)valid;
    }return 0;
}
int clock_gettime(clockid_t id,struct timespec *ts){
    struct timeval tv;if(!ts){errno=EINVAL;return -1;}
    if(id==CLOCK_MONOTONIC){uint32_t t=pvz_runtime_ticks();if(t<last_tick)tick_high++;last_tick=t;uint64_t ticks=((uint64_t)tick_high<<32)|t;ts->tv_sec=(time_t)(ticks/PVZ_H1_RAW_TICK_HZ);ts->tv_nsec=(long)((ticks%PVZ_H1_RAW_TICK_HZ)*1000000000ull/PVZ_H1_RAW_TICK_HZ);}
    else {_gettimeofday(&tv,0);ts->tv_sec=tv.tv_sec;ts->tv_nsec=tv.tv_usec*1000;}
    return 0;
}
int nanosleep(const struct timespec *request,struct timespec *remaining){if(!request||request->tv_sec<0||request->tv_nsec<0||request->tv_nsec>=1000000000L){errno=EINVAL;return -1;}pvz_runtime_delay_ms((unsigned)(request->tv_sec*1000u+(request->tv_nsec+999999L)/1000000L));if(remaining)memset(remaining,0,sizeof(*remaining));return 0;}
unsigned sleep(unsigned seconds){pvz_runtime_delay_ms(seconds*1000u);return 0;}
int usleep(useconds_t microseconds){pvz_runtime_delay_ms((microseconds+999u)/1000u);return 0;}

struct pvz_h1_directory {uint32_t record[0x248/4];int phase,started,descriptor;long position;char path[NATIVE_PATH_MAX],pattern[NATIVE_PATH_MAX];struct dirent entry;};
static void close_find(DIR *directory){
    typedef int (*Fn)(void *);if(directory->record[0]){
        Fn fn=(Fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x44u);fn(directory->record);
    }
}
DIR *opendir(const char *path){
    struct stat st;DIR *d;size_t n;char native[NATIVE_PATH_MAX];if(pvz_map_native_path(path,native,sizeof(native),0)||stat_native(native,&st))return 0;if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return 0;}
    d=calloc(1,sizeof(*d));if(!d)return 0;d->descriptor=-1;strcpy(d->path,native);
    n=strlen(d->path);if(n+5>=sizeof(d->pattern)){free(d);errno=ENAMETOOLONG;return 0;}
    memcpy(d->pattern,d->path,n);if(n&&d->pattern[n-1]!='\\')d->pattern[n++]='\\';strcpy(d->pattern+n,"*.*");return d;
}
struct dirent *readdir(DIR *d){
    if(!d){errno=EBADF;return 0;}
    while(d->phase<2){
        int result;const char *name;size_t n;
        if(!d->started){typedef int (*Find)(const char *,unsigned,void *);Find fn=(Find)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x3cu);memset(d->record,0,sizeof(d->record));result=fn(d->pattern,d->phase?0x06u:0x16u,d->record);d->started=1;}
        else {typedef int (*Next)(void *);Next fn=(Next)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x40u);result=fn(d->record);}
        if(result){close_find(d);d->phase++;d->started=0;continue;}
        name=(const char *)d->record+0x12;n=strnlen(name,0x20a);if(!n||n>=sizeof(d->entry.d_name)){errno=EIO;return 0;}
        memcpy(d->entry.d_name,name,n+1);d->entry.d_ino=(unsigned long)++d->position;
        d->entry.d_reclen=(unsigned short)sizeof(d->entry);d->entry.d_type=d->phase?DT_REG:DT_DIR;return &d->entry;
    }
    errno=0;return 0;
}
int closedir(DIR *d){if(!d){errno=EBADF;return -1;}close_find(d);if(d->descriptor>=3)_close(d->descriptor);free(d);return 0;}
void rewinddir(DIR *d){if(!d)return;close_find(d);d->phase=d->started=0;d->position=0;}
long telldir(DIR *d){if(!d){errno=EBADF;return -1;}return d->position;}
void seekdir(DIR *d,long position){if(!d||position<0){errno=EINVAL;return;}rewinddir(d);while(d->position<position&&readdir(d));}
int dirfd(DIR *d){if(!d){errno=EBADF;return -1;}if(d->descriptor<0)d->descriptor=_open(d->path,O_RDONLY|O_DIRECTORY);return d->descriptor;}
DIR *fdopendir(int fd){DIR *d;if(fd<3||fd>=FILE_COUNT+3||!directory_paths[fd-3]){errno=ENOTDIR;return 0;}d=opendir(directory_paths[fd-3]);if(d)d->descriptor=fd;return d;}

/* This newlib MIPS configuration uses POSIX syscall names internally. Keep
   both spellings so upstream C/C++, libstdc++, and reentrant stdio agree. */
int open(const char *path,int flags,...){return _open(path,flags);}
int close(int fd){return _close(fd);}
ssize_t read(int fd,void *p,size_t n){return _read(fd,p,n);}
ssize_t write(int fd,const void *p,size_t n){return _write(fd,p,n);}
off_t lseek(int fd,off_t off,int origin){return _lseek(fd,off,origin);}
int stat(const char *path,struct stat *st){return _stat(path,st);}
int fstat(int fd,struct stat *st){return _fstat(fd,st);}
int unlink(const char *path){return _unlink(path);}
int isatty(int fd){return _isatty(fd);}
pid_t getpid(void){return (pid_t)_getpid();}
int kill(pid_t pid,int sig){return _kill((int)pid,sig);}
int gettimeofday(struct timeval *tv,void *zone){return _gettimeofday(tv,zone);}
static int at_path(int descriptor,const char *path,char result[NATIVE_PATH_MAX]){
    size_t n;if(path[0]=='/'||(path[0]&&path[1]==':')||descriptor==AT_FDCWD)return pvz_native_path(path,result,NATIVE_PATH_MAX);
    if(descriptor<3||descriptor>=FILE_COUNT+3||!directory_paths[descriptor-3]){errno=EBADF;return -1;}
    n=strlen(directory_paths[descriptor-3]);if(n+strlen(path)+2>NATIVE_PATH_MAX){errno=ENAMETOOLONG;return -1;}
    memcpy(result,directory_paths[descriptor-3],n);result[n++]='\\';strcpy(result+n,path);return 0;
}
int openat(int descriptor,const char *path,int flags,...){char native[NATIVE_PATH_MAX];if(at_path(descriptor,path,native))return -1;return _open(native,flags);}
int unlinkat(int descriptor,const char *path,int flags){char native[NATIVE_PATH_MAX];if(at_path(descriptor,path,native))return -1;return flags&AT_REMOVEDIR?rmdir(native):_unlink(native);}
long pathconf(const char *path,int name){(void)path;switch(name){case _PC_PATH_MAX:return NATIVE_PATH_MAX;case _PC_NAME_MAX:return 255;case _PC_LINK_MAX:return 1;case _PC_NO_TRUNC:case _PC_CHOWN_RESTRICTED:return 1;default:errno=EINVAL;return -1;}}
long fpathconf(int fd,int name){(void)fd;return pathconf("/PVZ",name);}
int fcntl(int fd,int command,...){if(fd<0||fd>=FILE_COUNT+3){errno=EBADF;return -1;}if(command==F_GETFD)return FD_CLOEXEC;if(command==F_SETFD)return 0;if(command==F_GETFL)return fd<3?O_RDWR:descriptor_flags[fd-3];errno=EINVAL;return -1;}
void pvz_runtime_shutdown(void){
    pvzh1_save_shutdown();
    extern void pvzp_audio_close(void) __attribute__((weak));
    extern void pvzp_window_close(void) __attribute__((weak));
    int i;
    /* Abort/OOM uses longjmp and can bypass engine destructors. Stop native
       PCM/DMA before its descriptor/ring and the BDA image can be unloaded. */
    if(pvzp_audio_close)pvzp_audio_close();
    if(pvzp_window_close)pvzp_window_close();
    for(i=0;i<FILE_COUNT;i++)if(files[i]||directory_paths[i])_close(i+3);
    if(log_file){h1_fclose(log_file);log_file=0;}
    free(asset_index_data);asset_index_data=0;memset(&asset_index,0,sizeof(asset_index));
    while(allocations)free(allocations+1);
    if(small_pool_ready){
        if(!pvz_small_pool_dispose(&small_pool))pvz_runtime_abort(106);
        small_pool_ready=0;update_small_pool_diagnostic();
    }
}
