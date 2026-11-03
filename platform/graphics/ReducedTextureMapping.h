#ifndef PVZH1_REDUCED_TEXTURE_MAPPING_H
#define PVZH1_REDUCED_TEXTURE_MAPPING_H
#include <cstdint>
#include <cmath>
#include <bit>
#ifdef PVZH1_FAST3_TEST
extern "C" bool pvzh1_fast3_test_enabled;
#endif

namespace Sexy {
struct H1ReducedTextureBounds {int left,top,right,bottom;};
/* The converter paints each logical cell/glyph into these exact floor bins.
 * Snap only original texel-aligned vertices, before screen clipping creates
 * fractional UVs. Continuous mesh UVs retain their original interpolation. */
inline bool H1MapReducedTriangle(float (&u)[3],float (&v)[3],uint32_t logicalWidth,
    uint32_t logicalHeight,uint32_t storageWidth,uint32_t storageHeight,
    H1ReducedTextureBounds& bounds)
{
    if(!logicalWidth||!logicalHeight||!storageWidth||!storageHeight)return false;
    bounds={0,0,(int)storageWidth,(int)storageHeight};
    // PVZH1_FULL_RECT_UV_IDENTITY: canonical full-image endpoints map to the
    // same 0/1 bits and full storage bounds. Cells/meshes retain the oracle.
    bool fullRect=true;
    for(int i=0;i<3;++i){
        const uint32_t a=std::bit_cast<uint32_t>(u[i]),b=std::bit_cast<uint32_t>(v[i]);
        if((a!=0u&&a!=0x3f800000u)||(b!=0u&&b!=0x3f800000u)){fullRect=false;break;}
    }
#ifdef PVZH1_FAST3_TEST
    if(pvzh1_fast3_test_enabled&&fullRect)return true;
#else
    if(fullRect)return true;
#endif
    int64_t x[3],y[3];
    for(int i=0;i<3;++i){
        const double px=(double)u[i]*logicalWidth,py=(double)v[i]*logicalHeight;
        if(!std::isfinite(px)||!std::isfinite(py)||px<0||py<0||px>logicalWidth||py>logicalHeight)return true;
        x[i]=(int64_t)std::llround(px);y[i]=(int64_t)std::llround(py);
        /* Float-normalized source rectangles incur <0.002 texel error even
         * on the original widest font strips. This does not quantize meshes. */
        if(std::fabs(px-x[i])>0.01||std::fabs(py-y[i])>0.01)return true;
    }
    int64_t minX=x[0],maxX=x[0],minY=y[0],maxY=y[0];
    for(int i=1;i<3;++i){
        if(x[i]<minX)minX=x[i];
        if(x[i]>maxX)maxX=x[i];
        if(y[i]<minY)minY=y[i];
        if(y[i]>maxY)maxY=y[i];
    }
    if(minX==maxX||minY==maxY)return true;
    bounds={(int)(minX*storageWidth/logicalWidth),(int)(minY*storageHeight/logicalHeight),
        (int)(maxX*storageWidth/logicalWidth),(int)(maxY*storageHeight/logicalHeight)};
    if(bounds.right<=bounds.left||bounds.bottom<=bounds.top)return false;
    for(int i=0;i<3;++i){
        u[i]=(float)(x[i]*storageWidth/logicalWidth)/(float)storageWidth;
        v[i]=(float)(y[i]*storageHeight/logicalHeight)/(float)storageHeight;
    }
    return true;
}
}
#endif
