#include "runtime.h"
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>

volatile uint32_t pvz_runtime_version=0x00010000u;
volatile uint32_t pvz_runtime_diagnostic[16];
__attribute__((section(".h1_noinit.stack"),aligned(16),used)) unsigned char pvz_runtime_stack[131072];
static jmp_buf exit_context;
static int exit_ready,exit_status;
extern unsigned char __bss_start[],__bss_end[];
typedef void (*initializer)(void);
extern initializer __preinit_array_start[],__preinit_array_end[];
extern initializer __init_array_start[],__init_array_end[];
extern initializer __fini_array_start[],__fini_array_end[];
extern initializer __ctors_start[],__ctors_end[];
extern initializer __dtors_start[],__dtors_end[];
extern int pvz_portable_main(void);
extern unsigned char __eh_frame_start[];
extern void __register_frame(const void *);
extern void __deregister_frame(const void *);
extern void pvz_cxx_finalize(void);
extern void pvz_cxx_initialize(void);

void pvz_runtime_abort(int status){
    exit_status=status;pvz_runtime_diagnostic[1]=0xffu;
    pvz_runtime_diagnostic[2]=(uint32_t)status;
    if(exit_ready)longjmp(exit_context,1);
    for(;;){}
}
void _exit(int status){pvz_runtime_abort(status);}
int pvz_runtime_main(void){
    initializer *p;int status;
    memset(__bss_start,0,(size_t)(__bss_end-__bss_start));
    pvz_runtime_diagnostic[0]=0x50565a50u;
    pvz_runtime_diagnostic[1]=1;pvz_runtime_diagnostic[15]=pvz_runtime_version;
    exit_ready=1;
    if(!setjmp(exit_context)){
        __register_frame(__eh_frame_start);
        pvz_cxx_initialize();
        for(p=__preinit_array_start;p<__preinit_array_end;p++)(*p)();
        for(p=__init_array_start;p<__init_array_end;p++)(*p)();
        /* This GCC target uses legacy .ctors, even for C++20. GNU crtbegin
           normally walks that list backwards; our BDA has no desktop CRT. */
        for(p=__ctors_end;p>__ctors_start;){
            initializer fn=*--p;
            if(fn&&(uintptr_t)fn!=0xffffffffu){pvz_runtime_diagnostic[11]=(uint32_t)(uintptr_t)fn;fn();pvz_runtime_diagnostic[10]++;}
        }
        pvz_runtime_diagnostic[1]=2;
        exit_status=pvz_portable_main();
        pvz_cxx_finalize();
        for(p=__fini_array_end;p>__fini_array_start;)(*--p)();
        for(p=__dtors_start;p<__dtors_end;p++){initializer fn=*p;if(fn&&(uintptr_t)fn!=0xffffffffu)fn();}
    }
    status=exit_status;exit_ready=0;
    __deregister_frame(__eh_frame_start);
    pvz_runtime_shutdown();pvz_runtime_diagnostic[1]=status?0xffu:9;
    return status;
}
