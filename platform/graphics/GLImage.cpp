/* H1 software replacement for PvZ-Portable's GLImage.cpp.
 * All Lawn, animation, Graphics and bitmap-font code stays upstream.
 * SPDX-License-Identifier: LGPL-3.0-or-later AND LicenseRef-PopCap */
#include "graphics/GLImage.h"
#include "graphics/GLInterface.h"
#include "SexyAppBase.h"
#include "SoftwareRaster.h"

using namespace Sexy;

static bool IsScreen(const GLImage *image)
{ return image&&image->mGLInterface&&image->mGLInterface->GetScreenImage()==image; }

GLImage::GLImage():MemoryImage(gSexyAppBase)
{
    mGLInterface=gSexyAppBase?gSexyAppBase->mGLInterface.get():nullptr;
    if (mGLInterface)mGLInterface->AddGLImage(this);
}
GLImage::GLImage(GLInterface *interface):MemoryImage(interface?interface->mApp:gSexyAppBase),mGLInterface(interface)
{ if (mGLInterface)mGLInterface->AddGLImage(this); }
GLImage::~GLImage()
{ if (mGLInterface)mGLInterface->RemoveGLImage(this); }
void GLImage::Create(int width,int height)
{ MemoryImage::Create(width,height); }
bool GLImage::Check3D(GLImage *image)
{ return IsScreen(image); }
bool GLImage::Check3D(Image *image)
{ return IsScreen(dynamic_cast<GLImage*>(image)); }
void GLImage::PurgeBits()
{
    /* Software source pixels are the backing store. Deleting them while no GPU
     * texture exists would destroy procedural images and font/atlas contents. */
    mPurgeBits=false;
    CommitBits();
}
bool GLImage::PolyFill3D(const Point*,int,const Rect*,const Color&,int,int,int)
{
    /* Graphics.cpp's original scan converter handles clipped concave/convex
     * polygons and calls our FillRect/coverage methods. */
    return false;
}
void GLImage::FillScanLinesWithCoverage(Span *spans,int count,const Color& color,int mode,
                                      const uint8_t *coverage,int x,int y,int width,int height)
{ H1Coverage(this,spans,count,color,mode,coverage,x,y,width,height); }
void GLImage::FillRect(const Rect& rect,const Color& color,int mode)
{
    if (IsScreen(this))mGLInterface->FillRect(rect,color,mode);
    else H1FillRect(this,rect,color,mode);
}
void GLImage::DrawLine(double x1,double y1,double x2,double y2,const Color& color,int mode)
{
    if (IsScreen(this))mGLInterface->DrawLine(x1,y1,x2,y2,color,mode);
    else MemoryImage::DrawLine(x1,y1,x2,y2,color,mode);
}
void GLImage::DrawLineAA(double x1,double y1,double x2,double y2,const Color& color,int mode)
{
    if (IsScreen(this))mGLInterface->DrawLine(x1,y1,x2,y2,color,mode);
    else MemoryImage::DrawLineAA(x1,y1,x2,y2,color,mode);
}
void GLImage::Blt(Image *image,int x,int y,const Rect& src,const Color& color,int mode,bool linear)
{
    if (IsScreen(this)) {mGLInterface->Blt(image,(float)x,(float)y,src,color,mode,linear);return;}
    if (!image)return;
    Rect valid=src.Intersection(Rect(0,0,image->mWidth,image->mHeight));
    x+=valid.mX-src.mX;y+=valid.mY-src.mY;
    Rect dest=Rect(x,y,valid.mWidth,valid.mHeight).Intersection(Rect(0,0,mWidth,mHeight));
    if (dest.mWidth<=0||dest.mHeight<=0)return;
    valid.mX+=dest.mX-x;valid.mY+=dest.mY-y;valid.mWidth=dest.mWidth;valid.mHeight=dest.mHeight;
    MemoryImage::Blt(image,dest.mX,dest.mY,valid,color,mode,linear);
}
void GLImage::BltF(Image *image,float x,float y,const Rect& src,const Rect& clip,const Color& color,int mode)
{
    if (IsScreen(this)) {mGLInterface->BltClipF(image,x,y,src,&clip,color,mode);return;}
    MemoryImage::BltF(image,x,y,src,clip.Intersection(Rect(0,0,mWidth,mHeight)),color,mode);
}
void GLImage::BltRotated(Image *image,float x,float y,const Rect& src,const Rect& clip,
                        const Color& color,int mode,double rotation,float centerX,float centerY)
{
    if (IsScreen(this)) {mGLInterface->BltRotated(image,x,y,&clip,color,mode,rotation,centerX,centerY,src);return;}
    MemoryImage::BltRotated(image,x,y,src,clip.Intersection(Rect(0,0,mWidth,mHeight)),color,mode,rotation,centerX,centerY);
}
void GLImage::StretchBlt(Image *image,const Rect& dest,const Rect& src,const Rect& clip,
                        const Color& color,int mode,bool fast)
{
    if (IsScreen(this)) {mGLInterface->StretchBlt(image,dest,src,&clip,color,mode,fast,false);return;}
    if (src.mWidth<=0||src.mHeight<=0)return;
    SexyTransform2D transform;
    transform.Scale((float)dest.mWidth/src.mWidth,(float)dest.mHeight/src.mHeight);
    transform.Translate((float)dest.mX,(float)dest.mY);
    /* Original FastStretchBlt has an empty non-white-tint branch and both
     * stretch variants ignore additive mode. Use the original triangle raster
     * instead, retaining colorization and the requested sampling behavior. */
    H1DrawTransformed(this,image,src,clip,color,mode,transform,!fast);
}
void GLImage::BltMatrix(Image *image,float x,float y,const SexyMatrix3& matrix,
                       const Rect& clip,const Color& color,int mode,const Rect& src,bool linear)
{
    if (IsScreen(this)) {mGLInterface->BltTransformed(image,&clip,color,mode,src,matrix,linear,x,y,true);return;}
    H1DrawTransformed(this,image,src,clip,color,mode,matrix,linear,x,y,true);
}
void GLImage::BltTrianglesTex(Image *texture,const TriVertex vertices[][3],int count,const Rect& clip,
                             const Color& color,int mode,float tx,float ty,bool linear)
{
    /* Unlike the original GL backend this respects the supplied clip, including
     * offscreen atlas and filter-effect destinations. */
    H1DrawTriangles(this,texture,vertices,count,clip,color,mode,tx,ty,linear);
}
void GLImage::BltMirror(Image *image,int x,int y,const Rect& src,const Color& color,int mode,bool linear)
{
    if (IsScreen(this)) {mGLInterface->BltMirror(image,(float)x,(float)y,src,color,mode,linear);return;}
    SexyTransform2D transform;
    transform.Scale(-1,1);transform.Translate((float)(x+src.mWidth),(float)y);
    H1DrawTransformed(this,image,src,Rect(0,0,mWidth,mHeight),color,mode,transform,linear);
}
void GLImage::StretchBltMirror(Image *image,const Rect& dest,const Rect& src,const Rect& clip,
                              const Color& color,int mode,bool fast)
{
    if (IsScreen(this)) {mGLInterface->StretchBlt(image,dest,src,&clip,color,mode,fast,true);return;}
    if (src.mWidth<=0||src.mHeight<=0)return;
    SexyTransform2D transform;
    transform.Scale(-(float)dest.mWidth/src.mWidth,(float)dest.mHeight/src.mHeight);
    transform.Translate((float)(dest.mX+dest.mWidth),(float)dest.mY);
    H1DrawTransformed(this,image,src,clip,color,mode,transform,!fast);
}
