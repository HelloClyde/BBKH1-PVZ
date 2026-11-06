/* Software adaptations of the PvZ-Portable drawing interface.
 * Existing MemoryImage/SWTri implement the ordinary raster paths; this file
 * supplies missing additive, repeated-texture, and coverage behavior.
 * SPDX-License-Identifier: LGPL-3.0-or-later AND LicenseRef-PopCap */
#include "SoftwareRaster.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/SWTri.h"
#include "MemoryImageCache.h"
#include "RenderPlanCache.h"
#include "DirtyCanvas.h"
#include "TextReplay.h"
#ifdef PVZ_NATIVE_PROFILE
#include "profiler.h"
#endif
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "h1_resources.h"
#include "zlib.h"
#include <algorithm>
#if defined(PVZ_HOST_RUNNER)
#include "h1_host.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#endif

/* Attempted rows, accepted rows, pixels, opaque stores, partial-alpha pixels,
 * transparent skips, unit-step rows, reserved. Updated only at row boundaries. */
extern "C" {volatile uint32_t pvzh1_axis_span_diagnostic[8];}
extern "C" {uint32_t pvzh1_direct_additive;volatile uint32_t pvzh1_additive_diagnostic[4];}
extern "C" {const unsigned char* pvzh1_additive_lut=nullptr;}
/* White alpha-only repeated water texture: accepted triangles, shaded pixels,
 * ordinary fallback triangles. No animation state is changed. */
extern "C" {volatile uint32_t pvzh1_white_repeat_diagnostic[3];}
extern "C" bool pvzh1_additive_prepare(){
    if(pvzh1_additive_lut)return true;
    char path[640];if(std::snprintf(path,sizeof(path),"%sdata/additive.h1lut",h1_resources_get_root())>=int(sizeof(path)))return false;
    FILE* file=std::fopen(path,"rb");if(!file)return false;
    unsigned char header[16];auto u32=[](const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);};
    bool ok=std::fread(header,1,16,file)==16&&!std::memcmp(header,"H1AL",4)&&u32(header+4)==1&&u32(header+8)==65536;
    unsigned char* data=ok?new(std::nothrow) unsigned char[65536]:nullptr;
    ok=ok&&data&&std::fread(data,1,65536,file)==65536&&std::fgetc(file)==EOF&&crc32(0,data,65536)==u32(header+12);
    std::fclose(file);if(!ok){delete[] data;return false;}pvzh1_additive_lut=data;return true;
}
extern "C" void pvzh1_additive_shutdown(){delete[] pvzh1_additive_lut;pvzh1_additive_lut=nullptr;}

namespace {
uintptr_t additiveBase=0;unsigned char saturatedTiles[30*34]{};
bool saturatedAny=false;
bool AdditivePosition(const uint32_t* p,unsigned& x,unsigned& y){
    const auto address=(uintptr_t)p;
    if(!additiveBase||address<additiveBase||address-additiveBase>=480u*272u*4u||(address-additiveBase)%4)return false;
    const unsigned index=(address-additiveBase)/4;x=index%480;y=index/480;return true;
}
}
extern "C" void pvzh1_additive_normal_draw(){if(!pvzh1_direct_additive&&saturatedAny){std::memset(saturatedTiles,0,sizeof(saturatedTiles));saturatedAny=false;}}
extern "C" void pvzh1_additive_begin_frame(uint32_t* pixels){additiveBase=(H1DirtyCanvas::Width==480&&H1DirtyCanvas::Height==272)?(uintptr_t)pixels:0;std::memset(saturatedTiles,0,sizeof(saturatedTiles));saturatedAny=false;}
extern "C" unsigned pvzh1_additive_span(uint32_t* pixels,unsigned count,unsigned* span){
    unsigned x,y;if(!AdditivePosition(pixels,x,y)){*span=count;return 0;}
    *span=std::min(count,16u-(x&15));if(saturatedTiles[(y/8)*30+x/16]){++pvzh1_additive_diagnostic[3];return *span;}return 0;
}
extern "C" void pvzh1_additive_span_done(uint32_t* pixels){
    unsigned x,y;if(!AdditivePosition(pixels,x,y)||(y&7)!=7)return;
    const auto tile=(y/8)*30+x/16;auto* base=(const uint32_t*)additiveBase+(y&~7u)*480+(x&~15u);
    for(unsigned row=0;row<8;++row)for(unsigned col=0;col<16;++col)if((base[row*480+col]&0xffffffu)!=0xffffffu)return;
    saturatedTiles[tile]=1;saturatedAny=true;
}
#ifdef PVZ_HOST_RUNNER
static bool directAdditiveEnabled=true;
static bool whiteRepeatEnabled=true;
#endif
#ifdef PVZ_HOST_RUNNER
extern "C" {bool pvzh1_geometry_fast_enabled=true;}
#endif
#ifdef PVZH1_SAMPLER_TEST
extern "C" bool pvzh1_sampler_test_enabled;
#endif

