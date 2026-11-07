#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int H1IntroUnpack(const uint8_t*,unsigned,uint16_t*);
void H1IntroReconstruct(int,const uint16_t*,const uint16_t*,uint16_t*);
void H1IntroReconstructExpand(int,const uint16_t*,const uint16_t*,uint16_t*,uint32_t*);
void H1IntroExpand(const uint16_t*,uint32_t*);
void H1IntroPresent565(const uint16_t*,volatile uint32_t*);
#ifdef __cplusplus
}
#endif
