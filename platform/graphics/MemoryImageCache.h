#ifndef PVZH1_MEMORY_IMAGE_CACHE_H
#define PVZH1_MEMORY_IMAGE_CACHE_H
#include "graphics/MemoryImage.h"
#include <cstddef>
#include <cstdint>
namespace Sexy {
/* Only immutable file-backed sources are registered. Dynamic images and images
 * modified through BitsChanged are never reclaimed by this cache. */
bool H1ImageCacheDescribe(MemoryImage *image,const std::string& path,bool autoAlpha,int alphaColor);
bool H1ImageCacheLoad(MemoryImage *image);
/* Before software blits borrow pointers, prepare exact A8 alpha indices or a
 * <=256-full-ARGB palette for immutable original RGBA font data under data/. */
bool H1ImageCachePrepareAlphaPalette(MemoryImage *image);
/* Bounded, already-clipped glyph draw for >256-color RGBA font strips. Exact
 * pixels are paged; the original blitter renders a small dynamic glyph tile. */
bool H1ImageCacheBltFontGlyph(MemoryImage *source,MemoryImage *destination,int x,int y,
    const Rect& validSource,const Color& color,int drawMode);
struct H1ImageCacheFontPageStats {
    std::size_t pages,bytes,tableBytes,maxPageBytes;
    uint64_t loads,blits,argbLoads;
};
bool H1ImageCacheGetFontPageStats(MemoryImage *image,H1ImageCacheFontPageStats *stats);
/* A synchronous raster caller pins this actual pointer table through its
 * completed drawing batch. Pixel index math remains the rasterizer's own. */
bool H1ImageCachePrepareTexturePages(MemoryImage *image,const uint32_t* const**pages);
/* Reduced files retain logical Image dimensions; this view exposes only the
 * actual physical texels and stride to the software sampler. */
bool H1ImageCacheIsReduced(MemoryImage *image);
bool H1ImageCachePrepareStorageTexture(MemoryImage *image,const uint32_t **bits,uint32_t *width,uint32_t *height);
bool H1ImageCachePrepareStorageAlpha(MemoryImage *image,const unsigned char **alpha,uint32_t *width,uint32_t *height);
/* Large reduced A8 masks use 1024-byte ordinary heap blocks. The table and
 * blocks are pinned for this synchronous draw, just like dense source views. */
bool H1ImageCachePrepareStorageAlphaPages(MemoryImage *image,const unsigned char *const **pages,uint32_t *width,uint32_t *height);
bool H1ImageCachePrepareStorageFontTiles(MemoryImage *image,const float (&u)[3],const float (&v)[3],
    const unsigned char *const **tiles,uint32_t *columns,uint32_t *width,uint32_t *height);
bool H1ImageCachePrepareStorageRGBPages(MemoryImage *image,const uint32_t *const **pages,uint32_t *width,uint32_t *height);
void H1ImageCacheAllocationChanged(MemoryImage *image);
/* Mark after a lazy native/RL/palette getter finishes: its nested GetBits may
 * publish an intermediate recount before the outer backing allocation exists. */
struct H1ImageCacheAccountingChange {
    MemoryImage* image;
    bool changed;
    H1ImageCacheAccountingChange(MemoryImage* value,bool missing):image(value),changed(missing){}
    ~H1ImageCacheAccountingChange(){if(changed)H1ImageCacheAllocationChanged(image);}
};
/* Query after a successful PrepareStorageTexture in this synchronous batch.
 * Zero means no eligible live dense pixels. The process-wide content cookie
 * changes after release/reload, sanding or Image-address reuse. It does not
 * extend the view's lifetime: never retain a pixel pointer across batches.
 * No allocation, loading, touching or eviction occurs in this query. */
uint64_t H1ImageCacheStorageRevision(MemoryImage *image) noexcept;
struct H1ImageCacheStorageStats {
    std::size_t bytes;
    uint32_t logicalWidth,logicalHeight,storageWidth,storageHeight;
    uint64_t loads,blits,argbLoads;
};
bool H1ImageCacheGetStorageStats(MemoryImage *image,H1ImageCacheStorageStats *stats);
bool H1ImageCacheBltStorageOpaqueRGB(MemoryImage *source,MemoryImage *destination,int x,int y,
    const Rect& validSource,const Rect& originalSource);
/* Called once after selecting the resource root and before loading the game.
 * Atlas policy is fixed for this process; live definitions are never switched. */
void H1ImageCacheConfigureReducedPack();
bool H1ImageCacheReducedPack();
void H1ImageCacheTouch(MemoryImage *image);
void H1ImageCacheForget(MemoryImage *image);
bool H1ImageCacheContains(MemoryImage *image);
void H1ImageCacheEndFrame();
/* Limit applies after completed frames; an active draw may exceed it. */
void H1ImageCacheSetBudget(std::size_t bytes);
using H1SandingAverage=uint32_t (*)(MemoryImage*,uint32_t*,int,int);
bool H1ImageCacheSandingStorage(MemoryImage *image,void (*sanding)(Image*),H1SandingAverage average);
bool H1ImageCacheAfterSanding(MemoryImage *image,void (*sanding)(Image*),H1SandingAverage average=nullptr);
bool H1ImageCacheSetAlpha(MemoryImage *image,const std::string& alphaPath,int rows,int columns);
/* Narrow opaque screen blit, after clipping. Large validated RGB3 files can
 * reside in 3072-byte pages instead of requiring contiguous ARGB/RL buffers. */
bool H1ImageCacheBltOpaqueRGB(MemoryImage *source,MemoryImage *destination,int x,int y,const Rect& validSource);
struct H1ImageCacheRGBStats {
    std::size_t pages,bytes,tableBytes,maxPageBytes;
    bool eligible;
    uint64_t loads,blits,argbLoads;
};
bool H1ImageCacheGetRGBStats(MemoryImage *image,H1ImageCacheRGBStats *stats);
#ifdef PVZ_HOST_RUNNER
/* -1 disables injection; zero fails the next table/page allocation or read. */
void H1ImageCacheRGBTestFailure(int allocationsBeforeFailure,int readsBeforeFailure);
void H1ImageCacheAlphaTestFailure(int allocationsBeforeFailure,int readsBeforeFailure);
void H1ImageCacheFontPaletteTestFailure(int allocationsBeforeFailure,int readsBeforeFailure);
void H1ImageCacheFontPageTestFailure(int allocationsBeforeFailure,int readsBeforeFailure);
void H1ImageCacheStorageAlphaTestEnable(bool enabled);
void H1ImageCacheAlphaPagesTestEnable(bool enabled);
void H1ImageCacheFontTilesTestEnable(bool enabled);
#endif
struct H1ImageCacheStats {
    std::size_t descriptors,residentBytes,peakBytes,budgetBytes;
    uint64_t frames,loads,evictions,loadFailures;
};
H1ImageCacheStats H1ImageCacheGetStats();
uint64_t H1ImageCacheSourceCookie(MemoryImage*) noexcept;
// Only use from the completed-frame callback, after EndFrame's exact recount.
H1ImageCacheStats H1ImageCacheCompletedFrameStats();
}
#endif
