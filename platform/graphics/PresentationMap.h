/* Exact nearest presentation mapping for the fixed H1 LCD. LGPL-3.0-or-later. */
#ifndef PVZH1_PRESENTATION_MAP_H
#define PVZH1_PRESENTATION_MAP_H
#include <stdint.h>
#include <stddef.h>
#include "ScreenLayout.h"

struct H1PresentationMap {
    int width=0,height=0,dw=0,dh=0,ox=0,oy=0;
    unsigned x[480];
    size_t row[272];
    bool Configure(int w,int h){
        if(w<=0||h<=0)return false;
        if(width==w&&height==h)return true;
        width=w;height=h;dw=(int)((int64_t)w*272/h);dh=272;
        if(dw>480){dw=480;dh=(int)((int64_t)h*480/w);}
        ox=(480-dw)/2;oy=(272-dh)/2;
        if(dw>0)for(int i=0;i<dw;++i)x[i]=(unsigned)((uint64_t)(unsigned)i*(unsigned)w/(unsigned)dw);
        if(dh>0)for(int i=0;i<dh;++i)row[i]=(size_t)((uint64_t)(unsigned)i*(unsigned)h/(unsigned)dh)*(unsigned)w;
        return true;
    }
};

inline bool H1PresentRangesOverlap(uintptr_t a,size_t an,uintptr_t b,size_t bn){
    return a<=b?b-a<an:a-b<bn;
}
inline uintptr_t H1PresentAddressKey(uintptr_t address){
#if defined(__mips__) && UINTPTR_MAX == 0xffffffffu
    // Compare physical ranges of KSEG0/KSEG1 aliases; never create or access a
    // different cached/uncached pointer and never change cache state.
    if((address&0xc0000000u)==0x80000000u)return address&0x1fffffffu;
#endif
    return address;
}
inline bool H1CanPresent360(volatile uint32_t*out,const uint32_t* pixels,int w,int h,const H1PresentationMap& map){
    if(!out||!pixels||w!=360||h!=270||map.width!=360||map.height!=270||map.dw!=362||map.dh!=272||map.ox!=59||map.oy!=0)return false;
    const uintptr_t a=(uintptr_t)out,b=(uintptr_t)pixels,m=(uintptr_t)&map;
    if((a&3)||(b&3)||H1PresentRangesOverlap(a,480u*272u*4u,b,360u*270u*4u)||
       H1PresentRangesOverlap(a,480u*272u*4u,m,sizeof(map))||H1PresentRangesOverlap(b,360u*270u*4u,m,sizeof(map)))return false;
    const uintptr_t pa=H1PresentAddressKey(a),pb=H1PresentAddressKey(b),pm=H1PresentAddressKey(m);
    if(H1PresentRangesOverlap(pa,480u*272u*4u,pb,360u*270u*4u)||H1PresentRangesOverlap(pa,480u*272u*4u,pm,sizeof(map))||H1PresentRangesOverlap(pb,360u*270u*4u,pm,sizeof(map)))return false;
    // Preserve even a caller-modified cached map: a mismatch takes the original
    // code. Validation is per frame, not per output pixel, and uses no divides.
    for(unsigned x=0;x<362;++x)if(map.x[x]!=x-(x>0)-(x>181))return false;
    for(unsigned y=0;y<272;++y)if(map.row[y]!=(size_t)(y-(y>0)-(y>136))*360)return false;
    return true;
}
#if defined(__GNUC__)
#define H1_PRESENT_FORCE_INLINE inline __attribute__((always_inline))
#else
#define H1_PRESENT_FORCE_INLINE inline
#endif
H1_PRESENT_FORCE_INLINE void H1PresentMargin59(volatile uint32_t*& destination){
    for(unsigned group=0;group<7;++group){
        *destination++=0x101b14;*destination++=0x101b14;*destination++=0x101b14;*destination++=0x101b14;
        *destination++=0x101b14;*destination++=0x101b14;*destination++=0x101b14;*destination++=0x101b14;
    }
    *destination++=0x101b14;*destination++=0x101b14;*destination++=0x101b14;
}
H1_PRESENT_FORCE_INLINE void H1PresentCopy180(volatile uint32_t*& destination,const uint32_t*& source){
    for(unsigned group=0;group<22;++group){
        *destination++=*source++&0xffffffu;*destination++=*source++&0xffffffu;
        *destination++=*source++&0xffffffu;*destination++=*source++&0xffffffu;
        *destination++=*source++&0xffffffu;*destination++=*source++&0xffffffu;
        *destination++=*source++&0xffffffu;*destination++=*source++&0xffffffu;
    }
    *destination++=*source++&0xffffffu;*destination++=*source++&0xffffffu;
    *destination++=*source++&0xffffffu;*destination++=*source++&0xffffffu;
}
inline void H1PresentFixed360(volatile uint32_t* destination,const uint32_t* pixels){
    const uint32_t* row=pixels;
    for(unsigned y=0;y<272;++y){
        const uint32_t* source=row;H1PresentMargin59(destination);
        *destination++=*source&0xffffffu;H1PresentCopy180(destination,source);
        *destination++=*source&0xffffffu;H1PresentCopy180(destination,source);
        H1PresentMargin59(destination);
        if(y!=0&&y!=136)row+=360;
    }
}
#undef H1_PRESENT_FORCE_INLINE

