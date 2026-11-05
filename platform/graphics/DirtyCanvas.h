/* Retained pixels behind a proven opaque first draw. LGPL-3.0-or-later. */
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "ScreenLayout.h"

inline void (*h1AdditiveNormalDraw)()=nullptr;
inline void (*h1AdditiveBeginFrame)(uint32_t*)=nullptr;
struct H1DirtyCanvas {
    static constexpr unsigned Width=Sexy::H1ScreenWidth,Height=Sexy::H1ScreenHeight,
        TileWidth=Sexy::H1TileWidth,TileHeight=Sexy::H1TileHeight,Rows=Height/TileHeight,
        All=(uint32_t(1)<<(Width/TileWidth))-1,KeyBytes=160;
    void* owner=nullptr;uint32_t* pixels=nullptr;
    uint32_t previous[Rows]{},current[Rows]{},combined[Rows]{};
    unsigned char key[KeyBytes]{};unsigned keySize=0,protectedDepth=0;
    unsigned char domain[32]{};unsigned domainSize=0,staticDepth=0;
    bool pending=false,keyValid=false,rooted=false,limited=false,enabled=true;
    uint32_t roots=0,limitedRoots=0,fullClears=0,backgroundWritten=0,backgroundSkipped=0;
#ifdef PVZH1_WIDESCREEN
    void* beforeContext=nullptr;
    void(*beforeDraw)(void*,unsigned,unsigned,uint32_t)=nullptr;
#endif
    // Preserve the constructor's original zero-filled first back buffer. The
    // opaque black clear becomes pending only after the first presentation.
    void Bind(void* image,uint32_t* bits){Reset();owner=image;pixels=bits;}
    void Reset(){const bool setting=enabled;*this=H1DirtyCanvas{};enabled=setting;}
    void Black(){
        for(unsigned i=0;i<Width*Height;i+=8){
            pixels[i]=pixels[i+1]=pixels[i+2]=pixels[i+3]=0xff000000u;
            pixels[i+4]=pixels[i+5]=pixels[i+6]=pixels[i+7]=0xff000000u;
        }
    }
    void MarkAll(){for(auto& row:current)row=All;}
    void Domain(const void* identity,unsigned bytes){
        if(!owner||!identity||bytes>sizeof(domain))return;
        if(domainSize==bytes&&!std::memcmp(domain,identity,bytes))return;
        std::memcpy(domain,identity,bytes);domainSize=bytes;keyValid=false;
        if(!pending){rooted=limited=false;MarkAll();}
    }
    void RequireClear(){if(!pending)return;Black();++fullClears;pending=false;rooted=limited=keyValid=false;MarkAll();}
    void Raw(void* image){if(image!=owner)return;RequireClear();if(!protectedDepth){
#ifdef PVZH1_WIDESCREEN
        if(beforeDraw)beforeDraw(beforeContext,0,Rows-1,All);
#endif
        if(h1AdditiveNormalDraw)h1AdditiveNormalDraw();
        MarkAll();}}
    void Begin(void* image){
        if(image!=owner)return;
#ifdef PVZH1_WIDESCREEN
        beforeDraw=nullptr;beforeContext=nullptr;
#endif
        for(unsigned i=0;i<Rows;++i){previous[i]=current[i];current[i]=0;}
        if(h1AdditiveBeginFrame)h1AdditiveBeginFrame(pixels);
        pending=true;rooted=limited=false;
    }
    void Mark(void* image,int x,int y,int width,int height){
        if(image!=owner)return;RequireClear();
        if(h1AdditiveNormalDraw)h1AdditiveNormalDraw();
        if(width<=0||height<=0)return;
        int64_t right=(int64_t)x+width,bottom=(int64_t)y+height;
        if(x<0)x=0;if(y<0)y=0;if(right>Width)right=Width;if(bottom>Height)bottom=Height;
        if(right<=x||bottom<=y)return;
        const unsigned first=(unsigned)x/TileWidth,last=((unsigned)right-1)/TileWidth;
        const uint32_t mask=((uint32_t(1)<<(last-first+1))-1)<<first;
#ifdef PVZH1_WIDESCREEN
        if(beforeDraw)beforeDraw(beforeContext,(unsigned)y/TileHeight,((unsigned)bottom-1)/TileHeight,mask);
#endif
        for(unsigned row=(unsigned)y/TileHeight;row<=((unsigned)bottom-1)/TileHeight;++row)current[row]|=mask;
    }
    bool Root(void* image,const void* identity,unsigned bytes,bool presentationOnly=false){
        if(!enabled||image!=owner||!pending||!identity||!bytes||bytes>KeyBytes)return false;
        const bool same=keyValid&&keySize==bytes&&!std::memcmp(key,identity,bytes);
        if(presentationOnly){Black();++fullClears;}
        std::memcpy(key,identity,bytes);keySize=bytes;keyValid=true;
        pending=false;rooted=true;limited=same;
        ++roots;if(same)++limitedRoots;
        return true;
    }
    uint32_t RestoreRow(unsigned y)const{return limited?previous[y/TileHeight]:All;}
    uint32_t DamageRow(unsigned y)const{return limited?(previous[y/TileHeight]|current[y/TileHeight]):All;}
    const uint32_t* Presentation(const uint32_t* input){
        if(input!=pixels||pending||!rooted||!limited)return nullptr;
        for(unsigned i=0;i<Rows;++i)combined[i]=previous[i]|current[i];
        return combined;
    }
};
inline H1DirtyCanvas h1DirtyCanvas;
struct H1CanvasStaticLayer {
    H1CanvasStaticLayer(){++h1DirtyCanvas.staticDepth;}
    ~H1CanvasStaticLayer(){--h1DirtyCanvas.staticDepth;}
};
struct H1CanvasKnownDraw {
    bool active;
    H1CanvasKnownDraw(void* owner,int x,int y,int width,int height):active(owner==h1DirtyCanvas.owner){
        if(active){h1DirtyCanvas.Mark(owner,x,y,width,height);++h1DirtyCanvas.protectedDepth;}
    }
    ~H1CanvasKnownDraw(){if(active)--h1DirtyCanvas.protectedDepth;}
};
struct H1CanvasProtectedPixels {
    bool active;
    explicit H1CanvasProtectedPixels(void* owner):active(owner==h1DirtyCanvas.owner){
        if(active){h1DirtyCanvas.RequireClear();++h1DirtyCanvas.protectedDepth;}
    }
    ~H1CanvasProtectedPixels(){if(active)--h1DirtyCanvas.protectedDepth;}
};
