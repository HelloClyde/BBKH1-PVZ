/* Completed-draw source-image cache for the original MemoryImage renderer.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "MemoryImageCache.h"
#include "graphics/Graphics.h"
#include "h1_resources.h"
#include <map>
#include <new>
#include <limits>
#include <cstdlib>
#include <cstring>

/* Optional render metadata cache. Its no-allocation, same-thread hook releases
 * one idle plan and returns an allocation-size hint, never a native heap span.
 * The allocator retries before asking this callback for another release. */
extern "C" size_t pvzh1_render_plan_release_pressure(size_t requested) __attribute__((weak));
extern "C" size_t pvzh1_text_replay_release_pressure(size_t requested) __attribute__((weak));

extern "C" { volatile uint32_t pvzh1_cache_pressure_diagnostic[4]; }
/* Resident, peak, loads, evictions, physical frames, payload bytes (low32). */
extern "C" { volatile uint32_t pvzh1_cache_diagnostic[6]; }
/* RGB payload resident, pages resident, cumulative pages, source loads,
 * bytes read(low32), failed loads, generic ARGB load attempts, largest page. */
extern "C" { volatile uint32_t pvzh1_paged_rgb_diagnostic[8]; }
/* Resident A8 index/table bytes, source count, loads, bytes read(low32),
 * failed loads, palette uses, generic ARGB source loads, reserved. */
extern "C" { volatile uint32_t pvzh1_alpha_palette_diagnostic[8]; }
/* RGBA font index/table resident bytes, sources, loads, bytes read(low32),
 * failed preparations, palette uses, >256-color rejections, latest colors. */
extern "C" { volatile uint32_t pvzh1_rgba_font_palette_diagnostic[8]; }
/* Paged RGBA-font resident bytes/pages, loads, bytes read(low32), failures,
 * bounded glyph blits, generic source ARGB loads, maximum data page. */
extern "C" { volatile uint32_t pvzh1_rgba_font_page_diagnostic[8]; }
/* Raster-origin RGBA resident bytes/pages, loads, bytes read(low32), failures,
 * synchronous texture views, generic source ARGB loads, maximum page. Font
 * glyph-origin storage is counted separately even though the format is shared. */
extern "C" { volatile uint32_t pvzh1_rgba_raster_page_diagnostic[8]; }
/* Reduced dense bytes, source count, loads, physical bytes read, failures,
 * sampler views, logical materializations, maximum physical allocation. */
extern "C" { volatile uint32_t pvzh1_storage_texture_diagnostic[8]; }