inline void H1PresentCursorMapped(volatile uint32_t*out,int logicalPointerX,int logicalPointerY,const H1PresentationMap& map){
    const int cx=map.ox+(int)((int64_t)logicalPointerX*map.dw/Sexy::H1LogicalWidth);
    const int cy=map.oy+(int)((int64_t)logicalPointerY*map.dh/Sexy::H1LogicalHeight);
    if(cx>=map.ox&&cx<map.ox+map.dw&&cy>=map.oy&&cy<map.oy+map.dh){
        for(int d=-3;d<=3;++d){
            if(cx+d>=0&&cx+d<480)out[cy*480+cx+d]=0xffec55;
            if(cy+d>=0&&cy+d<272)out[(cy+d)*480+cx]=0xffec55;
        }
    }
}

inline bool H1CanPresent480(volatile uint32_t* out,const uint32_t* pixels,int w,int h,const H1PresentationMap& map){
    if(!out||!pixels||w!=480||h!=272||map.width!=w||map.height!=h||map.dw!=480||map.dh!=272||map.ox||map.oy)return false;
    const uintptr_t a=(uintptr_t)out,b=(uintptr_t)pixels,m=(uintptr_t)&map;
    constexpr size_t bytes=480u*272u*4u;
    if((a&3)||(b&3)||H1PresentRangesOverlap(a,bytes,b,bytes)||H1PresentRangesOverlap(a,bytes,m,sizeof(map))||H1PresentRangesOverlap(b,bytes,m,sizeof(map)))return false;
    const uintptr_t pa=H1PresentAddressKey(a),pb=H1PresentAddressKey(b),pm=H1PresentAddressKey(m);
    if(H1PresentRangesOverlap(pa,bytes,pb,bytes)||H1PresentRangesOverlap(pa,bytes,pm,sizeof(map))||H1PresentRangesOverlap(pb,bytes,pm,sizeof(map)))return false;
    for(unsigned x=0;x<480;++x)if(map.x[x]!=x)return false;
    for(unsigned y=0;y<272;++y)if(map.row[y]!=(size_t)y*480)return false;
    return true;
}
inline void H1PresentFixed480(volatile uint32_t* out,const uint32_t* pixels){
    for(unsigned i=0;i<480u*272u;i+=8){
        out[i]=pixels[i]&0xffffffu;out[i+1]=pixels[i+1]&0xffffffu;
        out[i+2]=pixels[i+2]&0xffffffu;out[i+3]=pixels[i+3]&0xffffffu;
        out[i+4]=pixels[i+4]&0xffffffu;out[i+5]=pixels[i+5]&0xffffffu;
        out[i+6]=pixels[i+6]&0xffffffu;out[i+7]=pixels[i+7]&0xffffffu;
    }
}

/* Keep volatile LCD stores: acquiring the firmware surface does not establish
 * that ordinary memcpy or a cached alias may replace its access contract. */
inline bool H1PresentMapped(volatile uint32_t *out,const uint32_t *pixels,int w,int h,
    int logicalPointerX,int logicalPointerY,H1PresentationMap& map)
{
    if(!out||!pixels||!map.Configure(w,h))return false;
    if(H1CanPresent360(out,pixels,w,h,map))H1PresentFixed360(out,pixels);
    else {
    for(int y=0;y<272;++y){
        volatile uint32_t *destination=out+y*480;
        for(int x=0;x<map.ox;++x)destination[x]=0x101b14;
        for(int x=map.ox+map.dw;x<480;++x)destination[x]=0x101b14;
        if(y<map.oy||y>=map.oy+map.dh){
            for(int x=map.ox;x<map.ox+map.dw;++x)destination[x]=0x101b14;
            continue;
        }
        const uint32_t *source=pixels+map.row[y-map.oy];
        for(int x=0;x<map.dw;++x)destination[map.ox+x]=source[map.x[x]]&0xffffffu;
    }
    }
    /* Input and the upstream pointer always use the original 800x600 logical
     * coordinates, even when the raster backing is only 360x270. */
    H1PresentCursorMapped(out,logicalPointerX,logicalPointerY,map);
    return true;
}
#endif
