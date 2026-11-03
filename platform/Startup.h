#pragma once
#include <stdint.h>
extern "C" void pvzh1_startup_mark(unsigned phase);
extern "C" volatile uint32_t pvzh1_startup_diagnostic[32];
struct H1StartupSelectorScope {
    H1StartupSelectorScope(){pvzh1_startup_mark(10);}
    ~H1StartupSelectorScope(){pvzh1_startup_mark(11);}
};