namespace Sexy {
#ifdef PVZ_HOST_RUNNER
static bool axisSpanEnabled=true;
void H1DirectAdditiveTestEnable(bool enabled){directAdditiveEnabled=enabled;}
void H1FastReducedSpanTestEnable(bool enabled){axisSpanEnabled=enabled;}
void H1GeometryFastTestEnable(bool enabled){pvzh1_geometry_fast_enabled=enabled;}
void H1WhiteRepeatTestEnable(bool enabled){whiteRepeatEnabled=enabled;}
#endif
bool H1FastReducedSpan(uint32_t *destination,int count,const SWHelper::SWTextureInfo *texture,
                      unsigned u,unsigned v,int du,int dv,bool sourceAlpha)
{
#ifndef H1_PVZ_PORTABLE // PVZH1_UNREDUCED_TEXTURE_ABI
    // Original SWTextureInfo has no H1 physical-cell fields.
    (void)destination;(void)count;(void)texture;(void)u;(void)v;(void)du;(void)dv;(void)sourceAlpha;
    return false;
#else

    pvzh1_axis_span_diagnostic[0]=pvzh1_axis_span_diagnostic[0]+1;
#ifdef PVZ_HOST_RUNNER
    if(!axisSpanEnabled)return false;
#endif
    if(count<8||!destination||!texture||texture->captureText||!texture->reducedClamp||(!texture->pTexture&&!texture->pages)||dv!=0||
       texture->clampRight<=texture->clampLeft||texture->clampBottom<=texture->clampTop)return false;
    /* Only accept spans wholly inside their physical cell. Border clamps,
     * wrapped fixed-point coordinates and arbitrary triangles keep the original
     * per-pixel path. Endpoints suffice because u advances monotonically. */
    const int64_t end=(int64_t)u+(int64_t)(count-1)*du;
    if(end<0||end>UINT32_MAX)return false;
    const unsigned first=u>>16,last=(unsigned)end>>16;
    if(first<(unsigned)texture->clampLeft||first>=(unsigned)texture->clampRight||
       last<(unsigned)texture->clampLeft||last>=(unsigned)texture->clampRight)return false;
    unsigned y=v>>16;
    if(y<(unsigned)texture->clampTop)y=(unsigned)texture->clampTop;
    if(y>=(unsigned)texture->clampBottom)y=(unsigned)texture->clampBottom-1;
    const uint32_t *row=texture->pTexture?texture->pTexture+(size_t)y*texture->pitch:nullptr;
    auto texel=[&](unsigned x){const unsigned at=y*texture->pitch+x;return row?row[x]:texture->pages[at>>10u][at&1023u];};
    unsigned opaque=0,partial=0,transparent=0;
    if(!sourceAlpha
#ifdef PVZH1_SAMPLER_TEST
       &&pvzh1_sampler_test_enabled
#endif
    ){
        // One dispatch per row. Preserve ordered loads/stores even if a caller
        // supplies overlapping memory; no restrict or batched speculative load.
        int remaining=count;
        while(remaining>=4){
            *destination++=texel(u>>16)|0xff000000u;u+=(unsigned)du;
            *destination++=texel(u>>16)|0xff000000u;u+=(unsigned)du;
            *destination++=texel(u>>16)|0xff000000u;u+=(unsigned)du;
            *destination++=texel(u>>16)|0xff000000u;u+=(unsigned)du;
            remaining-=4;
        }
        while(remaining--){*destination++=texel(u>>16)|0xff000000u;u+=(unsigned)du;}
        opaque=(unsigned)count;
    }else for(int i=0;i<count;++i,u+=(unsigned)du){
        const uint32_t tex=texel(u>>16);
        const unsigned alpha=sourceAlpha?tex>>24:255;
        if(alpha<=8){++transparent;continue;}
        if(alpha>=240){destination[i]=tex|0xff000000u;++opaque;continue;}
        /* Bit-identical to SWTri_Pixel888.inc, including its /256 rounding. */
        const uint32_t trb=(((tex&0xff00ffu)*alpha)>>8)&0xff00ffu;
        const uint32_t tg=(((tex&0x00ff00u)*alpha)>>8)&0x00ff00u;
        const uint32_t p=destination[i],inverse=255-alpha;
        const uint32_t prb=(((p&0xff00ffu)*inverse)>>8)&0xff00ffu;
        const uint32_t pg=(((p&0x00ff00u)*inverse)>>8)&0x00ff00u;
        destination[i]=0xff000000u|((trb|tg)+(prb|pg));++partial;
    }
    pvzh1_axis_span_diagnostic[1]=pvzh1_axis_span_diagnostic[1]+1;
    pvzh1_axis_span_diagnostic[2]=pvzh1_axis_span_diagnostic[2]+(unsigned)count;
    pvzh1_axis_span_diagnostic[3]=pvzh1_axis_span_diagnostic[3]+opaque;
    pvzh1_axis_span_diagnostic[4]=pvzh1_axis_span_diagnostic[4]+partial;
    pvzh1_axis_span_diagnostic[5]=pvzh1_axis_span_diagnostic[5]+transparent;
    if(du==65536)pvzh1_axis_span_diagnostic[6]=pvzh1_axis_span_diagnostic[6]+1;
    return true;

#endif
}
void H1TracePixel(MemoryImage *dest,Image *source,const char *operation)
{
#if defined(PVZ_HOST_RUNNER)
    static bool configured=false,enabled=false;
    static int x=0,y=0;static uint32_t last=0;static FILE *trace=nullptr;
    if(!configured){
        configured=true;const char *position=getenv("PVZ_HOST_TRACE_PIXEL");
        if(position&&sscanf(position,"%d,%d",&x,&y)==2){
            const char *directory=getenv("PVZ_HOST_OUTPUT");std::string path=directory?directory:".";path+="/pixel-trace.txt";
            trace=fopen(path.c_str(),"ab");enabled=trace!=nullptr;
        }
    }
    if(!enabled||dest->mWidth!=800||dest->mHeight!=600||x<0||x>=800||y<0||y>=600||!dest->mBits)return;
    uint32_t pixel=dest->mBits[y*800+x];
    if(pixel!=last){
        fprintf(trace,"frame=%u pixel=%08x operation=%s source=%p dims=%dx%d path=%s\n",
            pvzp_diagnostic[2]+1,pixel,operation,(void*)source,source?source->mWidth:0,source?source->mHeight:0,source?source->mFilePath.c_str():"");
        fflush(trace);last=pixel;
    }
#else
    (void)dest;(void)source;(void)operation;
#endif
}
static Rect Bounds(MemoryImage *dest,const Rect& clip)
{ return clip.Intersection(Rect(0,0,dest->mWidth,dest->mHeight)); }

static uint32_t Composite(uint32_t dest,uint32_t src,int mode)
{
    const unsigned sa=src>>24;
    if (!sa) return dest;
    unsigned dr=(dest>>16)&255,dg=(dest>>8)&255,db=dest&255;
    unsigned sr=(src>>16)&255,sg=(src>>8)&255,sb=src&255;
    if (mode==Graphics::DRAWMODE_ADDITIVE) {
        dr=std::min(255u,dr+(sr*sa+127)/255);
        dg=std::min(255u,dg+(sg*sa+127)/255);
        db=std::min(255u,db+(sb*sa+127)/255);
        return (dest&0xff000000u)|(dr<<16)|(dg<<8)|db;
    }
    if (sa==255) return src;
    const unsigned da=dest>>24;
    const unsigned oa=sa+(da*(255-sa)+127)/255;
    if (!oa) return 0;
    dr=(sr*sa*255+dr*da*(255-sa)+oa*127)/(oa*255);
    dg=(sg*sa*255+dg*da*(255-sa)+oa*127)/(oa*255);
    db=(sb*sa*255+db*da*(255-sa)+oa*127)/(oa*255);
    return (oa<<24)|(dr<<16)|(dg<<8)|db;
}

void H1FillRect(MemoryImage *dest,const Rect& rect,const Color& color,int mode)
{
    if(mode==Graphics::DRAWMODE_NORMAL&&color.mAlpha==0)return;
    h1AdditiveNormalDraw=pvzh1_additive_normal_draw;h1AdditiveBeginFrame=pvzh1_additive_begin_frame;
    Rect r=Bounds(dest,rect);
    if (r.mWidth<=0||r.mHeight<=0) return;
    H1CanvasKnownDraw canvas(dest,r.mX,r.mY,r.mWidth,r.mHeight);
    if (mode==Graphics::DRAWMODE_NORMAL) {
        if(dest->mForcedMode&&!dest->mHasAlpha&&!dest->mHasTrans){
            uint32_t* bits=dest->GetBits();const uint32_t src=color.ToInt();
            const unsigned alpha=src>>24;
            for(int y=r.mY;y<r.mY+r.mHeight;++y){
                uint32_t* out=bits+(size_t)y*dest->mWidth+r.mX;
                if(alpha==255){
                    int n=r.mWidth;
                    while(n>=4){out[0]=out[1]=out[2]=out[3]=src;out+=4;n-=4;}
                    while(n--)*out++=src;
                }else for(int x=0;x<r.mWidth;++x){
                    const uint32_t p=out[x];const unsigned da=p>>24;
                    // Screen pixels are normally opaque. In that case the
                    // original general-alpha formula simplifies exactly and
                    // needs no per-pixel integer division. Keep its rounding
                    // and the general case for any unusual input pixel.
                    const unsigned oa=da==255?255:da+((255-da)*alpha)/255;
                    const unsigned a=da==255?alpha:oa?255*alpha/oa:0;
                    const unsigned inverse=256-a;
#ifdef OPTIMIZE_SOFTWARE_DRAWING
                    out[x]=(oa<<24)|((((p&0xff00ffu)*inverse+(src&0xff00ffu)*a)>>8)&0xff00ffu)|
                        ((((p&0x00ff00u)*inverse+(src&0x00ff00u)*a)>>8)&0x00ff00u);
#else
                    out[x]=(oa<<24)|(((((p&0x0000ffu)*inverse)>>8)+(((src&0x0000ffu)*a)>>8))&0x0000ffu)|
                        (((((p&0x00ff00u)*inverse)>>8)+(((src&0x00ff00u)*a)>>8))&0x00ff00u)|
                        (((((p&0xff0000u)*inverse)>>8)+(((src&0xff0000u)*a)>>8))&0xff0000u);
#endif
                }
            }
            dest->BitsChanged();H1TracePixel(dest,nullptr,"fill");return;
        }
        dest->MemoryImage::FillRect(r,color,mode);
        H1TracePixel(dest,nullptr,"fill");
        return;
    }
    uint32_t *bits=dest->GetBits();
    const uint32_t pixel=color.ToInt();
    for (int y=r.mY;y<r.mY+r.mHeight;++y)
        for (int x=r.mX;x<r.mX+r.mWidth;++x)
            bits[y*dest->mWidth+x]=Composite(bits[y*dest->mWidth+x],pixel,mode);
    dest->BitsChanged();
    H1TracePixel(dest,nullptr,"additive-fill");
}

void H1Coverage(MemoryImage *dest,Span *spans,int count,const Color& color,int mode,
                const uint8_t *coverage,int coverX,int coverY,int width,int height)
{
    if (!coverage||width<=0||height<=0) return;
    H1CanvasKnownDraw canvas(dest,coverX,coverY,width,height);
    uint32_t *bits=dest->GetBits();
    const uint32_t rgb=color.ToInt()&0xffffffu;
    for (int i=0;i<count;++i) {
        const Span& s=spans[i];
        int y=s.mY;
        if (y<0||y>=dest->mHeight||y<coverY||y>=coverY+height) continue;
        int left=std::max(std::max(0,s.mX),coverX);
        int right=std::min(std::min(dest->mWidth,s.mX+s.mWidth),coverX+width);
        for (int x=left;x<right;++x) {
            unsigned a=(coverage[(y-coverY)*width+x-coverX]*color.mAlpha+127)/255;
            bits[y*dest->mWidth+x]=Composite(bits[y*dest->mWidth+x],rgb|(a<<24),mode);
        }
    }
    dest->BitsChanged();
}

void H1CoverageScaled(MemoryImage *dest,Span *spans,int count,const Color& color,int mode,
    const uint8_t *coverage,int coverX,int coverY,int width,int height,int logicalWidth,int logicalHeight)
{
    if(!dest||!spans||count<=0||!coverage||width<=0||height<=0||logicalWidth<=0||logicalHeight<=0||
       dest->mWidth<=0||dest->mHeight<=0||dest->mWidth>480)return;
    H1CanvasKnownDraw canvas(dest,0,0,dest->mWidth,dest->mHeight);
    int logicalX[480];
    const uint64_t den=(uint64_t)dest->mWidth*2,step=(uint64_t)logicalWidth*2;
    uint64_t sample=logicalWidth/den,error=logicalWidth%den;
    const uint64_t whole=step/den,remainder=step%den;
    for(int x=0;x<dest->mWidth;++x){logicalX[x]=(int)sample;sample+=whole;error+=remainder;if(error>=den){++sample;error-=den;}}
    uint32_t *bits=dest->GetBits();const uint32_t rgb=color.ToInt()&0xffffffu;
    for(int y=0;y<dest->mHeight;++y){
        const int ly=(int)(((int64_t)y*2+1)*logicalHeight/((int64_t)dest->mHeight*2));
        if(ly<coverY||(int64_t)ly>=((int64_t)coverY+height))continue;
        for(int i=0;i<count;++i){
            const Span& s=spans[i];if(s.mY!=ly)continue;
            for(int x=0;x<dest->mWidth;++x){
                const int lx=logicalX[x];
                if(lx<s.mX||(int64_t)lx>=(int64_t)s.mX+s.mWidth||lx<coverX||(int64_t)lx>=(int64_t)coverX+width)continue;
                const unsigned a=(coverage[(std::size_t)(ly-coverY)*width+lx-coverX]*color.mAlpha+127)/255;
                const std::size_t at=(std::size_t)y*dest->mWidth+x;
                bits[at]=Composite(bits[at],rgb|(a<<24),mode);
            }
        }
    }
    dest->BitsChanged();
}

static void OriginalTriangle(MemoryImage *dest,MemoryImage *texture,const TriVertex vertices[3],
                              const Rect& clip,const Color& color,int mode,bool linear)
{
    if (texture) {
        TriVertex a[1][3]={{vertices[0],vertices[1],vertices[2]}};
        int format=dest->mForcedMode&&!dest->mHasAlpha&&!dest->mHasTrans?0x888:0x8888;
        dest->MemoryImage::BltTrianglesTexHelper(texture,a,1,clip,color,mode,dest->GetBits(),dest->mWidth*4,format,0,0,linear);
        dest->BitsChanged();
    } else {
        SWHelper::XYZStruct v[3];
        for (int j=0;j<3;++j) {
            v[j].mX=vertices[j].x;v[j].mY=vertices[j].y;
            v[j].mU=v[j].mV=0;
            v[j].mDiffuse=vertices[j].color?vertices[j].color:0xffffffffu;
        }
        /* SWTri's untextured path requires vertex color modulation. */
        SWHelper::SWDrawShape(v,3,nullptr,color,mode,clip,dest->GetBits(),dest->mWidth*4,0x8888,linear,true);
        dest->BitsChanged();
    }
}

struct TexturePixels {
    const uint32_t *bits=nullptr;
    const uint32_t *const *pages=nullptr;
    uint32_t Read(std::size_t index) const {return pages?pages[index>>10u][index&1023u]:bits[index];}
};
static uint32_t ReadRepeat(const TexturePixels& bits,int width,int height,int x,int y)
{
    x%=width;y%=height;
    if (x<0)x+=width;if(y<0)y+=height;
    return bits.Read((std::size_t)y*width+x);
}

static uint32_t SampleRepeat(const TexturePixels& bits,int width,int height,float u,float v,bool linear)
{
    u-=std::floor(u);v-=std::floor(v);
    float x=u*width,y=v*height;
    if (!linear) return ReadRepeat(bits,width,height,(int)x,(int)y);
    x-=0.5f;y-=0.5f;
    int ix=(int)std::floor(x),iy=(int)std::floor(y);
    float fx=x-ix,fy=y-iy;
    uint32_t p[4]={ReadRepeat(bits,width,height,ix,iy),ReadRepeat(bits,width,height,ix+1,iy),
                   ReadRepeat(bits,width,height,ix,iy+1),ReadRepeat(bits,width,height,ix+1,iy+1)};
    float weight[4]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
    float a=0,r=0,g=0,b=0;
    for (int i=0;i<4;++i) {
        float wa=weight[i]*(p[i]>>24);
        a+=wa;r+=wa*((p[i]>>16)&255);g+=wa*((p[i]>>8)&255);b+=wa*(p[i]&255);
    }
    if (a<0.5f) return 0;
    return ((uint32_t)(a+0.5f)<<24)|((uint32_t)(r/a+0.5f)<<16)|
           ((uint32_t)(g/a+0.5f)<<8)|(uint32_t)(b/a+0.5f);
}

static float Edge(const TriVertex& a,const TriVertex& b,float x,float y)
{ return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x); }
static bool InclusiveEdge(const TriVertex& a,const TriVertex& b)
{ return b.y<a.y || (b.y==a.y&&b.x>a.x); }