namespace Sexy {
namespace {
constexpr std::size_t RGBPagePixels=1024,RGBPageBytes=3072,RGBMinimumPixels=800000u;
constexpr std::size_t FontPagePixels=1024,FontPageBytes=FontPagePixels*4;
/* Independent of Source and Image addresses. Exhaustion disables new plan
 * cookies instead of wrapping into an old generation (ordinary draws remain
 * available). Single-threaded, just like the cache and its drawing batches. */
uint64_t storageRevisionClock=0;
uint64_t NextStorageRevision() noexcept {
    if(storageRevisionClock==std::numeric_limits<uint64_t>::max())return 0;
    return ++storageRevisionClock;
}
std::string FontPath(std::string path){
    for(char& c:path){if(c=='\\')c='/';else if(c>='A'&&c<='Z')c=(char)(c-'A'+'a');}
    while(path.compare(0,2,"./")==0)path.erase(0,2);
    for(std::size_t i;(i=path.find("//"))!=std::string::npos;)path.erase(i,1);
    for(std::size_t i;(i=path.find("/./"))!=std::string::npos;)path.erase(i,2);
    return path;
}
bool IsFontDataPath(const std::string& file){
    std::string path=FontPath(file),root=FontPath(h1_resources_get_root());
    while(!root.empty()&&root.back()=='/')root.pop_back();
    if(!root.empty()&&path.compare(0,root.size()+1,root+'/')==0)path.erase(0,root.size()+1);
    if(path.compare(0,5,"data/")!=0||path.size()<=5)return false;
    /* No parent traversal may turn a font qualification into another folder. */
    return path.find("/../")==std::string::npos&&path.compare(path.size()-3,3,"/..")!=0;
}
struct Source {
    uint64_t identity=NextStorageRevision();
    std::string path;
    bool autoAlpha,sanded;
    int alphaColor;
    uint64_t lastUse;
    uint64_t lastEpoch;
    std::string alphaPath;
    int alphaRows=1,alphaColumns=1;
    uint32_t width=0,height=0;
    uint32_t storageWidth=0,storageHeight=0;
    std::unique_ptr<uint32_t[]> storageBits;
    std::unique_ptr<unsigned char[]> storageAlpha;
    uint64_t storageRevision=NextStorageRevision();
    bool storageSanded=false;
    uint64_t storageLoads=0,storageBlits=0,storageARGBLoads=0;
    bool rgbEligible=false,alphaPaletteEligible=false,rgbaFontEligible=false,rgbaFontRejected=false,rgbaEligible=false;
    std::unique_ptr<unsigned char*[]> rgbPages;
    std::size_t rgbPageCount=0,rgbBytes=0;
    uint64_t rgbLoads=0,rgbBlits=0,rgbARGBLoads=0;
    std::unique_ptr<uint32_t*[]> fontPages;
    std::size_t fontPageCount=0,fontBytes=0;
    uint64_t fontLoads=0,fontBlits=0,fontARGBLoads=0;
    bool rasterStorage=false;
    H1SandingAverage sandingAverage=nullptr;
    Source(const std::string& file,bool automatic,int color,uint64_t use,uint64_t epoch,const H1ImageInfo& info)
        :path(file),autoAlpha(automatic),sanded(false),alphaColor(color),lastUse(use),lastEpoch(epoch),
         width(info.width),height(info.height),storageWidth(info.storage_width?info.storage_width:info.width),
         storageHeight(info.storage_height?info.storage_height:info.height),rgbEligible(info.channels==3&&info.flags==0),
         alphaPaletteEligible(info.channels==1&&info.flags==H1_IMAGE_MASK_ONLY),
         rgbaFontEligible(info.channels==4&&info.flags==0&&IsFontDataPath(file)),
         rgbaEligible(info.channels==4&&info.flags==0){}
    Source(Source&&)=default;
    Source(const Source&)=delete;
    ~Source(){ClearRGB();ClearFontPages();}
    bool Reduced()const{return storageWidth!=width||storageHeight!=height;}
    void ClearStorage(){storageRevision=0;storageBits.reset();storageAlpha.reset();storageSanded=false;}
    void ClearRGB(){
        if(rgbPages)for(std::size_t i=0;i<rgbPageCount;++i)delete[] rgbPages[i];
        rgbPages.reset();rgbPageCount=rgbBytes=0;
    }
    void ClearFontPages(){
        if(fontPages)for(std::size_t i=0;i<fontPageCount;++i)delete[] fontPages[i];
        fontPages.reset();fontPageCount=fontBytes=0;
    }
};
/* Zero-initialized pointer: allocation-pressure callbacks must never create the
 * cache, including while its own first allocation is in progress. */
struct Cache {
    std::map<MemoryImage*,Source> sources;
    H1ImageCacheStats stats{0,0,0,20u*1024u*1024u,0,0,0,0};
    uint64_t useClock=0;
    uint64_t frameBeginUse=0;
    uint64_t pixelEpoch=0;
    uint64_t loadedPixelBytes=0;
    uint64_t pressureCalls=0,pressureEvictions=0,pressureBytes=0;
    void (*sanding)(Image*)=nullptr;
};
Cache *cacheInstance=nullptr;
bool pressureInProgress=false;
bool reducedPackConfigured=false,reducedPack=false;
#ifdef PVZ_HOST_RUNNER
bool storageAlphaEnabled=true;
int rgbAllocFailure=-1,rgbReadFailure=-1;
int alphaAllocFailure=-1,alphaReadFailure=-1;
int fontAllocFailure=-1,fontReadFailure=-1;
int fontPageAllocFailure=-1,fontPageReadFailure=-1;
bool RGBTestAllow(int& remaining){if(remaining<0)return true;if(!remaining)return false;--remaining;return true;}
#endif
Cache& State(){if(!cacheInstance)cacheInstance=new Cache();return *cacheInstance;}
void UpdateDiagnostic(const Cache& cache)
{
    pvzh1_cache_diagnostic[0]=(uint32_t)cache.stats.residentBytes;
    pvzh1_cache_diagnostic[1]=(uint32_t)cache.stats.peakBytes;
    pvzh1_cache_diagnostic[2]=(uint32_t)cache.stats.loads;
    pvzh1_cache_diagnostic[3]=(uint32_t)cache.stats.evictions;
    pvzh1_cache_diagnostic[4]=(uint32_t)cache.stats.frames;
    pvzh1_cache_diagnostic[5]=(uint32_t)cache.loadedPixelBytes;
}
std::size_t Size(MemoryImage *image,const Source *source=nullptr)
{
    std::size_t area=(std::size_t)image->mWidth*image->mHeight;
    std::size_t size=image->mBits?area*4+4:0;
    if(image->mColorIndices)size+=area;
    if(image->mColorTable)size+=1024;
    if(image->mNativeAlphaData)size+=(image->mColorTable?256:area)*4;
    if(image->mRLAlphaData)size+=area;
    if(image->mRLAdditiveData)size+=area;
    if(source&&source->rgbPages)size+=source->rgbBytes+source->rgbPageCount*sizeof(unsigned char*);
    if(source&&source->fontPages)size+=source->fontBytes+source->fontPageCount*sizeof(uint32_t*);
    if(source&&source->storageBits)size+=(std::size_t)source->storageWidth*source->storageHeight*4+4;
    if(source&&source->storageAlpha)size+=(std::size_t)source->storageWidth*source->storageHeight;
    return size;
}
/* The size of one real backing allocation, rather than the sum of separate
 * pixel caches. This is a selection heuristic; only the allocator's retry can
 * establish whether a contiguous block is now available. */
std::size_t LargestBuffer(MemoryImage *image,const Source *source=nullptr)
{
    const std::size_t area=(std::size_t)image->mWidth*image->mHeight;
    std::size_t largest=image->mBits?area*4+4:0;
    if(image->mColorIndices)largest=std::max(largest,area);
    if(image->mColorTable)largest=std::max(largest,(std::size_t)1024);
    if(image->mNativeAlphaData)largest=std::max(largest,(image->mColorTable?256:area)*4);
    if(image->mRLAlphaData||image->mRLAdditiveData)largest=std::max(largest,area);
    if(source&&source->rgbPages){
        largest=std::max(largest,source->rgbPageCount*sizeof(unsigned char*));
        if(source->rgbBytes)largest=std::max(largest,std::min(RGBPageBytes,source->rgbBytes));
    }
    if(source&&source->fontPages){
        largest=std::max(largest,source->fontPageCount*sizeof(uint32_t*));
        if(source->fontBytes)largest=std::max(largest,std::min(FontPageBytes,source->fontBytes));
    }
    if(source&&source->storageBits)largest=std::max(largest,(std::size_t)source->storageWidth*source->storageHeight*4+4);
    if(source&&source->storageAlpha)largest=std::max(largest,(std::size_t)source->storageWidth*source->storageHeight);
    return largest;
}
std::size_t Recount()
{
    Cache& cache=State();std::size_t size=0;
    std::size_t rgbBytes=0,rgbPages=0,alphaBytes=0,alphaSources=0,fontBytes=0,fontSources=0,fontPageBytes=0,fontPages=0,rasterBytes=0,rasterPages=0,storageBytes=0,storageSources=0;
    for(auto& pair:cache.sources){
        size+=Size(pair.first,&pair.second);rgbBytes+=pair.second.rgbBytes;
        if(pair.second.rgbPages)rgbPages+=pair.second.rgbPageCount;
        if(pair.second.alphaPaletteEligible&&pair.first->mColorIndices&&pair.first->mColorTable){
            alphaBytes+=(std::size_t)pair.first->mWidth*pair.first->mHeight+1024;++alphaSources;
        }
        if(pair.second.rgbaFontEligible&&pair.first->mColorIndices&&pair.first->mColorTable){
            fontBytes+=(std::size_t)pair.first->mWidth*pair.first->mHeight+1024;++fontSources;
        }
        if(pair.second.rasterStorage){rasterBytes+=pair.second.fontBytes;if(pair.second.fontPages)rasterPages+=pair.second.fontPageCount;}
        else{fontPageBytes+=pair.second.fontBytes;if(pair.second.fontPages)fontPages+=pair.second.fontPageCount;}
        if(pair.second.storageBits){storageBytes+=(std::size_t)pair.second.storageWidth*pair.second.storageHeight*4+4;++storageSources;}
        if(pair.second.storageAlpha){storageBytes+=(std::size_t)pair.second.storageWidth*pair.second.storageHeight;++storageSources;}
    }
    pvzh1_alpha_palette_diagnostic[0]=(uint32_t)alphaBytes;pvzh1_alpha_palette_diagnostic[1]=(uint32_t)alphaSources;
    pvzh1_rgba_font_palette_diagnostic[0]=(uint32_t)fontBytes;pvzh1_rgba_font_palette_diagnostic[1]=(uint32_t)fontSources;
    pvzh1_rgba_font_page_diagnostic[0]=(uint32_t)fontPageBytes;pvzh1_rgba_font_page_diagnostic[1]=(uint32_t)fontPages;
    pvzh1_rgba_raster_page_diagnostic[0]=(uint32_t)rasterBytes;pvzh1_rgba_raster_page_diagnostic[1]=(uint32_t)rasterPages;
    pvzh1_storage_texture_diagnostic[0]=(uint32_t)storageBytes;pvzh1_storage_texture_diagnostic[1]=(uint32_t)storageSources;
    pvzh1_paged_rgb_diagnostic[0]=(uint32_t)rgbBytes;pvzh1_paged_rgb_diagnostic[1]=(uint32_t)rgbPages;
    cache.stats.residentBytes=size;
    cache.stats.peakBytes=std::max(cache.stats.peakBytes,size);
    cache.stats.descriptors=cache.sources.size();
    UpdateDiagnostic(cache);
    return size;
}
bool RGBEligible(MemoryImage *image,const Source& source){
    return source.rgbEligible&&source.alphaPath.empty()&&image->mFilePath==source.path&&
        image->mWidth==(int)source.width&&image->mHeight==(int)source.height&&
        (std::size_t)source.width*source.height>=RGBMinimumPixels&&
        !image->mHasAlpha&&!image->mHasTrans;
}
bool LoadRGB(MemoryImage *image,Source& source){
    if(source.rgbPages)return true;
    Cache& cache=State();
    H1Image input;
    if(!h1_image_open(&input,source.path.c_str(),source.autoAlpha,(uint32_t)source.alphaColor)){
        ++cache.stats.loadFailures;++pvzh1_paged_rgb_diagnostic[5];return false;
    }
    const std::size_t pixels=(std::size_t)source.width*source.height;
    bool ok=input.info.width==source.width&&input.info.height==source.height&&input.info.channels==3&&input.info.flags==0;
    const std::size_t count=(pixels+RGBPagePixels-1)/RGBPagePixels;
    if(ok){
#ifdef PVZ_HOST_RUNNER
        if(!RGBTestAllow(rgbAllocFailure))ok=false;
#endif
        if(ok){
            source.rgbPages.reset(new(std::nothrow) unsigned char*[count]());
            ok=source.rgbPages!=nullptr;if(ok)source.rgbPageCount=count;
        }
    }
    for(std::size_t i=0;ok&&i<count;++i){
        const std::size_t n=std::min(RGBPagePixels,pixels-i*RGBPagePixels),bytes=n*3;
#ifdef PVZ_HOST_RUNNER
        if(!RGBTestAllow(rgbAllocFailure)){ok=false;break;}
#endif
        source.rgbPages[i]=new(std::nothrow) unsigned char[bytes];
        if(!source.rgbPages[i]){ok=false;break;}
        source.rgbBytes+=bytes;
        ++pvzh1_paged_rgb_diagnostic[2];
        pvzh1_paged_rgb_diagnostic[7]=std::max((uint32_t)pvzh1_paged_rgb_diagnostic[7],(uint32_t)bytes);
#ifdef PVZ_HOST_RUNNER
        if(!RGBTestAllow(rgbReadFailure)){ok=false;break;}
#endif
        ok=h1_image_read_rgb_span(&input,(uint32_t)(i*RGBPagePixels),(uint32_t)n,source.rgbPages[i])!=0;
        if(ok){cache.loadedPixelBytes+=bytes;pvzh1_paged_rgb_diagnostic[4]+=(uint32_t)bytes;}
    }
    h1_image_close(&input);
    if(!ok){
        source.ClearRGB();++cache.stats.loadFailures;++pvzh1_paged_rgb_diagnostic[5];Recount();return false;
    }
    /* RGB3 has no transparent pixels, so original zero-alpha edge sanding is
     * an identity operation. No ARGB/native-alpha/RL cache is manufactured. */
    ++cache.stats.loads;++source.rgbLoads;++pvzh1_paged_rgb_diagnostic[3];Recount();return true;
}
bool LoadRGBAFontPalette(MemoryImage *image,Source& source)
{
    if(!source.rgbaFontEligible||source.rgbaFontRejected||!source.alphaPath.empty()||source.sanded||
       image->mFilePath!=source.path||image->mWidth!=(int)source.width||image->mHeight!=(int)source.height||
       image->mBits||image->mNativeAlphaData||image->mRLAlphaData||image->mRLAdditiveData||source.rgbPages||source.fontPages)return false;
    H1ImageCacheTouch(image); /* Pin before every allocation, including fopen. */
    if(image->mColorIndices||image->mColorTable){
        if(image->mColorIndices&&image->mColorTable){++pvzh1_rgba_font_palette_diagnostic[5];return true;}
        return false;
    }
    Cache& cache=State();H1Image input;
    if(!h1_image_open(&input,source.path.c_str(),source.autoAlpha,(uint32_t)source.alphaColor)){
        ++cache.stats.loadFailures;++pvzh1_rgba_font_palette_diagnostic[4];return false;
    }
    bool ok=input.info.width==source.width&&input.info.height==source.height&&
        input.info.channels==4&&input.info.flags==0,tooMany=false;
    const std::size_t area=(std::size_t)source.width*source.height;
    std::unique_ptr<uchar[]> indices;std::unique_ptr<uint32_t[]> table;
#ifdef PVZ_HOST_RUNNER
    if(ok&&!RGBTestAllow(fontAllocFailure))ok=false;
#endif
    if(ok){indices.reset(new(std::nothrow) uchar[area]);ok=indices!=nullptr;}
#ifdef PVZ_HOST_RUNNER
    if(ok&&!RGBTestAllow(fontAllocFailure))ok=false;
#endif
    if(ok){table.reset(new(std::nothrow) uint32_t[256]());ok=table!=nullptr;}
    /* Empty hash slots store zero; occupied slots store palette index+1, so
     * all 32-bit colors (including transparent RGB) and all 256 indices work. */
    uint16_t lookup[512]{};uint32_t pixels[1024];unsigned colors=0;
    bool hasTrans=false,hasAlpha=false;
    for(uint32_t y=0;ok&&y<source.height;++y)for(uint32_t x=0;ok&&x<source.width;){
        const uint32_t n=std::min(1024u,source.width-x);
#ifdef PVZ_HOST_RUNNER
        if(!RGBTestAllow(fontReadFailure)){ok=false;break;}
#endif
        if(!h1_image_read_span(&input,x,y,n,pixels)){ok=false;break;}
        cache.loadedPixelBytes+=(uint64_t)n*4;pvzh1_rgba_font_palette_diagnostic[3]+=n*4;
        for(uint32_t i=0;i<n;++i){
            const uint32_t pixel=pixels[i],hash=pixel*2654435761u;
            unsigned slot=(hash^(hash>>16))&511u;
            while(lookup[slot]&&table[lookup[slot]-1]!=pixel)slot=(slot+1)&511u;
            if(!lookup[slot]){
                if(colors==256){tooMany=true;ok=false;break;}
                table[colors]=pixel;lookup[slot]=(uint16_t)(++colors);
            }
            indices[(std::size_t)y*source.width+x+i]=(uchar)(lookup[slot]-1);
            const unsigned alpha=pixel>>24;
            if(!alpha)hasTrans=true;else if(alpha!=255)hasAlpha=true;
        }
        x+=n;
    }
    h1_image_close(&input);pvzh1_rgba_font_palette_diagnostic[7]=colors;
    if(tooMany){
        /* Immutable descriptor lifetime only: never repeat an unsupported scan.
         * This is an optimization rejection, not an image-load failure. */
        source.rgbaFontRejected=true;++pvzh1_rgba_font_palette_diagnostic[6];return false;
    }
    if(!ok){++cache.stats.loadFailures;++pvzh1_rgba_font_palette_diagnostic[4];return false;}
    image->mColorIndices=std::move(indices);image->mColorTable=std::move(table);
    if(!image->mForcedMode){image->mHasTrans=hasTrans;image->mHasAlpha=hasAlpha;}
    image->mBitsChanged=false;
    ++cache.stats.loads;++pvzh1_rgba_font_palette_diagnostic[2];++pvzh1_rgba_font_palette_diagnostic[5];
    Recount();return true;
}
bool SandRGBAPages(MemoryImage *image,Source& source)
{
    if(!source.sandingAverage)return false;
    if(!image->mHasTrans)return true;
#ifdef PVZ_HOST_RUNNER
    if(!RGBTestAllow(fontPageAllocFailure))return false;
#endif
    auto rows=std::unique_ptr<uint32_t[]>(new(std::nothrow) uint32_t[(std::size_t)source.width*3]);
    if(!rows)return false;
    for(uint32_t y=0;y<source.height;++y){
        for(int neighbor=-1;neighbor<=1;++neighbor){
            uint32_t *out=rows.get()+(std::size_t)(neighbor+1)*source.width;
            const int64_t sourceY=(int64_t)y+neighbor;
            if(sourceY<0||sourceY>=source.height){std::fill_n(out,source.width,0u);continue;}
            std::size_t offset=(std::size_t)sourceY*source.width,remaining=source.width;
            while(remaining){
                const std::size_t page=offset/FontPagePixels,within=offset%FontPagePixels;
                const std::size_t n=std::min(remaining,FontPagePixels-within);
                std::memcpy(out,source.fontPages[page]+within,n*4);out+=n;offset+=n;remaining-=n;
            }
        }
        uint32_t *middle=rows.get()+source.width;
        for(uint32_t x=0;x<source.width;++x)if(!(middle[x]&0xff000000u)){
            const std::size_t index=(std::size_t)y*source.width+x;
            /* Invoke the untouched original neighborhood implementation. Its
             * transparent-neighbor exclusion makes prior zero-alpha RGB edits
             * irrelevant, and its original border/i==0 behavior is retained. */
            source.fontPages[index/FontPagePixels][index%FontPagePixels]=
                source.sandingAverage(image,middle+x,(int)x,(int)y);
        }
    }
    return true;
}
bool SandStorage(MemoryImage *image,Source& source)
{
    if(source.storageSanded)return true;
    if(!source.sandingAverage)return false;
    /* Do not publish a recipe's old cookie while pixels are being changed. */
    source.storageRevision=0;
    if(image->mHasTrans){
        MemoryImage physical(image->mApp);physical.mWidth=(int)source.storageWidth;physical.mHeight=(int)source.storageHeight;
        for(uint32_t y=0;y<source.storageHeight;++y)for(uint32_t x=0;x<source.storageWidth;++x){
            uint32_t *pixel=source.storageBits.get()+(std::size_t)y*source.storageWidth+x;
            if(!(*pixel&0xff000000u))*pixel=source.sandingAverage(&physical,pixel,(int)x,(int)y);
        }
    }
    source.storageSanded=true;source.storageRevision=NextStorageRevision();return true;
}
bool LoadStorage(MemoryImage *image,Source& source)
{
    if(source.storageBits)return true;
    Cache& cache=State();H1Image input;
    if(!h1_image_open(&input,source.path.c_str(),source.autoAlpha,(uint32_t)source.alphaColor)){
        ++cache.stats.loadFailures;++pvzh1_storage_texture_diagnostic[4];return false;
    }
    const std::size_t area=(std::size_t)source.storageWidth*source.storageHeight;
    bool ok=input.info.width==source.width&&input.info.height==source.height&&
        input.info.storage_width==source.storageWidth&&input.info.storage_height==source.storageHeight;
    auto bits=std::unique_ptr<uint32_t[]>(ok?new(std::nothrow) uint32_t[area+1]:nullptr);
    ok=bits&&h1_image_load_storage(&input,bits.get(),area);
    const uint32_t bytes=input.info.pixel_bytes;h1_image_close(&input);
    if(!ok){++cache.stats.loadFailures;++pvzh1_storage_texture_diagnostic[4];return false;}
    bits[area]=MEMORYCHECK_ID;source.storageRevision=0;
    source.storageBits=std::move(bits);source.storageSanded=false;
    if(!image->mForcedMode){
        image->mHasAlpha=image->mHasTrans=false;
        for(std::size_t i=0;i<area;++i){const unsigned a=source.storageBits[i]>>24;if(!a)image->mHasTrans=true;else if(a<255)image->mHasAlpha=true;}
    }
    image->mBitsChanged=false;
    if(source.sanded){
        if(!SandStorage(image,source)){source.ClearStorage();++cache.stats.loadFailures;++pvzh1_storage_texture_diagnostic[4];return false;}
        if(image->mHasTrans)++image->mBitsChangedCount;
    }
    /* All pixels and the optional sanding recipe are complete before a caller
     * can obtain a content identity for this newly loaded allocation. */
    source.storageRevision=NextStorageRevision();
    ++cache.stats.loads;++source.storageLoads;++pvzh1_storage_texture_diagnostic[2];cache.loadedPixelBytes+=bytes;
    pvzh1_storage_texture_diagnostic[3]+=bytes;
    pvzh1_storage_texture_diagnostic[7]=std::max((uint32_t)pvzh1_storage_texture_diagnostic[7],(uint32_t)(area*4+4));
    Recount();return true;
}
bool LoadFontPages(MemoryImage *image,Source& source,bool raster=false)
{
    if(source.fontPages)return true;
    source.rasterStorage=raster;
    volatile uint32_t *diagnostic=raster?pvzh1_rgba_raster_page_diagnostic:pvzh1_rgba_font_page_diagnostic;
    Cache& cache=State();H1Image input;
    if(!h1_image_open(&input,source.path.c_str(),source.autoAlpha,(uint32_t)source.alphaColor)){
        ++cache.stats.loadFailures;++diagnostic[4];return false;
    }
    bool ok=input.info.width==source.width&&input.info.height==source.height&&input.info.channels==4&&input.info.flags==0;
    const std::size_t area=(std::size_t)source.width*source.height,count=(area+FontPagePixels-1)/FontPagePixels;
#ifdef PVZ_HOST_RUNNER
    if(ok&&!RGBTestAllow(fontPageAllocFailure))ok=false;
#endif
    if(ok){source.fontPages.reset(new(std::nothrow) uint32_t*[count]());ok=source.fontPages!=nullptr;if(ok)source.fontPageCount=count;}
    bool hasTrans=false,hasAlpha=false;
    for(std::size_t page=0;ok&&page<count;++page){
        const std::size_t n=std::min(FontPagePixels,area-page*FontPagePixels);
#ifdef PVZ_HOST_RUNNER
        if(!RGBTestAllow(fontPageAllocFailure)){ok=false;break;}
#endif
        source.fontPages[page]=new(std::nothrow) uint32_t[n];if(!source.fontPages[page]){ok=false;break;}
        source.fontBytes+=n*4;
        std::size_t remaining=n,offset=page*FontPagePixels,within=0;
        while(ok&&remaining){
            const uint32_t x=(uint32_t)(offset%source.width),y=(uint32_t)(offset/source.width);
            const uint32_t span=(uint32_t)std::min(remaining,(std::size_t)source.width-x);
#ifdef PVZ_HOST_RUNNER
            if(!RGBTestAllow(fontPageReadFailure)){ok=false;break;}
#endif
            if(!h1_image_read_span(&input,x,y,span,source.fontPages[page]+within)){ok=false;break;}
            cache.loadedPixelBytes+=(uint64_t)span*4;diagnostic[3]+=span*4;
            for(uint32_t i=0;i<span;++i){unsigned a=source.fontPages[page][within+i]>>24;if(!a)hasTrans=true;else if(a!=255)hasAlpha=true;}
            offset+=span;within+=span;remaining-=span;
        }
    }
    h1_image_close(&input);
    if(!ok){source.ClearFontPages();++cache.stats.loadFailures;++diagnostic[4];Recount();return false;}
    if(!image->mForcedMode){image->mHasTrans=hasTrans;image->mHasAlpha=hasAlpha;}
    image->mBitsChanged=false;
    if(source.sanded){
        if(!SandRGBAPages(image,source)){source.ClearFontPages();++cache.stats.loadFailures;++diagnostic[4];Recount();return false;}
        if(image->mHasTrans)++image->mBitsChangedCount;
    }
    ++cache.stats.loads;++source.fontLoads;++diagnostic[2];
    diagnostic[7]=std::max((uint32_t)diagnostic[7],(uint32_t)std::min(FontPageBytes,source.fontBytes));
    Recount();return true;
}
bool ApplyAlpha(MemoryImage *image,const Source& source)
{
    if(source.alphaPath.empty())return true;
    H1ImageCacheTouch(image); /* fopen and the mask row can allocate. */
    H1Image mask;
    if(!h1_image_open(&mask,source.alphaPath.c_str(),1,0xffffffu))return false;
    int width=image->mWidth/source.alphaColumns,height=image->mHeight/source.alphaRows;
    if(mask.info.width!=(uint32_t)width||mask.info.height!=(uint32_t)height){h1_image_close(&mask);return false;}
    auto row=std::unique_ptr<uint32_t[]>(new(std::nothrow) uint32_t[width]);
    bool ok=row!=nullptr;
    for(int y=0;ok&&y<image->mHeight;++y){
        ok=h1_image_read_span(&mask,0,(uint32_t)(y%height),(uint32_t)width,row.get())!=0;
        if(ok)State().loadedPixelBytes+=(uint64_t)width*mask.info.channels;
        if(ok)for(int x=0;x<image->mWidth;++x){
            uint32_t& pixel=image->mBits[(std::size_t)y*image->mWidth+x];
            pixel=(pixel&0xffffffu)|((row[x%width]&0xffu)<<24);
        }
    }
    h1_image_close(&mask);return ok;
}
}

bool H1ImageCacheDescribe(MemoryImage *image,const std::string& path,bool autoAlpha,int alphaColor)
{
    H1ImageInfo info;
    if(!image||!h1_image_probe(path.c_str(),autoAlpha,&info))return false;
    /* A file source uses metadata only until its first actual pixel access. */
    H1ImageCacheForget(image);
    image->mBits.reset();image->mColorIndices.reset();image->mColorTable.reset();
    image->mNativeAlphaData.reset();image->mRLAlphaData.reset();image->mRLAdditiveData.reset();
    image->mWidth=(int)info.width;image->mHeight=(int)info.height;
    image->mFilePath=path;image->mPurgeBits=false;image->mBitsChanged=false;
    image->mHasTrans=image->mHasAlpha=info.channels!=3;
    State().sources.emplace(image,Source(path,autoAlpha,alphaColor,++State().useClock,State().pixelEpoch,info));
    return true;
}
bool H1ImageCacheContains(MemoryImage *image)
{return State().sources.find(image)!=State().sources.end();}
void H1ImageCacheTouch(MemoryImage *image)
{
    Cache& cache=State();auto found=cache.sources.find(image);
    if(found==cache.sources.end())return;
    if(image->mFilePath!=found->second.path){cache.sources.erase(found);return;}
    found->second.lastUse=++cache.useClock;
    found->second.lastEpoch=cache.pixelEpoch;
}
void H1ImageCacheForget(MemoryImage *image)
{if(State().sources.erase(image))Recount();}
bool H1ImageCacheLoad(MemoryImage *image)
{
    Cache& cache=State();auto found=cache.sources.find(image);
    if(found==cache.sources.end())return false;
    found->second.lastUse=++cache.useClock;
    found->second.lastEpoch=cache.pixelEpoch; /* before every allocation below */
    /* Generic pixel consumers require the original contiguous ARGB format.
     * Release RGB pages before its allocation, never retain both copies. */
    if(found->second.rgbPages||found->second.fontPages){found->second.ClearRGB();found->second.ClearFontPages();Recount();}
    if(found->second.storageBits||found->second.storageAlpha){found->second.ClearStorage();Recount();}
    /* Let upstream GetBits expand its lossless palette and release the index
     * table. Reloading here would retain both ARGB and the palette buffers. */
    if(image->mBits||(image->mColorIndices&&image->mColorTable))return true;
    if(found->second.Reduced()){++pvzh1_storage_texture_diagnostic[6];++found->second.storageARGBLoads;}
    if(image->mFilePath!=found->second.path){cache.sources.erase(found);return false;}
    std::size_t area=(std::size_t)image->mWidth*image->mHeight;
    if(found->second.alphaPaletteEligible)++pvzh1_alpha_palette_diagnostic[6];
    if(found->second.rgbaFontEligible){++found->second.fontARGBLoads;++pvzh1_rgba_font_page_diagnostic[6];}
    else if(found->second.rgbaEligible&&area>=262144u){++found->second.fontARGBLoads;++pvzh1_rgba_raster_page_diagnostic[6];}
    if(found->second.rgbEligible&&found->second.alphaPath.empty()&&area>=RGBMinimumPixels){
        ++found->second.rgbARGBLoads;++pvzh1_paged_rgb_diagnostic[6];
    }
    if(area>(std::numeric_limits<std::size_t>::max()/4)-1)return false;
    /* Current drawing-batch pixels are pinned. The allocation bridge may
     * reclaim only pixels from completed top-level widgets or frames. */
    auto bits=std::unique_ptr<uint32_t[]>(new(std::nothrow) uint32_t[area+1]);
    if(!bits){++cache.stats.loadFailures;return false;}
    H1Image input;
    bool opened=h1_image_open(&input,found->second.path.c_str(),found->second.autoAlpha,
                              (uint32_t)found->second.alphaColor)!=0;
    bool loaded=opened&&input.info.width==(uint32_t)image->mWidth&&input.info.height==(uint32_t)image->mHeight&&
                h1_image_load_all(&input,bits.get(),area);
    if(loaded)cache.loadedPixelBytes+=input.info.pixel_bytes;
    if(opened)h1_image_close(&input);
    if(!loaded){
        ++cache.stats.loadFailures;
        LogErrorLn("Failed to load H1 image '{}'",found->second.path);
        std::fill_n(bits.get(),area,0u);
    }
    bits[area]=MEMORYCHECK_ID;
    image->mBits=std::move(bits);
    if(loaded&&!ApplyAlpha(image,found->second)){
        loaded=false;++cache.stats.loadFailures;
        LogErrorLn("Failed to load H1 alpha grid '{}'",found->second.alphaPath);
    }
    if(!image->mForcedMode){
        image->mHasTrans=image->mHasAlpha=false;
        for(std::size_t i=0;i<area;++i){unsigned a=image->mBits[i]>>24;if(!a)image->mHasTrans=true;else if(a<255)image->mHasAlpha=true;}
    }
    image->mBitsChanged=false;
    found->second.lastUse=++cache.useClock;
    found->second.lastEpoch=cache.pixelEpoch;
    ++cache.stats.loads;
    if(found->second.sanded&&cache.sanding)cache.sanding(image);
    Recount();
    return loaded;
}
bool H1ImageCachePrepareAlphaPalette(MemoryImage *image)
{
    if(!image||!cacheInstance)return false;
    Cache& cache=*cacheInstance;auto found=cache.sources.find(image);
    if(found==cache.sources.end())return false;
    Source& source=found->second;
    if(source.Reduced())return false;
    if(source.rgbaFontEligible)return LoadRGBAFontPalette(image,source);
    if(!source.alphaPaletteEligible||!source.alphaPath.empty()||source.sanded||
       image->mFilePath!=source.path||image->mWidth!=(int)source.width||image->mHeight!=(int)source.height)return false;
    /* Never replace an existing representation: a caller may hold its pixels.
     * Palette preparation runs before Normal/AdditiveBlt take either pointer. */
    if(image->mBits||image->mNativeAlphaData||image->mRLAlphaData||image->mRLAdditiveData||source.rgbPages)return false;
    H1ImageCacheTouch(image);
    if(image->mColorIndices||image->mColorTable){
        if(image->mColorIndices&&image->mColorTable){++pvzh1_alpha_palette_diagnostic[5];return true;}
        return false;
    }
    H1Image input;
    if(!h1_image_open(&input,source.path.c_str(),source.autoAlpha,(uint32_t)source.alphaColor)){
        ++cache.stats.loadFailures;++pvzh1_alpha_palette_diagnostic[4];return false;
    }
    bool ok=input.info.width==source.width&&input.info.height==source.height&&
        input.info.channels==1&&input.info.flags==H1_IMAGE_MASK_ONLY;
    const std::size_t area=(std::size_t)source.width*source.height;
    std::unique_ptr<uchar[]> indices;std::unique_ptr<uint32_t[]> table;
#ifdef PVZ_HOST_RUNNER
    if(ok&&!RGBTestAllow(alphaAllocFailure))ok=false;
#endif
    if(ok){indices.reset(new(std::nothrow) uchar[area]);ok=indices!=nullptr;}
#ifdef PVZ_HOST_RUNNER
    if(ok&&!RGBTestAllow(alphaAllocFailure))ok=false;
#endif
    if(ok){table.reset(new(std::nothrow) uint32_t[256]);ok=table!=nullptr;}
    uint32_t pixels[1024];bool hasTrans=false,hasAlpha=false;
    for(uint32_t y=0;ok&&y<source.height;++y)for(uint32_t x=0;ok&&x<source.width;){
        const uint32_t n=std::min(1024u,source.width-x);
#ifdef PVZ_HOST_RUNNER
        if(!RGBTestAllow(alphaReadFailure)){ok=false;break;}
#endif
        if(!h1_image_read_span(&input,x,y,n,pixels)){ok=false;break;}
        for(uint32_t i=0;i<n;++i){
            const uchar a=(uchar)(pixels[i]>>24);indices[(std::size_t)y*source.width+x+i]=a;
            if(!a)hasTrans=true;else if(a!=255)hasAlpha=true;
        }
        x+=n;
    }
    const uint32_t readBytes=input.info.pixel_bytes;h1_image_close(&input);
    if(!ok){++cache.stats.loadFailures;++pvzh1_alpha_palette_diagnostic[4];return false;}
    const uint32_t rgb=(uint32_t)source.alphaColor&0x00ffffffu;
    for(uint32_t a=0;a<256;++a)table[a]=(a<<24)|rgb;
    image->mColorIndices=std::move(indices);image->mColorTable=std::move(table);
    if(!image->mForcedMode){image->mHasTrans=hasTrans;image->mHasAlpha=hasAlpha;}
    image->mBitsChanged=false;
    ++cache.stats.loads;++pvzh1_alpha_palette_diagnostic[2];++pvzh1_alpha_palette_diagnostic[5];
    cache.loadedPixelBytes+=readBytes;pvzh1_alpha_palette_diagnostic[3]+=readBytes;
    Recount();return true;
}
bool H1ImageCacheBltFontGlyph(MemoryImage *image,MemoryImage *destination,int x,int y,
    const Rect& rect,const Color& color,int mode)
{
    if(!image||!destination||image==destination||!cacheInstance||rect.mWidth<=0||rect.mHeight<=0||
       rect.mWidth>256||rect.mHeight>256||(std::size_t)rect.mWidth*rect.mHeight>16384||
       (mode!=Graphics::DRAWMODE_NORMAL&&mode!=Graphics::DRAWMODE_ADDITIVE))return false;
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return false;
    Source& source=found->second;
    if(!source.rgbaFontEligible||!source.rgbaFontRejected||!source.alphaPath.empty()||source.sanded||
       image->mFilePath!=source.path||image->mWidth!=(int)source.width||image->mHeight!=(int)source.height||
       image->mBits||image->mColorIndices||image->mColorTable||image->mNativeAlphaData||
       image->mRLAlphaData||image->mRLAdditiveData||source.rgbPages)return false;
    if(x<0||y<0||rect.mX<0||rect.mY<0||(int64_t)x+rect.mWidth>destination->mWidth||
       (int64_t)y+rect.mHeight>destination->mHeight||(int64_t)rect.mX+rect.mWidth>image->mWidth||
       (int64_t)rect.mY+rect.mHeight>image->mHeight)return false;
    H1ImageCacheTouch(image);
    if(!LoadFontPages(image,source))return false;
    auto failedTile=[&](){source.ClearFontPages();++cacheInstance->stats.loadFailures;
        ++pvzh1_rgba_font_page_diagnostic[4];Recount();return false;};
    try{
        const std::size_t area=(std::size_t)rect.mWidth*rect.mHeight;
#ifdef PVZ_HOST_RUNNER
        if(!RGBTestAllow(fontPageAllocFailure))return failedTile();
#endif
        auto bits=std::unique_ptr<uint32_t[]>(new(std::nothrow) uint32_t[area+1]);if(!bits)return failedTile();
        for(int row=0;row<rect.mHeight;++row){
            std::size_t offset=(std::size_t)(rect.mY+row)*source.width+rect.mX;int remaining=rect.mWidth;
            uint32_t *out=bits.get()+(std::size_t)row*rect.mWidth;
            while(remaining){
                const std::size_t page=offset/FontPagePixels,within=offset%FontPagePixels;
                const std::size_t n=std::min((std::size_t)remaining,FontPagePixels-within);
                std::memcpy(out,source.fontPages[page]+within,n*4);offset+=n;out+=n;remaining-=(int)n;
            }
        }
        bits[area]=MEMORYCHECK_ID;
        MemoryImage tile(image->mApp);tile.mWidth=rect.mWidth;tile.mHeight=rect.mHeight;tile.mBits=std::move(bits);
        /* The full original image's mode controls original blend rounding.
         * Never infer different flags from an opaque/transparent glyph crop. */
        tile.SetImageMode(image->mHasTrans,image->mHasAlpha);tile.mBitsChanged=false;
        destination->MemoryImage::Blt(&tile,x,y,Rect(0,0,rect.mWidth,rect.mHeight),color,mode,false);
        image->mDrawn=true;++source.fontBlits;++pvzh1_rgba_font_page_diagnostic[5];return true;
    }catch(const std::bad_alloc&){return failedTile();}
}
bool H1ImageCachePrepareTexturePages(MemoryImage *image,const uint32_t* const**pages)
{
    if(pages)*pages=nullptr;
    if(!pages||!image||!cacheInstance)return false;
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return false;
    Source& source=found->second;
    if(source.Reduced())return false;
    if(!source.rgbaEligible||!source.alphaPath.empty()||(source.sanded&&!source.sandingAverage)||
       image->mFilePath!=source.path||image->mWidth!=(int)source.width||image->mHeight!=(int)source.height||
       ((std::size_t)source.width*source.height<262144u&&!source.fontPages)||
       image->mBits||image->mColorIndices||image->mColorTable||image->mNativeAlphaData||
       image->mRLAlphaData||image->mRLAdditiveData||source.rgbPages)return false;
    H1ImageCacheTouch(image);
    if(source.rgbaFontEligible&&!source.rgbaFontRejected&&!source.fontPages){
        H1ImageCachePrepareAlphaPalette(image);
        if(image->mColorIndices||image->mColorTable)return false;
    }
    if(!LoadFontPages(image,source,true))return false;
    *pages=source.fontPages.get();++pvzh1_rgba_raster_page_diagnostic[5];return true;
}
bool H1ImageCacheIsReduced(MemoryImage *image)
{
    if(!cacheInstance||!image)return false;
    auto found=cacheInstance->sources.find(image);
    return found!=cacheInstance->sources.end()&&found->second.Reduced();
}
void H1ImageCacheConfigureReducedPack()
{
    if(reducedPackConfigured)return;
    reducedPackConfigured=true;
    H1ImageInfo info{};
    /* This descriptor is present in both complete packs. Inspect actual
     * metadata once, rather than toggling atlas policy during later draws. */
    reducedPack=h1_image_probe("reanim/selectorscreen_bg_right",1,&info)&&
        (info.storage_width!=info.width||info.storage_height!=info.height);
}
bool H1ImageCacheReducedPack(){return reducedPackConfigured&&reducedPack;}
bool H1ImageCachePrepareStorageAlpha(MemoryImage *image,const unsigned char **alpha,uint32_t *width,uint32_t *height)
{
    if(alpha)*alpha=nullptr;
    if(!alpha||!width||!height||!cacheInstance||!image)return false;
#ifdef PVZ_HOST_RUNNER
    if(!storageAlphaEnabled)return false;
#endif
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return false;
    auto& source=found->second;
    if(!source.Reduced()||!source.alphaPaletteEligible||(uint32_t(source.alphaColor)&0xffffffu)!=0xffffffu||!source.alphaPath.empty()||
       image->mFilePath!=source.path||image->mWidth!=int(source.width)||image->mHeight!=int(source.height)||image->mBits||image->mColorIndices||image->mColorTable||source.storageBits||image->mNativeAlphaData||image->mRLAlphaData||image->mRLAdditiveData)return false;
    H1ImageCacheTouch(image);
    if(!source.storageAlpha){
        H1Image input;if(!h1_image_open(&input,source.path.c_str(),source.autoAlpha,source.alphaColor))return false;
        if(input.info.width!=source.width||input.info.height!=source.height||input.info.storage_width!=source.storageWidth||input.info.storage_height!=source.storageHeight){h1_image_close(&input);return false;}
        const size_t area=size_t(source.storageWidth)*source.storageHeight;
        auto data=std::unique_ptr<unsigned char[]>(new(std::nothrow) unsigned char[area]);
        const bool ok=data&&h1_image_load_storage_alpha(&input,data.get(),area);h1_image_close(&input);
        if(!ok){++cacheInstance->stats.loadFailures;return false;}
        source.storageAlpha=std::move(data);source.storageRevision=NextStorageRevision();
        if(!image->mForcedMode){image->mHasAlpha=image->mHasTrans=false;for(size_t i=0;i<area;++i){auto a=source.storageAlpha[i];if(!a)image->mHasTrans=true;else if(a!=255)image->mHasAlpha=true;}}
        ++cacheInstance->stats.loads;++source.storageLoads;++pvzh1_storage_texture_diagnostic[2];cacheInstance->loadedPixelBytes+=area;
        pvzh1_storage_texture_diagnostic[3]+=uint32_t(area);pvzh1_storage_texture_diagnostic[7]=std::max(uint32_t(pvzh1_storage_texture_diagnostic[7]),uint32_t(area));Recount();
    }
    *alpha=source.storageAlpha.get();*width=source.storageWidth;*height=source.storageHeight;++pvzh1_storage_texture_diagnostic[5];return true;
}
bool H1ImageCachePrepareStorageTexture(MemoryImage *image,const uint32_t **bits,uint32_t *width,uint32_t *height)
{
    if(bits)*bits=nullptr;
    if(!bits||!width||!height||!cacheInstance||!image)return false;
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return false;
    Source& source=found->second;
    if(!source.Reduced()||!source.alphaPath.empty()||image->mFilePath!=source.path||
       image->mWidth!=(int)source.width||image->mHeight!=(int)source.height||image->mBits||
       image->mColorIndices||image->mColorTable||image->mNativeAlphaData||image->mRLAlphaData||image->mRLAdditiveData)return false;
    // The command cache asks for dense storage before the triangle sampler.
    // Keep eligible white masks on the A8 path instead of expanding them here.
    if(!source.storageBits&&source.alphaPaletteEligible&&(uint32_t(source.alphaColor)&0xffffffu)==0xffffffu
#ifdef PVZ_HOST_RUNNER
       &&storageAlphaEnabled
#endif
    )return false;
    H1ImageCacheTouch(image);
    if(!LoadStorage(image,source))return false;
    *bits=source.storageBits.get();*width=source.storageWidth;*height=source.storageHeight;
    ++pvzh1_storage_texture_diagnostic[5];return true;
}
uint64_t H1ImageCacheStorageRevision(MemoryImage *image) noexcept
{
    if(!cacheInstance||!image)return 0;
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return 0;
    const Source& source=found->second;
    if(!source.storageBits||!source.Reduced()||!source.alphaPath.empty()||image->mFilePath!=source.path||
       image->mWidth!=(int)source.width||image->mHeight!=(int)source.height||image->mBits||
       image->mColorIndices||image->mColorTable||image->mNativeAlphaData||image->mRLAlphaData||image->mRLAdditiveData)return 0;
    return source.storageRevision;
}
bool H1ImageCacheGetStorageStats(MemoryImage *image,H1ImageCacheStorageStats *stats)
{
    if(stats)*stats={};
    if(!stats||!image||!cacheInstance)return false;
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return false;
    const Source& source=found->second;if(!source.Reduced())return false;
    *stats={(source.storageBits?(std::size_t)source.storageWidth*source.storageHeight*4+4:0)+(source.storageAlpha?(std::size_t)source.storageWidth*source.storageHeight:0),
        source.width,source.height,source.storageWidth,source.storageHeight,
        source.storageLoads,source.storageBlits,source.storageARGBLoads};
    return true;
}
bool H1ImageCacheBltStorageOpaqueRGB(MemoryImage *image,MemoryImage *destination,int x,int y,
    const Rect& valid,const Rect& original)
{
    if(!image||!destination||image==destination||!cacheInstance||destination->mHasAlpha||destination->mHasTrans)return false;
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return false;
    Source& source=found->second;
    if(!source.Reduced()||!source.rgbEligible||!source.alphaPath.empty()||image->mHasAlpha||image->mHasTrans||
       original.mWidth<=0||original.mHeight<=0||valid.mWidth<=0||valid.mHeight<=0||
       x<0||y<0||valid.mX<0||valid.mY<0||original.mX<0||original.mY<0||
       (int64_t)x+valid.mWidth>destination->mWidth||(int64_t)y+valid.mHeight>destination->mHeight||
       (int64_t)original.mX+original.mWidth>source.width||(int64_t)original.mY+original.mHeight>source.height||
       valid.mX<original.mX||valid.mY<original.mY||
       (int64_t)valid.mX+valid.mWidth>(int64_t)original.mX+original.mWidth||
       (int64_t)valid.mY+valid.mHeight>(int64_t)original.mY+original.mHeight)return false;
    const uint32_t *pixels=nullptr;uint32_t width=0,height=0;
    if(!H1ImageCachePrepareStorageTexture(image,&pixels,&width,&height))return false;
    const uint32_t left=(uint32_t)((uint64_t)original.mX*width/source.width);
    const uint32_t top=(uint32_t)((uint64_t)original.mY*height/source.height);
    const uint32_t right=(uint32_t)((uint64_t)(original.mX+original.mWidth)*width/source.width);
    const uint32_t bottom=(uint32_t)((uint64_t)(original.mY+original.mHeight)*height/source.height);
    if(right<=left||bottom<=top)return false;
    uint32_t *out=destination->GetBits();if(!out)return false;
    /* Nearest sample at the logical destination pixel center. Quotient and
     * remainder advance per row/pixel: no per-pixel division or modulo. */
    const uint64_t xDen=(uint64_t)original.mWidth*2,yDen=(uint64_t)original.mHeight*2;
    const uint64_t xStep=(uint64_t)(right-left)*2,yStep=(uint64_t)(bottom-top)*2;
    const uint64_t xStart=((uint64_t)(valid.mX-original.mX)*2+1)*(right-left);
    const uint64_t yStart=((uint64_t)(valid.mY-original.mY)*2+1)*(bottom-top);
    uint64_t sampleY=yStart/yDen,errorY=yStart%yDen;
    const uint64_t xWhole=xStep/xDen,xRemainder=xStep%xDen;
    const uint64_t yWhole=yStep/yDen,yRemainder=yStep%yDen;
    for(int row=0;row<valid.mHeight;++row){
        const uint32_t *input=pixels+(std::size_t)(top+sampleY)*width+left;
        uint32_t *target=out+(std::size_t)(y+row)*destination->mWidth+x;
        uint64_t sampleX=xStart/xDen,errorX=xStart%xDen;
        for(int col=0;col<valid.mWidth;++col){
            *target++=input[sampleX];
            sampleX+=xWhole;errorX+=xRemainder;if(errorX>=xDen){++sampleX;errorX-=xDen;}
        }
        sampleY+=yWhole;errorY+=yRemainder;if(errorY>=yDen){++sampleY;errorY-=yDen;}
    }
    ++source.storageBlits;image->mDrawn=true;destination->BitsChanged();return true;
}
bool H1ImageCacheSandingStorage(MemoryImage *image,void (*sanding)(Image*),H1SandingAverage average)
{
    if(!average||!cacheInstance||!image)return false;
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return false;
    Source& source=found->second;
    if(!source.Reduced()||!source.alphaPath.empty()||image->mBits||image->mColorIndices||image->mColorTable||
       image->mNativeAlphaData||image->mRLAlphaData||image->mRLAdditiveData)return false;
    H1ImageCacheTouch(image);source.sandingAverage=average;cacheInstance->sanding=sanding;
    // White A8 masks have no visible RGB edge to modify. Generic GetBits still
    // applies the saved recipe; physical nearest sampling needs only alpha.
    if(source.alphaPaletteEligible&&(uint32_t(source.alphaColor)&0xffffffu)==0xffffffu){source.sanded=true;if(image->mHasTrans)++image->mBitsChangedCount;return true;}
    if(!LoadStorage(image,source)||!SandStorage(image,source))return false;
    source.sanded=true;++image->mBitsChangedCount;return true;
}
bool H1ImageCacheSetAlpha(MemoryImage *image,const std::string& path,int rows,int columns)
{
    H1ImageCacheTouch(image);
    Cache& cache=State();auto found=cache.sources.find(image);H1ImageInfo info;
    if(found==cache.sources.end()||rows<=0||columns<=0||
       !h1_image_probe(path.c_str(),1,&info)||image->mWidth%columns||image->mHeight%rows||
       info.width!=(uint32_t)(image->mWidth/columns)||info.height!=(uint32_t)(image->mHeight/rows))return false;
    found->second.alphaPath=path;found->second.alphaRows=rows;found->second.alphaColumns=columns;
    found->second.ClearRGB();found->second.ClearFontPages();found->second.ClearStorage();Recount();
    image->mHasAlpha=image->mHasTrans=true;
    return !image->mBits||ApplyAlpha(image,found->second);
}
bool H1ImageCacheAfterSanding(MemoryImage *image,void (*sanding)(Image*),H1SandingAverage average)
{
    Cache& cache=State();auto found=cache.sources.find(image);
    if(found==cache.sources.end())return false;
    /* Sanding changes only RGB at zero-alpha edges. Reapplying the exact
     * upstream operation after reload reconstructs those pixels faithfully. */
    found->second.sanded=true;found->second.sandingAverage=average;cache.sanding=sanding;image->mFilePath=found->second.path;
    return true;
}
bool H1ImageCacheGetRGBStats(MemoryImage *image,H1ImageCacheRGBStats *stats){
    if(stats)*stats={0,0,0,0,false};
    if(!cacheInstance||!image)return false;
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return false;
    const Source& source=found->second;
    if(stats)*stats={source.rgbPages?source.rgbPageCount:0,source.rgbBytes,
        source.rgbPages?source.rgbPageCount*sizeof(unsigned char*):0,
        source.rgbBytes?std::min(RGBPageBytes,source.rgbBytes):0,RGBEligible(image,source),source.rgbLoads,source.rgbBlits,source.rgbARGBLoads};
    return true;
}
bool H1ImageCacheGetFontPageStats(MemoryImage *image,H1ImageCacheFontPageStats *stats){
    if(stats)*stats={0,0,0,0,0,0,0};
    if(!image||!cacheInstance)return false;
    auto found=cacheInstance->sources.find(image);if(found==cacheInstance->sources.end())return false;
    const Source& source=found->second;
    if(stats)*stats={source.fontPages?source.fontPageCount:0,source.fontBytes,
        source.fontPages?source.fontPageCount*sizeof(uint32_t*):0,
        source.fontBytes?std::min(FontPageBytes,source.fontBytes):0,source.fontLoads,source.fontBlits,source.fontARGBLoads};
    return true;
}
#ifdef PVZ_HOST_RUNNER
void H1ImageCacheStorageAlphaTestEnable(bool enabled){storageAlphaEnabled=enabled;if(cacheInstance){for(auto& item:cacheInstance->sources)if(item.second.alphaPaletteEligible)item.second.ClearStorage();Recount();}}
void H1ImageCacheRGBTestFailure(int allocationsBeforeFailure,int readsBeforeFailure){rgbAllocFailure=allocationsBeforeFailure;rgbReadFailure=readsBeforeFailure;}
void H1ImageCacheAlphaTestFailure(int allocationsBeforeFailure,int readsBeforeFailure){alphaAllocFailure=allocationsBeforeFailure;alphaReadFailure=readsBeforeFailure;}
void H1ImageCacheFontPaletteTestFailure(int allocationsBeforeFailure,int readsBeforeFailure){fontAllocFailure=allocationsBeforeFailure;fontReadFailure=readsBeforeFailure;}
void H1ImageCacheFontPageTestFailure(int allocationsBeforeFailure,int readsBeforeFailure){fontPageAllocFailure=allocationsBeforeFailure;fontPageReadFailure=readsBeforeFailure;}
#endif
bool H1ImageCacheBltOpaqueRGB(MemoryImage *image,MemoryImage *destination,int x,int y,const Rect& sourceRect){
    if(!image||!destination||image==destination||destination->mHasAlpha||destination->mHasTrans)return false;
    Cache& cache=State();auto found=cache.sources.find(image);
    if(found==cache.sources.end()||found->second.Reduced()||!RGBEligible(image,found->second))return false;
    if(sourceRect.mWidth<=0||sourceRect.mHeight<=0)return true;
    if(x<0||y<0||sourceRect.mX<0||sourceRect.mY<0||
       (int64_t)x+sourceRect.mWidth>destination->mWidth||(int64_t)y+sourceRect.mHeight>destination->mHeight||
       (int64_t)sourceRect.mX+sourceRect.mWidth>image->mWidth||(int64_t)sourceRect.mY+sourceRect.mHeight>image->mHeight)return false;
    H1ImageCacheTouch(image); /* Pin before target allocation, fopen or pages. */
    uint32_t *out=destination->GetBits();if(!out)return false;
    Source& source=found->second;
    if(image->mBits){
        /* A previous transformed/generic use may already have real ARGB.
         * The same narrow opaque copy avoids manufacturing RL data. */
        for(int row=0;row<sourceRect.mHeight;++row)
            std::memcpy(out+(std::size_t)(y+row)*destination->mWidth+x,
                image->mBits.get()+(std::size_t)(sourceRect.mY+row)*image->mWidth+sourceRect.mX,(std::size_t)sourceRect.mWidth*4);
    }else{
        if(image->mColorIndices||image->mColorTable||image->mNativeAlphaData||image->mRLAlphaData||image->mRLAdditiveData)return false;
        if(!LoadRGB(image,source))return false;
        for(int row=0;row<sourceRect.mHeight;++row){
            std::size_t offset=(std::size_t)(sourceRect.mY+row)*image->mWidth+sourceRect.mX;
            int remaining=sourceRect.mWidth;uint32_t *pixel=out+(std::size_t)(y+row)*destination->mWidth+x;
            while(remaining){
                const std::size_t page=offset/RGBPagePixels,within=offset%RGBPagePixels;
                const std::size_t n=std::min((std::size_t)remaining,RGBPagePixels-within);
                const unsigned char *rgb=source.rgbPages[page]+within*3;
                for(std::size_t i=0;i<n;++i,rgb+=3)*pixel++=0xff000000u|((uint32_t)rgb[0]<<16)|((uint32_t)rgb[1]<<8)|rgb[2];
                offset+=n;remaining-=(int)n;
            }
        }
    }
    ++source.rgbBlits;image->mDrawn=true;destination->BitsChanged();return true;
}
void H1ImageCacheSetBudget(std::size_t bytes){State().stats.budgetBytes=bytes;}
void H1ImageCacheEndFrame()
{
    Cache& cache=State();std::size_t bytes=Recount();
    // Most completed frames only reuse resident images. No eviction victim or
    // second set of counters is needed while the accurate recount fits.
    if(bytes<=cache.stats.budgetBytes){
        cache.frameBeginUse=cache.useClock;++cache.stats.frames;++cache.pixelEpoch;
        UpdateDiagnostic(cache);return;
    }
    /* A large background is normally drawn first. Pure LRU would throw it out
     * every frame to save a few glyphs and reread several MiB on the next draw.
     * Prefer the largest source actually used this frame that fits the budget;
     * unused large images receive no preference. No pointer is held past here. */
    MemoryImage *preferred=nullptr;std::size_t preferredBytes=0;uint64_t preferredUse=0;
#ifdef PVZ_HOST_RUNNER
    const char *preferSetting=std::getenv("PVZ_HOST_CACHE_PREFER_LARGE");
    const bool preferLarge=!preferSetting||preferSetting[0]!='0';
#else
    const bool preferLarge=true;
#endif
    for(auto& entry:cache.sources){
        const std::size_t size=Size(entry.first,&entry.second);
        if(!preferLarge||!size||size>cache.stats.budgetBytes||entry.second.lastUse<=cache.frameBeginUse)continue;
        if(size>preferredBytes||(size==preferredBytes&&entry.second.lastUse>preferredUse)){
            preferred=entry.first;preferredBytes=size;preferredUse=entry.second.lastUse;
        }
    }
    while(bytes>cache.stats.budgetBytes){
        auto oldest=cache.sources.end();
        for(auto it=cache.sources.begin();it!=cache.sources.end();++it){
            if(!Size(it->first,&it->second)||it->first==preferred)continue;
            if(it->first->mFilePath!=it->second.path)continue;
            if(oldest==cache.sources.end()||it->second.lastUse<oldest->second.lastUse)oldest=it;
        }
        if(oldest==cache.sources.end()){
            if(preferred){preferred=nullptr;continue;}
            break;
        }
        MemoryImage *image=oldest->first;
        bytes-=Size(image,&oldest->second);oldest->second.ClearRGB();oldest->second.ClearFontPages();oldest->second.ClearStorage();
        image->mBits.reset();image->mColorIndices.reset();image->mColorTable.reset();
        image->mNativeAlphaData.reset();image->mRLAlphaData.reset();image->mRLAdditiveData.reset();
        ++cache.stats.evictions;
    }
    cache.stats.residentBytes=bytes;cache.frameBeginUse=cache.useClock;++cache.stats.frames;++cache.pixelEpoch;
    // Eviction changed backing allocations; one recount publishes all formats.
    Recount();
}
/* A top-level Widget::DrawAll has completely returned. Mark an allocation-
 * pressure boundary only: no eviction, screen clearing or presentation. */
extern "C" void pvzh1_cache_complete_draw_batch(){
    if(cacheInstance)++cacheInstance->pixelEpoch;
}
H1ImageCacheStats H1ImageCacheGetStats(){Recount();return State().stats;}
uint64_t H1ImageCacheSourceCookie(MemoryImage* image) noexcept {
    if(!cacheInstance)return 0;
    auto found=cacheInstance->sources.find(image);
    return found==cacheInstance->sources.end()?0:found->second.identity;
}
H1ImageCacheStats H1ImageCacheCompletedFrameStats(){return State().stats;}
extern "C" uint64_t pvzh1_image_pixel_bytes_loaded(){return State().loadedPixelBytes;}
extern "C" size_t pvzh1_cache_memory_pressure(size_t requested){
    if(!cacheInstance||pressureInProgress)return 0;
    pressureInProgress=true;
    Cache& cache=*cacheInstance;++cache.pressureCalls;
    pvzh1_cache_pressure_diagnostic[0]=(uint32_t)cache.pressureCalls;
    if(pvzh1_text_replay_release_pressure){
        const size_t hint=pvzh1_text_replay_release_pressure(requested);
        if(hint){pvzh1_cache_pressure_diagnostic[3]=0;pressureInProgress=false;return hint;}
    }
    if(pvzh1_render_plan_release_pressure){
        const size_t hint=pvzh1_render_plan_release_pressure(requested);
        if(hint){
            /* Do not attribute render metadata to source pixel eviction/bytes.
             * A successful real allocation retry may preserve every image. */
            pvzh1_cache_pressure_diagnostic[3]=0;
            pressureInProgress=false;return hint;
        }
    }
    size_t freed=0,bestBuffer=0,bestBytes=0;bool bestCold=false,bestFits=false;
    auto chosen=cache.sources.end();
    /* No allocation, logging, descriptor/map mutation, or epoch advancement.
     * Current drawing-batch getters and palette readers pin before allocating. */
    for(auto& entry:cache.sources){
        MemoryImage *image=entry.first;
        if(entry.second.lastEpoch>=cache.pixelEpoch||image->mFilePath!=entry.second.path)continue;
        const size_t bytes=Size(image,&entry.second);if(!bytes)continue;
        const bool cold=entry.second.lastUse<=cache.frameBeginUse;
        const size_t buffer=LargestBuffer(image,&entry.second);const bool fits=buffer>=requested;
        bool better=chosen==cache.sources.end();
        if(!better&&cold!=bestCold)better=cold;
        else if(!better&&cold==bestCold){
            if(fits!=bestFits)better=fits;
            else if(fits){
                if(buffer!=bestBuffer)better=buffer<bestBuffer;
                else if(bytes!=bestBytes)better=bytes<bestBytes;
                else better=entry.second.lastUse<chosen->second.lastUse;
            }else better=entry.second.lastUse<chosen->second.lastUse;
        }
        if(better){chosen=cache.sources.find(image);bestCold=cold;bestFits=fits;bestBuffer=buffer;bestBytes=bytes;}
    }
    /* One source per callback. The runtime retries the actual allocation after
     * each release, asking again only if it still fails. In particular, a tiny
     * metadata request no longer discards every completed background image. */
    if(chosen!=cache.sources.end()){
        MemoryImage *image=chosen->first;freed=bestBytes;
        chosen->second.ClearRGB();chosen->second.ClearFontPages();chosen->second.ClearStorage();
        image->mBits.reset();image->mColorIndices.reset();image->mColorTable.reset();
        image->mNativeAlphaData.reset();image->mRLAlphaData.reset();image->mRLAdditiveData.reset();
        ++cache.stats.evictions;++cache.pressureEvictions;
    }
    cache.pressureBytes+=freed;
    pvzh1_cache_pressure_diagnostic[1]=(uint32_t)cache.pressureEvictions;
    pvzh1_cache_pressure_diagnostic[2]=(uint32_t)cache.pressureBytes;
    pvzh1_cache_pressure_diagnostic[3]=(uint32_t)freed;
    size_t bytes=0,rgbBytes=0,rgbPages=0,fontBytes=0,fontPages=0,rasterBytes=0,rasterPages=0;for(auto& entry:cache.sources){bytes+=Size(entry.first,&entry.second);rgbBytes+=entry.second.rgbBytes;if(entry.second.rgbPages)rgbPages+=entry.second.rgbPageCount;
        if(entry.second.rasterStorage){rasterBytes+=entry.second.fontBytes;if(entry.second.fontPages)rasterPages+=entry.second.fontPageCount;}
        else{fontBytes+=entry.second.fontBytes;if(entry.second.fontPages)fontPages+=entry.second.fontPageCount;}}
    pvzh1_paged_rgb_diagnostic[0]=(uint32_t)rgbBytes;pvzh1_paged_rgb_diagnostic[1]=(uint32_t)rgbPages;
    pvzh1_rgba_font_page_diagnostic[0]=(uint32_t)fontBytes;pvzh1_rgba_font_page_diagnostic[1]=(uint32_t)fontPages;
    pvzh1_rgba_raster_page_diagnostic[0]=(uint32_t)rasterBytes;pvzh1_rgba_raster_page_diagnostic[1]=(uint32_t)rasterPages;
    cache.stats.residentBytes=bytes;
    UpdateDiagnostic(cache);
    Recount();
    pressureInProgress=false;return freed;
}
extern "C" void pvzh1_cache_pressure_stats(uint64_t *calls,uint64_t *evictions,uint64_t *bytes){
    if(calls)*calls=cacheInstance?cacheInstance->pressureCalls:0;
    if(evictions)*evictions=cacheInstance?cacheInstance->pressureEvictions:0;
    if(bytes)*bytes=cacheInstance?cacheInstance->pressureBytes:0;
}
}
