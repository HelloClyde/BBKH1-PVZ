/* Bounded, optional replay of original SWTri scanlines. LGPL-3.0-or-later. */
#pragma once
#include <cstddef>
#include <cstdint>
#ifndef PVZH1_PLAN_STANDALONE
#include "graphics/Color.h"
#include "misc/Rect.h"
#include "misc/SexyMatrix.h"
#endif

extern "C" {
extern volatile uint32_t pvzh1_render_plan_diagnostic[24];
extern uint32_t pvzh1_render_plan_capture_active;
std::size_t pvzh1_render_plan_release_pressure(std::size_t requested);
void pvzh1_render_plan_shutdown();
}

namespace Sexy {
struct H1RenderPlanToken {uint32_t value=0;};
#ifndef PVZH1_PLAN_STANDALONE
class MemoryImage;class Image;
bool H1RenderPlanDrawOrBegin(MemoryImage*,Image*,const Rect&,const Rect&,
    const Color&,int,const SexyMatrix3&,bool,float,float,bool,H1RenderPlanToken&);
#ifdef PVZ_HOST_RUNNER
void H1RenderPlanTestEnable(bool);
#endif
#endif
void H1RenderPlanFinish(H1RenderPlanToken);
void H1RenderPlanAbort(H1RenderPlanToken);
void H1RenderPlanRecordSpan(uint32_t*,int,const uint32_t*,int,int,int,int,int,
    uint32_t,uint32_t,int,int,bool);

/* The same implementation is compiled standalone for allocator/source-eviction
 * tests. Production callers use the guarded engine wrapper above. No hook may
 * allocate from revision(), and prepare() is completed BEFORE entry lookup. */
namespace H1PlanTest {
struct View {const uint32_t* pixels=nullptr;uint32_t width=0,height=0;uint64_t revision=0;};
struct Key {const void* source=nullptr;const void* destination=nullptr;uint64_t revision=0;uint32_t words[28]{};};
struct Request {Key key{};uint32_t* destination=nullptr;uint32_t width=0,height=0;};
struct Hooks {
    void* context=nullptr;
    bool (*prepare)(void*,const void*,View*)=nullptr;
    uint64_t (*revision)(void*,const void*)=nullptr;
    void* (*allocate)(void*,std::size_t)=nullptr;
    void (*release)(void*,void*)=nullptr;
};
bool DrawOrBegin(const Request&,const Hooks&,H1RenderPlanToken&);
void Reset();
std::size_t Capacity();
bool Borrowed();
}
}
