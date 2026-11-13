/* Included by syscalls.c: V1.41 SDK-only save worker. The GUI thread owns
 * allocation, queue publication and collection; the worker never uses newlib,
 * the C++ allocator, renderer, audio mixer, or the descriptor table. */
#define SAVE_SLOTS 4
#include "profile_files.h"
#include <stdio.h>
#define PROFILE_NAME_BYTES 48u
typedef struct SaveJob {volatile unsigned state;unsigned size,result,ticks;
    unsigned operation,sequence,modes;
    char path[512],temporary[512],backup[512];unsigned char data[];} SaveJob;
static SaveJob *volatile save_jobs[SAVE_SLOTS];
static uint32_t save_stack[4096] __attribute__((aligned(16)));
static unsigned save_priority,save_enabled;
static volatile unsigned save_stop,save_parked;
static uint32_t save_previous_tick,save_last_yield;
static uint32_t save_sequence;
static volatile uint32_t save_wake_at[2],save_wake_tcb[2],save_wake_priority[2],save_wake_pending[2];
volatile uint32_t pvzh1_async_save_diagnostic[16];
/* submitted, completed, directory entries, matched files, removed, errors,
 * last native ticks, last profile id. */
volatile uint32_t pvzh1_delete_diagnostic[8];
static void save_barrier(void){__sync_synchronize();}
static uint32_t save_irq_lock(void){uint32_t old,masked;
    __asm__ volatile("mfc0 %0,$12\n\tnop":"=r"(old)::"memory");masked=old&~1u;
    __asm__ volatile("mtc0 %0,$12\n\tnop\n\tnop\n\tnop"::"r"(masked):"memory");return old;}
