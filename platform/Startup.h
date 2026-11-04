#pragma once
#include <stdint.h>
extern "C" void pvzh1_startup_mark(unsigned phase);
extern "C" volatile uint32_t pvzh1_startup_diagnostic[32];
extern "C" void pvzh1_adventure_mark(unsigned phase,unsigned value);
extern "C" void pvzh1_prepare_adventure();
extern "C" void pvzp_audio_loading_begin();
extern "C" void pvzp_audio_loading_end();
extern "C" int pvzp_audio_is_loading();
struct H1AudioLoadingScope {
    H1AudioLoadingScope(){pvzp_audio_loading_begin();}
    ~H1AudioLoadingScope(){pvzp_audio_loading_end();}
};
struct H1AudioSaveScope {
    bool previous;
    H1AudioSaveScope():previous(pvzp_audio_is_loading()!=0){pvzp_audio_loading_begin();}
    ~H1AudioSaveScope(){if(!previous)pvzp_audio_loading_end();}
};
struct H1StartupSelectorScope {
    H1StartupSelectorScope(){pvzh1_startup_mark(10);}
    ~H1StartupSelectorScope(){pvzh1_prepare_adventure();pvzh1_startup_mark(11);}
};