/* The original water caustic is white RGB with animated alpha. Interpolate its
 * alpha directly, with wrapped bilinear taps. Q20 texture coordinates and Q24
 * vertex modulation remove floating point divisions from the pixel loop.
 * The general RGBA sampler below remains the oracle and fallback. */
static bool WhiteRepeatTriangle(MemoryImage* dest,const uint32_t* texture,
    const TriVertex v[3],float area,int left,int top,int right,int bottom,
    const Color& color,int mode,bool linear)
{
    if(area<4.f||right-left>64||bottom-top>64)return false;
    for(int i=0;i<3;++i)
        if(((v[i].color?v[i].color:0xffffffffu)&0xffffffu)!=0xffffffu||
           !std::isfinite(v[i].u)||!std::isfinite(v[i].v)||
           std::fabs(v[i].u)>8.f||std::fabs(v[i].v)>8.f||
           !std::isfinite(v[i].x)||!std::isfinite(v[i].y)||
           std::fabs(v[i].x)>4096.f||std::fabs(v[i].y)>4096.f)return false;
    constexpr int64_t unit=1ll<<20,alphaUnit=1ll<<24;
    struct Plane {int64_t origin,dx,dy;};
    auto plane=[&](double a,double b,double c,double scale,double offset){
        const double dx=((b-a)*(v[2].y-v[0].y)-(c-a)*(v[1].y-v[0].y))/area;
        const double dy=((v[1].x-v[0].x)*(c-a)-(v[2].x-v[0].x)*(b-a))/area;
        return Plane{(int64_t)std::llround((a+dx*(left+.5-v[0].x)+dy*(top+.5-v[0].y)+offset)*scale),
                     (int64_t)std::llround(dx*scale),(int64_t)std::llround(dy*scale)};
    };
    Plane u=plane(v[0].u*128.,v[1].u*128.,v[2].u*128.,unit,linear?-.5:0);
    Plane w=plane(v[0].v*64.,v[1].v*64.,v[2].v*64.,unit,linear?-.5:0);
    const double modulation=color.mAlpha/(255.*255.);
    Plane alpha=plane((v[0].color?v[0].color:0xffffffffu)>>24,
                      (v[1].color?v[1].color:0xffffffffu)>>24,
                      (v[2].color?v[2].color:0xffffffffu)>>24,alphaUnit*modulation,0);
    int64_t ex[3],ey[3],edge[3];
    for(int i=0;i<3;++i){
        const auto& a=v[(i+1)%3];const auto& b=v[(i+2)%3];
        const int64_t ax=std::llround(a.x*65536.),ay=std::llround(a.y*65536.);
        const int64_t bx=std::llround(b.x*65536.),by=std::llround(b.y*65536.);
        ex[i]=-(by-ay)*65536;ey[i]=(bx-ax)*65536;
        edge[i]=(bx-ax)*(((int64_t)top<<16)+32768-ay)-(by-ay)*(((int64_t)left<<16)+32768-ax);
    }
    auto* bits=dest->GetBits();unsigned shaded=0;
    const uint32_t rgb=(color.mRed<<16)|(color.mGreen<<8)|color.mBlue;
    constexpr int64_t guard=1ll<<28; // Near an edge use the exact original coverage test.
    for(int y=top;y<bottom;++y){
        int64_t e[3]={edge[0],edge[1],edge[2]},uu=u.origin,vv=w.origin,aa=alpha.origin;
        for(int x=left;x<right;++x,uu+=u.dx,vv+=w.dx,aa+=alpha.dx){
            bool inside=e[0]>=0&&e[1]>=0&&e[2]>=0;
            const bool boundary=(std::llabs(e[0])<=guard||std::llabs(e[1])<=guard||std::llabs(e[2])<=guard);
            e[0]+=ex[0];e[1]+=ex[1];e[2]+=ex[2];
            if(boundary){
                inside=true;
                for(int i=0;i<3;++i){const auto& a=v[(i+1)%3];const auto& b=v[(i+2)%3];
                    float d=Edge(a,b,x+.5f,y+.5f);if(d<0||(d==0&&!InclusiveEdge(a,b)))inside=false;}
            }
            if(!inside)continue;
            // Ten-bit rounded fractions keep the complete bilinear numerator
            // below 255*2^20. Native MIPS32 then needs no 64-bit multiply in
            // this pixel loop, while the original-shader oracle bounds error.
            const unsigned qu=(unsigned)((uu+512)>>10),qv=(unsigned)((vv+512)>>10);
            const unsigned ix=linear?(qu>>10)&127:(unsigned)(uu>>20)&127;
            const unsigned iy=linear?(qv>>10)&63:(unsigned)(vv>>20)&63;
            unsigned sample=texture[iy*128+ix]>>24;
            if(linear){
                const unsigned fx=qu&1023,fy=qv&1023;
                const unsigned a=sample*(1024-fx)+(texture[iy*128+((ix+1)&127)]>>24)*fx;
                const unsigned b=(texture[((iy+1)&63)*128+ix]>>24)*(1024-fx)+(texture[((iy+1)&63)*128+((ix+1)&127)]>>24)*fx;
                sample=(a*(1024-fy)+b*fy+(1u<<19))>>20;
            }
            const unsigned modulation=(unsigned)((std::max<int64_t>(0,std::min<int64_t>(alphaUnit,aa))+128)>>8);
            const unsigned opacity=(sample*modulation+32768)>>16;
            const auto at=(size_t)y*dest->mWidth+x;bits[at]=Composite(bits[at],rgb|(opacity<<24),mode);++shaded;
        }
        for(int i=0;i<3;++i)edge[i]+=ey[i];u.origin+=u.dy;w.origin+=w.dy;alpha.origin+=alpha.dy;
    }
    ++pvzh1_white_repeat_diagnostic[0];pvzh1_white_repeat_diagnostic[1]+=shaded;
    dest->BitsChanged();return true;
}

