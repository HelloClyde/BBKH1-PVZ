"""Stress production queue functions with a host scheduling-lock substitute."""
from pathlib import Path
import os,subprocess,json,hashlib
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'runtime/async_save.h').read_text(encoding='utf8')
def function(signature):
 start=s.index(signature);brace=s.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
header=s[s.index('typedef struct SaveJob'):s.index('static uint32_t save_stack')]
program=r"""
#include <windows.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define SAVE_SLOTS 4
"""+header+r"""
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_t main_thread;
static _Thread_local unsigned locked;
static volatile uint32_t pvzh1_async_save_diagnostic[16];
static unsigned save_last_yield,freed;
static atomic_uint completed,finish;
static uint32_t save_irq_lock(void){assert(!locked);assert(!pthread_mutex_lock(&mutex));locked=1;return 0;}
static void save_irq_unlock(uint32_t old){assert(locked&&old==0);locked=0;assert(!pthread_mutex_unlock(&mutex));}
static void save_barrier(void){atomic_thread_fence(memory_order_seq_cst);}
static unsigned h1_raw_tick_80hz(void){return (unsigned)GetTickCount();}
static void save_delay(void){assert(!locked);Sleep(0);}
static void audio_io_yield(void){assert(!locked);}
static void tracked_free(void* p){assert(!locked&&pthread_equal(main_thread,pthread_self()));++freed;memset(p,0xdd,sizeof(SaveJob));free(p);}
#define free tracked_free
static void save_collect(void);
"""+'\n'.join(function(n) for n in ['static SaveJob *save_next_job(void){','static void save_collect(void){','void pvzh1_save_service(void){'])+r"""
#undef free
static SaveJob* make(unsigned sequence,unsigned state){
 assert(pthread_equal(main_thread,pthread_self()));SaveJob*j=calloc(1,sizeof(*j)+1024);assert(j);j->sequence=sequence;j->state=state;j->size=1024;memset(j->data,sequence&255,1024);return j;
}
static void publish(unsigned slot,SaveJob*j){unsigned irq=save_irq_lock();assert(!save_jobs[slot]);++pvzh1_async_save_diagnostic[1];pvzh1_async_save_diagnostic[6]+=sizeof(*j)+j->size;save_jobs[slot]=j;save_irq_unlock(irq);}
static void* worker(void*unused){unsigned expected=1;(void)unused;
 for(;;){SaveJob*j=save_next_job();if(!j){if(atomic_load(&finish))break;Sleep(0);continue;}
  assert(j->state==2&&j->sequence==expected++);for(unsigned k=0;k<1024;++k)assert(j->data[k]==(j->sequence&255));
  save_barrier();j->state=3;atomic_fetch_add(&completed,1);
 }return NULL;
}
int main(void){main_thread=pthread_self();
 publish(0,make(1,0));publish(1,make(2,1));assert(!save_next_job());pvzh1_save_service();SaveJob*j=save_next_job();assert(j&&j->sequence==1&&j->state==2);
 unsigned before=freed;pvzh1_save_service();assert(freed==before&&save_jobs[0]==j);j->state=3;pvzh1_save_service();assert(!save_jobs[0]&&freed==before+1);
 j=save_next_job();assert(j&&j->sequence==2);j->state=3;pvzh1_save_service();assert(!pvzh1_async_save_diagnostic[1]&&!pvzh1_async_save_diagnostic[6]);
 pthread_t thread;assert(!pthread_create(&thread,NULL,worker,NULL));const unsigned jobs=20000;unsigned next=1;
 while(next<=jobs){pvzh1_save_service();unsigned slot=0,irq=save_irq_lock();while(slot<SAVE_SLOTS&&save_jobs[slot])++slot;save_irq_unlock(irq);
  if(slot==SAVE_SLOTS){Sleep(0);continue;}publish(slot,make(next,next%17?1:0));++next;
 }
 while(atomic_load(&completed)<jobs){pvzh1_save_service();Sleep(0);}atomic_store(&finish,1);assert(!pthread_join(thread,NULL));pvzh1_save_service();
 assert(!pvzh1_async_save_diagnostic[1]&&!pvzh1_async_save_diagnostic[6]&&freed==jobs+2);
 puts("{\"passed\":true,\"concurrent_jobs\":20000,\"fifo_and_presented_frame_gate\":true,\"claimed_jobs_not_freed\":true,\"completed_payload_reclaimed_by_frame\":true,\"allocator_only_on_foreground\":true,\"native_scheduler_tested\":false}");
}
"""
out=ROOT/'build/async-save-queue';out.mkdir(exist_ok=True);c=out/'queue.c';c.write_text(program,encoding='utf8');env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
subprocess.run(['C:/msys64/ucrt64/bin/gcc.exe','-std=c11','-O2','-pthread',str(c),'-o',str(out/'queue.exe')],check=True,env=env)
r=subprocess.run([str(out/'queue.exe')],check=True,capture_output=True,text=True,env=env,timeout=60);report=json.loads(r.stdout);report['source_sha256']=hashlib.sha256(s.encode()).hexdigest();(out/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(r.stdout)
