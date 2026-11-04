#include "Startup.h"
#include "h1_host.h"
#include "PvzpLib/Reanimator.h"
extern "C" { volatile uint32_t pvzh1_startup_diagnostic[32]; }
extern "C" { volatile uint32_t pvzh1_adventure_diagnostic[12]; }
extern "C" void pvzh1_prepare_adventure(){
    ReanimatorEnsureDefinitionLoaded(ReanimationType::REANIM_ZOMBIE_HAND,true);
}
extern "C" void pvzh1_adventure_mark(unsigned phase,unsigned value){
    auto& d=pvzh1_adventure_diagnostic;uint32_t now=pvzp_ticks_ms()+1;d[0]=1;
    if(phase==0){for(unsigned i=1;i<12;i++)d[i]=0;d[1]=now;}
    else if(phase==1)d[2]=now;
    else if(phase==2)d[3]=value;
    else if(phase==3){++d[4];if(!d[5])d[5]=now;d[6]=now;}
    else if(phase==4){d[7]=now;pvzp_audio_loading_begin();}
}
extern "C" void pvzh1_startup_mark(unsigned phase){
    if(phase>=15)return;
    pvzh1_startup_diagnostic[0]=1;
    uint32_t now=pvzp_ticks_ms()+1;
    if(!pvzh1_startup_diagnostic[1+phase*2])pvzh1_startup_diagnostic[1+phase*2]=now;
    pvzh1_startup_diagnostic[2+phase*2]=now;
    if(phase==10)pvzp_audio_loading_begin();
    if(phase==8||phase==12)pvzp_audio_loading_end();
}