/* Only the original pool-caustic repeated texture uses this path. The original
 * SWTri lookup neither wraps UVs nor clamps each texture coordinate, whereas
 * the GL original uses GL_REPEAT and bilinear filtering across tile edges. */
static void RepeatTriangle(MemoryImage *dest,MemoryImage *texture,const TriVertex input[3],
                            const Rect& clip,const Color& color,int mode,bool linear,bool whiteRepeat)
{
#ifdef PVZ_NATIVE_PROFILE
    PVZProfileScope profile(PVZ_PROFILE_REPEAT_TRIANGLE);
#endif
    TriVertex v[3]={input[0],input[1],input[2]};
    float area=Edge(v[0],v[1],v[2].x,v[2].y);
    if (std::fabs(area)<0.00001f) return;
    if (area<0) {std::swap(v[1],v[2]);area=-area;}
    int left=std::max(clip.mX,(int)std::floor(std::min({v[0].x,v[1].x,v[2].x})));
    int top=std::max(clip.mY,(int)std::floor(std::min({v[0].y,v[1].y,v[2].y})));
    int right=std::min(clip.mX+clip.mWidth,(int)std::ceil(std::max({v[0].x,v[1].x,v[2].x})));
    int bottom=std::min(clip.mY+clip.mHeight,(int)std::ceil(std::max({v[0].y,v[1].y,v[2].y})));
    uint32_t *bits=dest->GetBits();
    TexturePixels textureBits;
    uint32_t textureWidth=texture->mWidth,textureHeight=texture->mHeight;
    if(H1ImageCachePrepareStorageRGBPages(texture,&textureBits.pages,&textureWidth,&textureHeight)||H1ImageCachePrepareStorageTexture(texture,&textureBits.bits,&textureWidth,&textureHeight))linear=false;
    else if(!H1ImageCachePrepareTexturePages(texture,&textureBits.pages))textureBits.bits=texture->GetBits();
    if(whiteRepeat&&!textureBits.pages&&textureWidth==128&&textureHeight==64&&
       WhiteRepeatTriangle(dest,textureBits.bits,v,area,left,top,right,bottom,color,mode,linear))return;
    ++pvzh1_white_repeat_diagnostic[2];
    for (int y=top;y<bottom;++y) for (int x=left;x<right;++x) {
        float w0=Edge(v[1],v[2],x+0.5f,y+0.5f);
        float w1=Edge(v[2],v[0],x+0.5f,y+0.5f);
        float w2=Edge(v[0],v[1],x+0.5f,y+0.5f);
        if (w0<0||w1<0||w2<0 || (w0==0&&!InclusiveEdge(v[1],v[2])) ||
            (w1==0&&!InclusiveEdge(v[2],v[0])) || (w2==0&&!InclusiveEdge(v[0],v[1]))) continue;
        w0/=area;w1/=area;w2/=area;
        uint32_t p=SampleRepeat(textureBits,textureWidth,textureHeight,v[0].u*w0+v[1].u*w1+v[2].u*w2,
                                        v[0].v*w0+v[1].v*w1+v[2].v*w2,linear);
        uint32_t out=0;
        for (int shift=0;shift<32;shift+=8) {
            uint32_t c0=v[0].color?v[0].color:0xffffffffu;
            uint32_t c1=v[1].color?v[1].color:0xffffffffu;
            uint32_t c2=v[2].color?v[2].color:0xffffffffu;
            float vc=((c0>>shift)&255)*w0+((c1>>shift)&255)*w1+((c2>>shift)&255)*w2;
            unsigned gc=shift==24?color.mAlpha:shift==16?color.mRed:shift==8?color.mGreen:color.mBlue;
            unsigned channel=(unsigned)(((p>>shift)&255)*vc*gc/(255.0f*255.0f)+0.5f);
            out|=std::min(255u,channel)<<shift;
        }
        bits[y*dest->mWidth+x]=Composite(bits[y*dest->mWidth+x],out,mode);
    }
    dest->BitsChanged();
}

