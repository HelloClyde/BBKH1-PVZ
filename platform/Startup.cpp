#include "Startup.h"
#include "h1_host.h"
extern "C" { volatile uint32_t pvzh1_startup_diagnostic[32]; }
extern "C" void pvzh1_startup_mark(unsigned phase){
    if(phase>=15)return;
    pvzh1_startup_diagnostic[0]=1;
    uint32_t now=pvzp_ticks_ms()+1;
    if(!pvzh1_startup_diagnostic[1+phase*2])pvzh1_startup_diagnostic[1+phase*2]=now;
    pvzh1_startup_diagnostic[2+phase*2]=now;
}
