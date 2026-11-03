/* Exact normal-screen glyph operations; LGPL-3.0-or-later. */
#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>
#include "graphics/Color.h"
extern "C" {
extern uint32_t pvzh1_text_capture_active;
extern volatile uint32_t pvzh1_text_replay_diagnostic[8];
extern volatile uint32_t pvzh1_font_metric_diagnostic[4];
void pvzh1_text_capture_pixel(uint32_t*,uint32_t);
std::size_t pvzh1_text_replay_release_pressure(std::size_t);
}
namespace Sexy {
class ImageFont;class Graphics;class MemoryImage;
bool H1TextReplayTryDraw(ImageFont*,Graphics*,int,int,std::string_view,const Color&);
void H1TextReplayForget(ImageFont*);
bool H1FontMetricLookup(ImageFont*,char32_t,char32_t,int&);
void H1FontMetricStore(ImageFont*,char32_t,char32_t,int);
class H1TextReplayCapture {
    ImageFont* font=nullptr;Graphics* graphics=nullptr;MemoryImage* target=nullptr;
    int x=0,y=0;std::string_view text;Color color;
    bool active=false;
    uint32_t targetBefore=0,logicalBefore=0,callsBefore=0;
public:
    H1TextReplayCapture(ImageFont*,Graphics*,int,int,std::string_view,const Color&);
    ~H1TextReplayCapture();
    void Commit();
};
#ifdef PVZ_HOST_RUNNER
void H1TextReplayTestEnable(bool);
void H1TextReplayTestFailure(int);
#endif
}