void H1DrawTriangles(MemoryImage *dest,Image *image,const TriVertex vertices[][3],int count,
                     const Rect& requestedClip,const Color& color,int mode,float tx,float ty,bool linear)
{
    h1AdditiveNormalDraw=pvzh1_additive_normal_draw;h1AdditiveBeginFrame=pvzh1_additive_begin_frame;
    bool directMode=mode==Graphics::DRAWMODE_ADDITIVE;
#ifdef PVZ_HOST_RUNNER
    directMode=directMode&&directAdditiveEnabled;
#endif
    struct AdditiveModeScope {uint32_t previous=pvzh1_direct_additive;AdditiveModeScope(bool enabled){pvzh1_direct_additive=enabled;}~AdditiveModeScope(){pvzh1_direct_additive=previous;}} directModeScope(directMode);
    Rect clip=Bounds(dest,requestedClip);
    if (clip.mWidth<=0||clip.mHeight<=0||count<=0) return;
    MemoryImage *texture=dynamic_cast<MemoryImage*>(image);
    H1ImageCacheTouch(dest);H1ImageCacheTouch(texture);
    if (image&&(!texture||texture->mWidth<=0||texture->mHeight<=0)) return;
    if (image) image->mDrawn=true;
    bool whiteRepeat=texture&&(texture->mRenderFlags&RenderImageFlag_Repeat)&&
        (texture->mRenderFlags&H1WhiteAlphaRepeatFlag)&&texture->mWidth==128&&texture->mHeight==64;
#ifdef PVZ_HOST_RUNNER
    whiteRepeat=whiteRepeat&&whiteRepeatEnabled;
#endif
    for (int i=0;i<count;++i) {
        TriVertex v[3]={vertices[i][0],vertices[i][1],vertices[i][2]};
        for (int j=0;j<3;++j) {v[j].x+=tx;v[j].y+=ty;}
        int left=0,top=0,right=dest->mWidth,bottom=dest->mHeight;
        const bool finite=std::isfinite(v[0].x)&&std::isfinite(v[0].y)&&std::isfinite(v[1].x)&&std::isfinite(v[1].y)&&std::isfinite(v[2].x)&&std::isfinite(v[2].y);
        if(finite&&dest==h1DirtyCanvas.owner&&h1DirtyCanvas.rooted){
            // Clamp floats before converting to int; include an extra pixel
            // around the original integer-center triangle rasterization.
            left=(int)std::floor(std::max(0.f,std::min((float)dest->mWidth,std::min({v[0].x,v[1].x,v[2].x}))));
            top=(int)std::floor(std::max(0.f,std::min((float)dest->mHeight,std::min({v[0].y,v[1].y,v[2].y}))));
            right=(int)std::ceil(std::max(0.f,std::min((float)dest->mWidth,std::max({v[0].x,v[1].x,v[2].x}))))+1;
            bottom=(int)std::ceil(std::max(0.f,std::min((float)dest->mHeight,std::max({v[0].y,v[1].y,v[2].y}))))+1;
        }
        H1CanvasKnownDraw canvas(dest,left,top,right-left,bottom-top);
        if (texture&&(texture->mRenderFlags&RenderImageFlag_Repeat)) {
            RepeatTriangle(dest,texture,v,clip,color,mode,linear,whiteRepeat);
        } else if (mode==Graphics::DRAWMODE_NORMAL) {
            OriginalTriangle(dest,texture,v,clip,color,mode,linear);
        } else {
            int left=(int)std::floor(std::min({v[0].x,v[1].x,v[2].x}));
            int top=(int)std::floor(std::min({v[0].y,v[1].y,v[2].y}));
            int right=(int)std::ceil(std::max({v[0].x,v[1].x,v[2].x}))+1;
            int bottom=(int)std::ceil(std::max({v[0].y,v[1].y,v[2].y}))+1;
            Rect box=Rect(left,top,right-left,bottom-top).Intersection(clip);
            if (box.mWidth<=0||box.mHeight<=0) continue;
            bool direct=true;
#ifdef PVZ_HOST_RUNNER
            direct=directAdditiveEnabled;
#endif
            if(direct){
                // The original 8888 shader still computes the exact sample over
                // zero, then commits that scalar sample additively. Use the old
                // box origin/clip so raster coordinates and rounding stay exact.
                for(int j=0;j<3;++j){v[j].x-=box.mX;v[j].y-=box.mY;}
                struct Mode {uint32_t previous=pvzh1_direct_additive;Mode(){pvzh1_direct_additive=1;}~Mode(){pvzh1_direct_additive=previous;}} additive;
                const Rect local(0,0,box.mWidth,box.mHeight);
                auto* target=dest->GetBits()+size_t(box.mY)*dest->mWidth+box.mX;
                if(texture){TriVertex a[1][3]={{v[0],v[1],v[2]}};
                    dest->MemoryImage::BltTrianglesTexHelper(texture,a,1,local,color,Graphics::DRAWMODE_NORMAL,target,dest->mWidth*4,0x8888,0,0,linear);
                }else{
                    SWHelper::XYZStruct a[3];for(int j=0;j<3;++j){a[j].mX=v[j].x;a[j].mY=v[j].y;a[j].mU=a[j].mV=0;a[j].mDiffuse=v[j].color?v[j].color:0xffffffffu;}
                    SWHelper::SWDrawShape(a,3,nullptr,color,Graphics::DRAWMODE_NORMAL,local,target,dest->mWidth*4,0x8888,linear,true);
                }
                dest->BitsChanged();++pvzh1_additive_diagnostic[0];H1TracePixel(dest,image,"direct-additive-triangle");continue;
            }
            ++pvzh1_additive_diagnostic[1];pvzh1_additive_diagnostic[2]=std::max(uint32_t(pvzh1_additive_diagnostic[2]),uint32_t(box.mWidth*box.mHeight*4+4));
            MemoryImage scratch(dest->mApp);
            scratch.Create(box.mWidth,box.mHeight);
            for (int j=0;j<3;++j) {v[j].x-=box.mX;v[j].y-=box.mY;}
            OriginalTriangle(&scratch,texture,v,Rect(0,0,box.mWidth,box.mHeight),color,Graphics::DRAWMODE_NORMAL,linear);
            /* SWTri ignores drawMode; compositing each individual triangle
             * additively preserves overlapping particles and mixed batches. */
            uint32_t *source=scratch.GetBits(),*target=dest->GetBits();
            for (int y=0;y<box.mHeight;++y) for (int x=0;x<box.mWidth;++x) {
                int at=(y+box.mY)*dest->mWidth+x+box.mX;
                target[at]=Composite(target[at],source[y*box.mWidth+x],mode);
            }
            dest->BitsChanged();
        }
        H1TracePixel(dest,image,"triangle");
    }
}

