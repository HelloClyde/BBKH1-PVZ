/* A small screen backing store for the unmodified 800x600 Lawn coordinate
 * system. SPDX-License-Identifier: LGPL-3.0-or-later */
#include "PhysicalScreen.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "SoftwareRaster.h"
#include "PhysicalMatrixScale.h"
#include "DirtyCanvas.h"
#include <algorithm>

/* active, physical W/H, logical W/H, permanent raw fallback count, presents,
 * logical drawing calls routed to the physical target. No diagnostics allocate. */
extern "C" {volatile uint32_t pvzh1_physical_screen_diagnostic[8];}

namespace Sexy {
namespace {
int64_t CeilDivide(int64_t a,int64_t b){return a>=0?(a+b-1)/b:a/b;}
int CenterBoundary(int value,int logical,int physical){
    return (int)CeilDivide((int64_t)value*physical*2-logical,(int64_t)logical*2);
}
int LogicalCenter(int pixel,int logical,int physical){
    return (int)(((int64_t)pixel*2+1)*logical/((int64_t)physical*2));
}
}
H1PhysicalScreen::H1PhysicalScreen(GLInterface *device,int lw,int lh,int width,int height)
    :GLImage(device),physical(device->mApp)
{
    mWidth=lw;mHeight=lh;SetImageMode(false,false);
    physical.Create(width,height);physical.SetImageMode(false,false);physical.GetBits();
    if(width==(int)H1ScreenWidth&&height==(int)H1ScreenHeight)h1DirtyCanvas.Bind(&physical,physical.mBits.get());
    pvzh1_physical_screen_diagnostic[0]=1;
    pvzh1_physical_screen_diagnostic[1]=width;pvzh1_physical_screen_diagnostic[2]=height;
    pvzh1_physical_screen_diagnostic[3]=lw;pvzh1_physical_screen_diagnostic[4]=lh;
}
H1PhysicalScreen::~H1PhysicalScreen(){if(h1DirtyCanvas.owner==&physical)h1DirtyCanvas.Reset();}
uint32_t *H1PhysicalScreen::GetBits()
{
    if(!fullCanvas){
        /* A caller asking for raw logical pixels may retain that pointer.
         * Expand once and permanently retain ordinary full-canvas behavior. */
        fullCanvas=true;
        pvzh1_physical_screen_diagnostic[0]=0;
        pvzh1_physical_screen_diagnostic[5]=pvzh1_physical_screen_diagnostic[5]+1;
        uint32_t *out=MemoryImage::GetBits();
        const uint32_t *input=physical.GetBits();
        for(int y=0;y<mHeight;++y){
            const uint32_t *row=input+(std::size_t)y*physical.mHeight/mHeight*physical.mWidth;
            for(int x=0;x<mWidth;++x)out[(std::size_t)y*mWidth+x]=row[(std::size_t)x*physical.mWidth/mWidth];
        }
    }
    return MemoryImage::GetBits();
}
void H1PhysicalScreen::BitsChanged(){if(!fullCanvas)GetBits();MemoryImage::BitsChanged();}
void H1PhysicalScreen::NoteDraw(){
    ++mBitsChangedCount;
    pvzh1_physical_screen_diagnostic[7]=pvzh1_physical_screen_diagnostic[7]+1;
}
Rect H1PhysicalScreen::MapRect(const Rect& logical)const
{
    if(fullCanvas)return logical.Intersection(Rect(0,0,mWidth,mHeight));
    const Rect r=logical.Intersection(Rect(0,0,mWidth,mHeight));
    if(r.mX==0&&r.mY==0&&r.mWidth==mWidth&&r.mHeight==mHeight)return Rect(0,0,physical.mWidth,physical.mHeight);
    const int left=CenterBoundary(r.mX,mWidth,physical.mWidth),top=CenterBoundary(r.mY,mHeight,physical.mHeight);
    const int right=CenterBoundary(r.mX+r.mWidth,mWidth,physical.mWidth),bottom=CenterBoundary(r.mY+r.mHeight,mHeight,physical.mHeight);
    return Rect(left,top,std::max(0,right-left),std::max(0,bottom-top));
}
void H1PhysicalScreen::Clear(){if(fullCanvas){MemoryImage::Clear();return;}physical.Clear();NoteDraw();}
void H1PhysicalScreen::ClearRect(const Rect& rect){
    if(fullCanvas){MemoryImage::ClearRect(rect);return;}
    const Rect r=MapRect(rect);if(r.mWidth>0&&r.mHeight>0)physical.ClearRect(r);NoteDraw();
}
void H1PhysicalScreen::FillScanLines(Span *spans,int count,const Color& color,int mode)
{
    if(fullCanvas){MemoryImage::FillScanLines(spans,count,color,mode);return;}
    /* Select exactly the logical row at each physical pixel center. Adjacent
     * 1px logical spans can never blend repeatedly into the same output row. */
    for(int y=0;y<physical.mHeight;++y){
        const int ly=LogicalCenter(y,mHeight,physical.mHeight);
        for(int i=0;i<count;++i)if(spans[i].mY==ly){
            Rect r=MapRect(Rect(spans[i].mX,ly,spans[i].mWidth,1));
            if(r.mWidth>0)H1FillRect(&physical,Rect(r.mX,y,r.mWidth,1),color,mode);
        }
    }
    NoteDraw();
}
void H1PhysicalScreen::FillScanLinesWithCoverage(Span *spans,int count,const Color& color,int mode,
    const uint8_t *coverage,int x,int y,int width,int height)
{
    if(fullCanvas){H1Coverage(this,spans,count,color,mode,coverage,x,y,width,height);return;}
    H1CoverageScaled(&physical,spans,count,color,mode,coverage,x,y,width,height,mWidth,mHeight);
    NoteDraw();
}
void H1PhysicalScreen::BltTrianglesTex(Image *texture,const TriVertex vertices[][3],int count,const Rect& clip,
    const Color& color,int mode,float tx,float ty,bool linear)
{H1ScreenDrawTriangles(this,texture,vertices,count,clip,color,mode,tx,ty,linear);}
H1PhysicalScreen *H1GetPhysicalScreen(GLImage *screen){return dynamic_cast<H1PhysicalScreen*>(screen);}
MemoryImage *H1ScreenTarget(GLImage *screen){auto *p=H1GetPhysicalScreen(screen);return p?p->Target():screen;}
Rect H1ScreenMapRect(GLImage *screen,const Rect& rect){auto *p=H1GetPhysicalScreen(screen);return p?p->MapRect(rect):rect;}
void H1ScreenNoteDraw(GLImage *screen){auto *p=H1GetPhysicalScreen(screen);if(p&&p->Active())p->NoteDraw();}
void H1ScreenScaleMatrix(GLImage *screen,SexyMatrix3& matrix)
{
    auto *p=H1GetPhysicalScreen(screen);if(!p||!p->Active())return;
    const float sx=p->ScaleX(),sy=p->ScaleY();
    if(H1TryScaleAffineMatrix(&matrix.m[0][0],sx,sy))return;
    SexyTransform2D scale;scale.Scale(sx,sy);matrix=scale*matrix;
}
void H1ScreenDrawTriangles(GLImage *screen,Image *texture,const TriVertex vertices[][3],int count,
    const Rect& clip,const Color& color,int mode,float tx,float ty,bool linear)
{
    auto *p=H1GetPhysicalScreen(screen);
    if(!p||!p->Active()){H1DrawTriangles(screen,texture,vertices,count,clip,color,mode,tx,ty,linear);return;}
    const Rect physicalClip=p->MapRect(clip);MemoryImage *target=p->Target();
    for(int i=0;i<count;++i){
        TriVertex triangle[1][3]={{vertices[i][0],vertices[i][1],vertices[i][2]}};
        for(int j=0;j<3;++j){triangle[0][j].x=(triangle[0][j].x+tx)*p->ScaleX();triangle[0][j].y=(triangle[0][j].y+ty)*p->ScaleY();}
        H1DrawTriangles(target,texture,triangle,1,physicalClip,color,mode,0,0,linear);
    }
    p->NoteDraw();
}
}
