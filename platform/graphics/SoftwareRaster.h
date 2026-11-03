#ifndef PVZH1_SOFTWARE_RASTER_H
#define PVZH1_SOFTWARE_RASTER_H
#include "graphics/MemoryImage.h"
#include "graphics/TriVertex.h"
#include "graphics/SWTri.h"
#include "misc/SexyMatrix.h"
#ifdef PVZ_HOST_RUNNER
extern "C" bool pvzh1_geometry_fast_enabled;
#endif
namespace Sexy {
bool H1FastReducedSpan(uint32_t *destination,int count,const SWHelper::SWTextureInfo *texture,
                      unsigned u,unsigned v,int du,int dv,bool sourceAlpha);
#ifdef PVZ_HOST_RUNNER
void H1FastReducedSpanTestEnable(bool enabled);
void H1GeometryFastTestEnable(bool enabled);
#endif
void H1DrawTriangles(MemoryImage *dest, Image *texture, const TriVertex vertices[][3], int count,
                     const Rect& clip, const Color& color, int mode, float tx=0, float ty=0, bool linear=true);
void H1DrawTransformed(MemoryImage *dest, Image *source, const Rect& sourceRect,
                       const Rect& clip, const Color& color, int mode,
                       const SexyMatrix3& matrix, bool linear, float x=0, float y=0, bool center=false);
void H1FillRect(MemoryImage *dest, const Rect& rect, const Color& color, int mode);
void H1Coverage(MemoryImage *dest, Span *spans, int count, const Color& color, int mode,
                const uint8_t *coverage, int x, int y, int width, int height);
void H1CoverageScaled(MemoryImage *dest,Span *spans,int count,const Color& color,int mode,
    const uint8_t *coverage,int x,int y,int width,int height,int logicalWidth,int logicalHeight);
void H1TracePixel(MemoryImage *dest,Image *source,const char *operation);
}
#endif