void H1DrawTransformed(MemoryImage *dest,Image *image,const Rect& sourceRect,
                       const Rect& clip,const Color& color,int mode,
                       const SexyMatrix3& matrix,bool linear,float x,float y,bool center)
{
    if (!image||image->mWidth<=0||image->mHeight<=0||sourceRect.mWidth<=0||sourceRect.mHeight<=0) return;
    const H1ImageReplayContext imageReplay{dest,image,sourceRect,clip,color,mode,matrix,linear,x,y,center};
    if(H1ImageReplayTryDraw(imageReplay))return;
    H1TextReplayCapture imageCapture(imageReplay);
    struct CaptureGuard {H1RenderPlanToken token{};~CaptureGuard(){H1RenderPlanAbort(token);}} plan;
    if(H1RenderPlanDrawOrBegin(dest,image,sourceRect,clip,color,mode,matrix,linear,x,y,center,plan.token)){
        H1ImageCacheTouch(dest);H1ImageCacheTouch(dynamic_cast<MemoryImage*>(image));image->mDrawn=true;
        // The original two-triangle wrapper marks the target twice. Keep that
        // counter contract while replaying each image at its original position.
        dest->BitsChanged();H1TracePixel(dest,image,"triangle");
        dest->BitsChanged();H1TracePixel(dest,image,"triangle");return;
    }
    float x0=center?-sourceRect.mWidth/2.0f:0;
    float y0=center?-sourceRect.mHeight/2.0f:0;
    float x1=x0+sourceRect.mWidth,y1=y0+sourceRect.mHeight;
    float u0=(float)sourceRect.mX/image->mWidth,v0=(float)sourceRect.mY/image->mHeight;
    float u1=(float)(sourceRect.mX+sourceRect.mWidth)/image->mWidth;
    float v1=(float)(sourceRect.mY+sourceRect.mHeight)/image->mHeight;
    TriVertex v[2][3]={{{x0,y0,u0,v0},{x1,y0,u1,v0},{x0,y1,u0,v1}},
                       {{x1,y0,u1,v0},{x1,y1,u1,v1},{x0,y1,u0,v1}}};
    bool fast=true;
#ifdef PVZ_HOST_RUNNER
    fast=pvzh1_geometry_fast_enabled;
#endif
    for(unsigned row=0;row<2;++row)for(unsigned col=0;col<3;++col)if(!std::isfinite(matrix.m[row][col]))fast=false;
    if(fast){
        // The original wrapper calculates six 3D products for four 2D corners.
        // Keep each x/y sum in its original order, and reuse only identical
        // vertices. The third row and multiplication of translation by 1 do
        // not contribute to either raster coordinate.
        const float cx[4]={x0,x1,x0,x1},cy[4]={y0,y0,y1,y1};
        float px[4],py[4];
        for(unsigned i=0;i<4;++i){
            px[i]=(matrix.m00*cx[i]+matrix.m01*cy[i]+matrix.m02)+x-0.5f;
            py[i]=(matrix.m10*cx[i]+matrix.m11*cy[i]+matrix.m12)+y-0.5f;
        }
        const unsigned corners[2][3]={{0,1,2},{1,3,2}};
        for(unsigned i=0;i<2;++i)for(unsigned j=0;j<3;++j){v[i][j].x=px[corners[i][j]];v[i][j].y=py[corners[i][j]];}
    }else for (int i=0;i<2;++i) for (int j=0;j<3;++j) {
        SexyVector3 p=matrix*SexyVector3(v[i][j].x,v[i][j].y,1);
        /* SWTri samples on integer pixel coordinates; GL samples at centers.
         * Match MemoryImage::BltMatrixHelper's half-pixel convention. */
        v[i][j].x=p.x+x-0.5f;v[i][j].y=p.y+y-0.5f;
    }
    H1DrawTriangles(dest,image,v,2,clip,color,mode,0,0,linear);
    H1RenderPlanFinish(plan.token);plan.token.value=0;imageCapture.Commit();
}
}
