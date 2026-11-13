/* Exact reuse of libm results for repeated float angles. LGPL-3.0-or-later. */
#pragma once
#include <cmath>
#include <bit>
#include <cstdint>
extern "C" {inline volatile uint32_t pvzh1_trig_diagnostic[4]{};}
#ifdef PVZH1_FAST3_TEST
extern "C" bool pvzh1_fast3_test_enabled;
#endif
#ifdef PVZ_HOST_RUNNER
inline bool h1ReanimTrigEnabled=true;
inline void H1ReanimTrigTestEnable(bool enabled){h1ReanimTrigEnabled=enabled;}
#endif
namespace H1ReanimTrigDetail {
struct Slot {uint32_t key=0;float cosine=0,sine=0;bool valid=false;};
inline Slot slots[128];
}
inline void H1ReanimTrig(float angle,float& cosine,float& sine){
 const uint32_t key=std::bit_cast<uint32_t>(angle);
 bool enabled=(key&0x7f800000u)!=0x7f800000u;
#ifdef PVZ_HOST_RUNNER
 enabled&=h1ReanimTrigEnabled;
#endif
#ifdef PVZH1_FAST3_TEST
 enabled&=pvzh1_fast3_test_enabled;
#endif
 if(!enabled){cosine=std::cos(angle);sine=std::sin(angle);return;}
 ++pvzh1_trig_diagnostic[0];
 auto& slot=H1ReanimTrigDetail::slots[(key^(key>>7)^(key>>14))&127];
 if(slot.valid&&slot.key==key){cosine=slot.cosine;sine=slot.sine;++pvzh1_trig_diagnostic[1];return;}
 cosine=std::cos(angle);sine=std::sin(angle);
 if(slot.valid)++pvzh1_trig_diagnostic[3];
 slot={key,cosine,sine,true};++pvzh1_trig_diagnostic[2];
}
