/* LGPL-3.0-or-later. BBK H1/Y100 V1.41 native presentation and input. */
#include "h1_host.h"
#include "h1_sdk.h"
#include "runtime.h"
#ifdef PVZ_NATIVE_PROFILE
#include "profiler.h"
#endif
#include "graphics/PresentationMap.h"
#include "graphics/DirtyPresentation.h"
#include "graphics/DirtyCanvas.h"
#include <stdio.h>
#include <string.h>

volatile uint32_t pvzp_diagnostic[32];
/* version, shadow bytes, full/partial frames, changed/skipped blocks,
 * allocation failures, reserved. */
extern "C" {volatile uint32_t pvzh1_dirty_present_diagnostic[8];}
static H1DirtyPresentation dirtyPresentation;
static int window_open,pen_down,pen_sent,pen_x,pen_y;
static int pointer_x=400,pointer_y=300;
static uint32_t first_tick;
static int clock_started;
static int sample_pen(int *x,int *y){
    typedef void(*Fn)(h1_u16*,h1_u16*);h1_u16 px=65535,py=65535;
    const uint32_t *fn=(const uint32_t*)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x6c0u);
    if(!fn||fn[0]!=0x27bdffa8u||fn[1]!=0xafbf0050u)return 0;
    ((Fn)fn)(&px,&py);if(px>=480||py>=272)return 0;
    /* Original 800x600 canvas, preserved at 362x272 with 59px side margins. */
#ifdef PVZH1_WIDESCREEN
    *x=(int)px*Sexy::H1LogicalWidth/480;*y=(int)py*Sexy::H1LogicalHeight/272;return 1;
#else
    *x=((int)px-59)*800/362;*y=(int)py*600/272;return 1;
#endif
}
extern "C" int pvzp_window_open(void){
    if(window_open)return 1;typedef int(*Fn)(void);
    Fn fn=(Fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x84cu);
    if(!fn||!fn())return 0;dirtyPresentation.Reset();window_open=1;pvzp_diagnostic[0]=0x50565031;pvzp_diagnostic[1]=1;return 1;
}
extern "C" void pvzp_window_close(void){
    if(!window_open)return;pvzp_audio_close();dirtyPresentation.Reset();pvzh1_dirty_present_diagnostic[1]=0;typedef void(*Fn)(void);
    Fn fn=(Fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x850u);
    if(fn)fn();window_open=0;pvzp_diagnostic[1]=9;
}
extern "C" uint32_t pvzp_ticks_ms(void){
    uint32_t t=h1_raw_tick_80hz();if(!clock_started){first_tick=t;clock_started=1;}
    uint32_t elapsed=t-first_tick;return (uint32_t)((uint64_t)elapsed*1000/PVZ_H1_RAW_TICK_HZ);
}
extern "C" void pvzp_log_line(const char *line){
    FILE *f=fopen("/PVZSAVE/pvz.log","ab");if(f){fputs(line,f);fputc('\n',f);fclose(f);}
    pvzp_diagnostic[4]++;
}
extern "C" void pvzp_pointer_set(int x,int y){pointer_x=x;pointer_y=y;}
extern "C" void pvzp_present_argb(const uint32_t *pixels,int w,int h){
    {
#ifdef PVZ_NATIVE_PROFILE
    PVZProfileScope profile(PVZ_PROFILE_PRESENT);
#endif
    typedef volatile uint32_t*(*Fn)(h1_u16*,void*);h1_u16 info[6]={0};
    Fn fn=(Fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x8f4u);
    volatile uint32_t *out=fn?fn(info,0):nullptr;
    if(!out||info[1]!=480||info[2]!=272||info[3]!=1920||info[5]!=32||!pixels||w<=0||h<=0){pvzp_diagnostic[3]=2;return;}
    static H1PresentationMap mapping;
    if(!dirtyPresentation.Present(out,pixels,w,h,pointer_x,pointer_y,mapping,h1DirtyCanvas.Presentation(pixels)))
        H1PresentMapped(out,pixels,w,h,pointer_x,pointer_y,mapping);
    pvzh1_dirty_present_diagnostic[0]=1;
    pvzh1_dirty_present_diagnostic[1]=dirtyPresentation.ResidentBytes();
    pvzh1_dirty_present_diagnostic[2]=dirtyPresentation.fullFrames;
    pvzh1_dirty_present_diagnostic[3]=dirtyPresentation.partialFrames;
    pvzh1_dirty_present_diagnostic[4]=dirtyPresentation.changedBlocks;
    pvzh1_dirty_present_diagnostic[5]=dirtyPresentation.skippedBlocks;
    pvzh1_dirty_present_diagnostic[6]=dirtyPresentation.allocationFailures;
    }
    pvzp_diagnostic[2]++;pvzp_audio_pump();
#ifdef PVZ_NATIVE_PROFILE
    pvz_profile_mark_frame();
#endif
}
extern "C" int pvzp_input_poll(int *kind,int *key,int *x,int *y){
    *kind=*key=0;*x=pen_x;*y=pen_y;
    for(int i=0;i<48;i++){
        int code=-1,value=-1;h1_event_fetch(&code,&value);
        if(code==-1&&value==-1){
            if(pen_down){int nx,ny;if(sample_pen(&nx,&ny)){
                if(!pen_sent||nx!=pen_x||ny!=pen_y){pen_x=nx;pen_y=ny;*x=nx;*y=ny;*kind=pen_sent?5:3;pen_sent=1;return 1;}
            }}return 0;
        }
        if((code==9||code==10)&&value>0&&value<=44){*kind=code==9?1:2;*key=value;pvzp_diagnostic[5]++;return 1;}
        if(code==11){pen_down=1;int nx,ny;if(sample_pen(&nx,&ny)){
            pen_x=nx;pen_y=ny;*x=nx;*y=ny;*kind=pen_sent?5:3;pen_sent=1;pvzp_diagnostic[6]++;return 1;
        }}else if(code==8){int had=pen_sent;int nx,ny;if(sample_pen(&nx,&ny)){pen_x=nx;pen_y=ny;}
            pen_down=pen_sent=0;if(had){*kind=4;*x=pen_x;*y=pen_y;pvzp_diagnostic[6]++;return 1;}
        }
    }return 0;
}