static void save_irq_unlock(uint32_t old){__asm__ volatile("mtc0 %0,$12\n\tnop\n\tnop\n\tnop"::"r"(old):"memory");}
static void save_tick(void *argument){
    ((void(*)(void*))(uintptr_t)save_previous_tick)(argument);
    for(unsigned i=0;i<2;++i)if(save_wake_pending[i]&&(int32_t)(h1_raw_tick_80hz()-save_wake_at[i])>=0){
        unsigned priority=save_wake_priority[i];uint32_t tcb=*(volatile uint32_t*)(0x80a6d6f0u+4*priority);
        if(tcb==save_wake_tcb[i]&&tcb&&*(volatile uint32_t*)(tcb+0x30u)){
            if(!((unsigned(*)(unsigned))0x8000e810u)(priority))save_wake_pending[i]=0;
        }
    }
}
static void save_delay(void){
    const uint32_t irq=save_irq_lock();const uint32_t tcb=*(volatile uint32_t*)0x8057adfcu;
    const unsigned priority=*(volatile unsigned char*)(tcb+0x35u),i=priority==save_priority?1:0;
    save_wake_tcb[i]=tcb;save_wake_priority[i]=priority;save_wake_at[i]=h1_raw_tick_80hz()+1;save_wake_pending[i]=1;
    if(!i)pvzh1_async_save_diagnostic[14]=priority;
    ((void(*)(unsigned))0x8000e8fcu)(1);save_irq_unlock(irq);
}
static int save_stat(const char *path,unsigned *size){
    typedef void(*Pend)(void*,unsigned,unsigned char*);typedef unsigned(*Post)(void*);
    typedef int(*Stat)(const char*,void*);uint32_t st[6]={0};unsigned char error=255;
    void *sem=*(void *volatile*)0x8071d378u;
    ((Pend)0x8000d484u)(sem,0,&error);if(error)return -1;
    int r=((Stat)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x54u))(path,st);
    ((Post)0x8000d598u)(sem);if(!r&&size)*size=st[2];return r;
}
static int save_rename(const char *a,const char *b){
    typedef int(*Rename)(const char*,const char*);
    return ((Rename)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x28u))(a,b);
}
static int save_identical(SaveJob *job){
    unsigned size=0;if(save_stat(job->path,&size)||size!=job->size)return 0;
    h1_file *f=h1_fopen(job->path,"rb");if(!f||(uintptr_t)f==0xffffffffu)return 0;
    unsigned char buffer[1024];unsigned at=0;int equal=1;
    while(at<size){unsigned count=size-at;if(count>sizeof(buffer))count=sizeof(buffer);
        if(h1_fread(buffer,1,count,f)!=count||memcmp(buffer,job->data+at,count)){equal=0;break;}at+=count;}
    if(h1_fclose(f))equal=0;return equal;
}
static unsigned save_commit(SaveJob *job){
    unsigned size=0;int backup=!save_stat(job->backup,0),original=!save_stat(job->path,0);
    if(backup&&!original){if(save_rename(job->backup,job->path))return 1;original=1;backup=0;}
    if(save_identical(job)){++pvzh1_async_save_diagnostic[12];return 0;}
    h1_file *f=h1_fopen(job->temporary,"wb");if(!f||(uintptr_t)f==0xffffffffu)return 2;
    unsigned written=h1_fwrite(job->data,1,job->size,f);int closed=h1_fclose(f);
    if(written!=job->size||closed){h1_remove(job->temporary);return 3;}
    if(save_stat(job->temporary,&size)||size!=job->size){h1_remove(job->temporary);return 4;}
    if(backup&&h1_remove(job->backup)){h1_remove(job->temporary);return 5;}
    if(original&&save_rename(job->path,job->backup)){h1_remove(job->temporary);return 6;}
    if(save_rename(job->temporary,job->path)){
        if(original)save_rename(job->backup,job->path);h1_remove(job->temporary);return 7;}
    if(original)h1_remove(job->backup);return 0;
}
static unsigned save_profile_delete(SaveJob *job){
    typedef int(*Find)(const char*,unsigned,void*);typedef int(*Next)(void*);typedef int(*Close)(void*);
    uint32_t record[0x248/4]={0};char pattern[512],path[512];unsigned count=0,error=0;
    const unsigned root=(unsigned)strlen(job->path),limit=job->size/PROFILE_NAME_BYTES;
    memcpy(pattern,job->path,root);memcpy(pattern+root,"\\*.*",5);
    int r=((Find)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x3cu))(pattern,0x06u,record);
    while(!r){const char *name=(const char*)record+0x12u;++pvzh1_delete_diagnostic[2];
        if(pvz_profile_file_matches(name,job->temporary,job->backup,job->modes)){
            const size_t length=strlen(name);
            if(count==limit||length>=PROFILE_NAME_BYTES){error=101;break;}
            memcpy(job->data+count*PROFILE_NAME_BYTES,name,length+1);++count;++pvzh1_delete_diagnostic[3];
        }
        r=((Next)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x40u))(record);
    }
    if(record[0]&&((Close)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_FS_TABLE_SLOT),0x44u))(record))error=102;
    if(!error)for(unsigned i=0;i<count;++i){
        const char *name=(const char*)job->data+i*PROFILE_NAME_BYTES;
        memcpy(path,job->path,root);path[root]='\\';strcpy(path+root+1,name);
        if(h1_remove(path))error=103;else ++pvzh1_delete_diagnostic[4];
    }
    ++pvzh1_delete_diagnostic[1];if(error)++pvzh1_delete_diagnostic[5];return error;
}
static SaveJob *save_next_job(void){
    SaveJob *next=0;
    for(unsigned i=0;i<SAVE_SLOTS;++i){SaveJob *j=save_jobs[i];
        if(j&&j->state<=1&&(!next||(int32_t)(j->sequence-next->sequence)<0))next=j;}
    return next&&next->state==1?next:0;
}
static void save_worker(void *unused){
    (void)unused;
    for(;;){
        int work=0;
        SaveJob *job;
        while((job=save_next_job())!=0){
            save_barrier();job->state=2;work=1;unsigned start=h1_raw_tick_80hz();
            job->result=job->operation?save_profile_delete(job):save_commit(job);job->ticks=h1_raw_tick_80hz()-start;
            if(job->operation)pvzh1_delete_diagnostic[6]=job->ticks;
            ++pvzh1_async_save_diagnostic[3];pvzh1_async_save_diagnostic[8]+=job->ticks;
            if(job->ticks>pvzh1_async_save_diagnostic[9])pvzh1_async_save_diagnostic[9]=job->ticks;
            if(job->result){++pvzh1_async_save_diagnostic[4];pvzh1_async_save_diagnostic[5]=job->result;}
            save_barrier();job->state=3;
        }
        if(save_stop&&!work){save_parked=1;save_barrier();for(;;)save_delay();}
        save_delay();
    }
}
static int save_start(void){
    if(save_priority)return 1;
    // Preserve the existing H1Debug (or stock) callback and its argument.
    // V1.41's ordinary OSTimeTick is disabled. Wake only our two sleepers,
    // using the same validated raw-tick/OSTimeDlyResume contract as H1Debug.
    uint32_t irq=save_irq_lock();save_previous_tick=*(volatile uint32_t*)0x8057b6d4u;
    if(!save_previous_tick||*(volatile uint32_t*)0x8057b6d8u){save_irq_unlock(irq);return 0;}
    *(volatile uint32_t*)0x8057b6d4u=(uint32_t)(uintptr_t)save_tick;save_irq_unlock(irq);
    typedef unsigned(*Create)(void(*)(void*),void*,uint32_t*,unsigned,unsigned,uint32_t*,uint32_t,void*,unsigned);
    for(unsigned priority=11;priority<24;++priority){
        if(*(volatile uint32_t*)(0x80a6d6f0u+4*priority))continue;
        unsigned r=((Create)0x8000db5cu)(save_worker,0,save_stack+4095,priority,0x505au,save_stack,4096,0,0);
        if(!r){save_priority=priority;pvzh1_async_save_diagnostic[10]=priority;pvzh1_async_save_diagnostic[11]=1;return 1;}
    }
    irq=save_irq_lock();*(volatile uint32_t*)0x8057b6d4u=save_previous_tick;save_previous_tick=0;save_irq_unlock(irq);return 0;
}
void pvzh1_save_service(void){
    int pending=0;
    // The changed player list has reached the LCD before deletion starts.
    // A state-0 command also holds later FIFO writes behind that boundary.
    for(unsigned i=0;i<SAVE_SLOTS;++i){SaveJob *job=save_jobs[i];
        if(job&&job->state==0){save_barrier();job->state=1;}
        if(job&&job->state<3)pending=1;}
    const uint32_t now=h1_raw_tick_80hz();
    if(pending&&now-save_last_yield>=2){save_last_yield=now;save_delay();audio_io_yield();}
}
static void save_collect(void){
    for(unsigned i=0;i<SAVE_SLOTS;++i){SaveJob *job=save_jobs[i];if(!job||job->state!=3)continue;
        save_barrier();save_jobs[i]=0;save_barrier();
        pvzh1_async_save_diagnostic[6]-=sizeof(*job)+job->size;
        --pvzh1_async_save_diagnostic[1];free(job);
    }
}
void pvzh1_save_scope(int begin){if(begin)++save_enabled;else if(save_enabled)--save_enabled;}
static int save_affects_path(const SaveJob *job,const char *path){
    if(!job->operation)return !strcmp(path,job->path);
    const size_t n=strlen(job->path);
    return strlen(path)>n&&pvz_profile_prefix(path,job->path,n)&&path[n]=='\\'&&
        pvz_profile_file_matches(path+n+1,job->temporary,job->backup,job->modes);
}
void pvzh1_save_wait(const char *path){
    if(!save_priority)return;
    char native[512];int all=!path;if(!all&&pvz_native_path(path,native,sizeof(native)))return;
    for(;;){int pending=0;
        for(unsigned i=0;i<SAVE_SLOTS;++i){SaveJob *job=save_jobs[i];
            if(job&&(all||save_affects_path(job,native))&&job->state<3){
                // An explicit same-file dependency or shutdown must not wait
                // for another frame to grant the pending command execution.
                if(job->state==0){save_barrier();job->state=1;}
                pending=1;break;}}
        if(!pending)break;save_delay();audio_io_yield();
    }
    save_collect();
}
int pvzh1_save_enqueue(const char *path,const void *data,size_t size){
    if(!save_enabled||size>512u*1024u)return 0;
    save_collect();if(!save_start())return 0;
    // FIFO sequence preserves repeated saves and deletions without waiting
    // on an earlier users.dat write inside a UI callback.
    unsigned slot=0;while(slot<SAVE_SLOTS&&save_jobs[slot])++slot;
    if(slot==SAVE_SLOTS){pvzh1_save_wait(0);slot=0;}
    SaveJob *job=malloc(sizeof(*job)+size);if(!job)return 0;memset(job,0,sizeof(*job));
    if(pvz_native_path(path,job->path,sizeof(job->path))){free(job);return 0;}
    size_t n=strlen(job->path);if(n+7>=sizeof(job->path)){free(job);return 0;}
    memcpy(job->temporary,job->path,n);memcpy(job->temporary+n,".h1tmp",7);
    memcpy(job->backup,job->path,n);memcpy(job->backup+n,".h1bak",7);
    memcpy(job->data,data,size);job->size=size;job->sequence=++save_sequence;job->state=1;
    save_barrier();save_jobs[slot]=job;save_barrier();
    ++pvzh1_async_save_diagnostic[0];++pvzh1_async_save_diagnostic[1];
    pvzh1_async_save_diagnostic[6]+=sizeof(*job)+size;
    if(pvzh1_async_save_diagnostic[6]>pvzh1_async_save_diagnostic[7])pvzh1_async_save_diagnostic[7]=pvzh1_async_save_diagnostic[6];
    return 1;
}
int pvzh1_delete_profile_enqueue(const char *root,int id,unsigned modes){
    if(id<0||!modes||modes>1000)return 0;
    save_collect();if(!save_start())return 0;
    unsigned slot=0;while(slot<SAVE_SLOTS&&save_jobs[slot])++slot;
    if(slot==SAVE_SLOTS){pvzh1_save_wait(0);slot=0;}
    const unsigned bytes=(modes*2u+1u)*3u*PROFILE_NAME_BYTES;
    SaveJob *job=malloc(sizeof(*job)+bytes);if(!job)return 0;memset(job,0,sizeof(*job));
    if(pvz_native_path(root,job->path,sizeof(job->path))||strlen(job->path)+PROFILE_NAME_BYTES+2>=sizeof(job->path)){free(job);return 0;}
    snprintf(job->temporary,sizeof(job->temporary),"user%d.dat",id);
    snprintf(job->backup,sizeof(job->backup),"game%d_",id);
    job->size=bytes;job->operation=1;job->modes=modes;job->sequence=++save_sequence;
    job->state=0; // Released by the next presented-frame callback.
    save_barrier();save_jobs[slot]=job;save_barrier();
    ++pvzh1_async_save_diagnostic[0];++pvzh1_async_save_diagnostic[1];
    pvzh1_async_save_diagnostic[6]+=sizeof(*job)+bytes;
    if(pvzh1_async_save_diagnostic[6]>pvzh1_async_save_diagnostic[7])pvzh1_async_save_diagnostic[7]=pvzh1_async_save_diagnostic[6];
    ++pvzh1_delete_diagnostic[0];pvzh1_delete_diagnostic[7]=(unsigned)id;return 1;
}
void pvzh1_save_shutdown(void){
    if(!save_priority)return;
    pvzh1_save_wait(0);save_stop=1;save_barrier();while(!save_parked)save_delay();
    unsigned r=((unsigned(*)(unsigned))0x8000dce0u)(save_priority);
    if(r){for(;;)save_delay();} // Never unload code while its task still exists.
    uint32_t irq=save_irq_lock();
    if(*(volatile uint32_t*)0x8057b6d4u!=(uint32_t)(uintptr_t)save_tick){save_irq_unlock(irq);for(;;)save_delay();}
    *(volatile uint32_t*)0x8057b6d4u=save_previous_tick;save_previous_tick=0;save_irq_unlock(irq);
    save_priority=0;pvzh1_async_save_diagnostic[11]=0;
}
