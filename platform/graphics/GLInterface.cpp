/* H1 software replacement for the PvZ-Portable GL device. No OpenGL/GLSL/SDL
 * calls are made. The upstream public class and complete game coordinates are
 * retained so Lawn and all animation/effect batching remain the original code.
 * SPDX-License-Identifier: LGPL-3.0-or-later AND LicenseRef-PopCap */
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/SWTri.h"
#include "SexyAppBase.h"
#include "SoftwareDisplay.h"
#include "SoftwareRaster.h"
#include "MemoryImageCache.h"
#include "PhysicalScreen.h"
#include "DirtyCanvas.h"
#include <cmath>

using namespace Sexy;
bool gDesktopGLFallback=false;
extern "C" volatile uint32_t pvzh1_physical_screen_diagnostic[8];
extern "C" {volatile uint32_t pvzh1_dirty_canvas_diagnostic[8];}

namespace {
PvzH1Present presentCallback=nullptr;
void *presentContext=nullptr;
PvzH1FrameComplete completeCallback=nullptr;
void *completeContext=nullptr;
int displayWidth=480,displayHeight=272;

Rect ScreenClip(GLInterface *device,const Rect *clip)
{
    Rect full(0,0,device->mWidth,device->mHeight);
    return clip?full.Intersection(*clip):full;
}
SexyVector2 TransformPoint(GLInterface *device,float x,float y)
{
    SexyVector2 p(x,y);
    if(!device->mTransformStack.empty())p=device->mTransformStack.back()*p;
    return p;
}
bool ClipLine(double& x1,double& y1,double& x2,double& y2,int width,int height)
{
    double low=0,high=1,dx=x2-x1,dy=y2-y1;
    double p[4]={-dx,dx,-dy,dy};
    double q[4]={x1,width-1-x1,y1,height-1-y1};
    for(int i=0;i<4;++i){
        if(p[i]==0){if(q[i]<0)return false;continue;}
        double ratio=q[i]/p[i];
        if(p[i]<0){if(ratio>high)return false;low=std::max(low,ratio);}
        else{if(ratio<low)return false;high=std::min(high,ratio);}
    }
    double oldX=x1,oldY=y1;
    x1=oldX+low*dx;y1=oldY+low*dy;x2=oldX+high*dx;y2=oldY+high*dy;
    return true;
}
}

extern "C" void pvzh1_graphics_set_present(PvzH1Present callback,void *context)
{ presentCallback=callback;presentContext=context; }
extern "C" void pvzh1_graphics_set_display_size(int width,int height)
{ if(width>0&&height>0){displayWidth=width;displayHeight=height;} }
extern "C" void pvzh1_graphics_set_frame_complete(PvzH1FrameComplete callback,void *context)
{ completeCallback=callback;completeContext=context; }

/* No TextureData is created by this backend. Constructors/destructor remain
 * available because existing MemoryImage cleanup uses the public ABI. */
TextureData::TextureData():mWidth(0),mHeight(0),mTexVecWidth(0),mTexVecHeight(0),
    mTexPieceWidth(0),mTexPieceHeight(0),mBitsChangedCount(0),mTexMemSize(0),
    mMaxTotalU(0),mMaxTotalV(0),mPixelFormat(PixelFormat_Unknown),mImageFlags(0) {}
TextureData::~TextureData()=default;
void TextureData::ReleaseTextures(){mTextures.clear();mTexMemSize=0;}

GLInterface::GLInterface(SexyAppBase *app):mApp(app)
{
    mWidth=app?app->mWidth:800;mHeight=app?app->mHeight:600;
    mDisplayWidth=displayWidth;mDisplayHeight=displayHeight;
    mRefreshRate=60;mMillisecondsPerFrame=1000/mRefreshRate;
    mNextCursorX=mNextCursorY=mCursorX=mCursorY=0;
    UpdateViewport();
}
GLInterface::~GLInterface()
{
    mScreenImage.reset();
    for(auto *image:mGLImageSet)image->mGLInterface=nullptr;
    for(auto *image:mImageSet){delete image->mRenderData;image->mRenderData=nullptr;}
}
void GLInterface::SetDrawMode(int) {}
void GLInterface::AddGLImage(GLImage *image){mGLImageSet.insert(image);}
void GLInterface::RemoveGLImage(GLImage *image){mGLImageSet.erase(image);}
void GLInterface::Remove3DData(MemoryImage *image)
{
    delete image->mRenderData;image->mRenderData=nullptr;mImageSet.erase(image);
}
GLImage *GLInterface::GetScreenImage(){return mScreenImage.get();}
void GLInterface::UpdateViewport()
{
    mDisplayWidth=displayWidth;mDisplayHeight=displayHeight;
    int width=mDisplayWidth,height=mDisplayHeight,x=0,y=0;
    if(mWidth<=0||mHeight<=0){mPresentationRect=Rect(0,0,width,height);return;}
    if(width*mHeight>height*mWidth){width=height*mWidth/mHeight;x=(mDisplayWidth-width)/2;}
    else if(width*mHeight<height*mWidth){height=width*mHeight/mWidth;y=(mDisplayHeight-height)/2;}
    mPresentationRect=Rect(x,y,width,height);
}
int GLInterface::Init(bool)
{
    if(mWidth<=0||mHeight<=0)return 0;
    static bool rasterInitialized=false;
    if(!rasterInitialized){SWTri_AddAllDrawTriFuncs();rasterInitialized=true;}
    mRGBBits=32;mRedBits=mGreenBits=mBlueBits=8;
    mRedShift=16;mGreenShift=8;mBlueShift=0;
    mRedMask=0x00ff0000u;mGreenMask=0x0000ff00u;mBlueMask=0x000000ffu;
    UpdateViewport();SetVideoOnlyDraw(false);return 1;
}
bool GLInterface::Redraw(Rect*)
{
    if(!mScreenImage)return false;
    MemoryImage *target=H1ScreenTarget(mScreenImage.get());
    H1CanvasProtectedPixels presentPixels(target);
    int result=presentCallback?presentCallback(target->GetBits(),target->mWidth,target->mHeight,presentContext):1;
    if(H1GetPhysicalScreen(mScreenImage.get()))pvzh1_physical_screen_diagnostic[6]=pvzh1_physical_screen_diagnostic[6]+1;
    pvzh1_dirty_canvas_diagnostic[0]=1;
    pvzh1_dirty_canvas_diagnostic[1]=h1DirtyCanvas.roots;
    pvzh1_dirty_canvas_diagnostic[2]=h1DirtyCanvas.limitedRoots;
    pvzh1_dirty_canvas_diagnostic[3]=h1DirtyCanvas.fullClears;
    pvzh1_dirty_canvas_diagnostic[4]=h1DirtyCanvas.backgroundWritten;
    pvzh1_dirty_canvas_diagnostic[5]=h1DirtyCanvas.backgroundSkipped;
    pvzh1_dirty_canvas_diagnostic[6]=h1DirtyCanvas.pending;
    pvzh1_dirty_canvas_diagnostic[7]=h1DirtyCanvas.rooted;
    /* The original GL Flush swaps, then clears the back buffer. Widget drawing
     * assumes that lifecycle; retaining pixels leaks earlier pages into small
     * uncovered regions (notably the selector's upper border). */
    if(auto* physical=H1GetPhysicalScreen(mScreenImage.get());physical&&physical->Active()&&h1DirtyCanvas.owner==target){
        h1DirtyCanvas.Begin(target);
        target->BitsChanged();
    }else target->MemoryImage::FillRect(Rect(0,0,target->mWidth,target->mHeight),Color::Black,Graphics::DRAWMODE_NORMAL);
    H1ImageCacheEndFrame();
    if(completeCallback)completeCallback(completeContext);
    return result!=0;
}
void GLInterface::SetVideoOnlyDraw(bool)
{
#if defined(PVZH1_PHYSICAL_SCREEN)
    if(H1ImageCacheReducedPack()){
        mScreenImage=std::make_unique<H1PhysicalScreen>(this,mWidth,mHeight,H1ScreenWidth,H1ScreenHeight);
        return;
    }
#endif
    auto image=std::make_unique<GLImage>(this);
    image->Create(mWidth,mHeight);image->SetImageMode(false,false);
    mScreenImage=std::move(image);
    mScreenImage->GetBits();
}
void GLInterface::SetCursorPos(int x,int y){mNextCursorX=x;mNextCursorY=y;}
bool GLInterface::PreDraw(){return mScreenImage!=nullptr;}
void GLInterface::Flush(){Redraw(nullptr);}
bool GLInterface::CreateImageTexture(MemoryImage *image)
{ return image&&image->mWidth>0&&image->mHeight>0&&image->GetBits(); }
bool GLInterface::RecoverBits(MemoryImage*){return false;}
void GLInterface::PushTransform(const SexyMatrix3& transform,bool concatenate)
{
    mTransformStack.push_back(mTransformStack.empty()||!concatenate?transform:transform*mTransformStack.back());
}
void GLInterface::PopTransform(){if(!mTransformStack.empty())mTransformStack.pop_back();}

void GLInterface::Blt(Image *image,float x,float y,const Rect& src,const Color& color,int mode,bool linear)
{
    if(!PreDraw()||!image)return;
    if(auto *physical=H1GetPhysicalScreen(mScreenImage.get());physical&&physical->Active()){
        BltClipF(image,x,y,src,nullptr,color,mode,linear);return;
    }
    const bool reduced=H1ImageCacheIsReduced(dynamic_cast<MemoryImage*>(image));
    if(mTransformStack.empty()&&x==std::floor(x)&&y==std::floor(y)){
        Rect valid=src.Intersection(Rect(0,0,image->mWidth,image->mHeight));
        int ix=(int)x+valid.mX-src.mX,iy=(int)y+valid.mY-src.mY;
        Rect dest=Rect(ix,iy,valid.mWidth,valid.mHeight).Intersection(ScreenClip(this,nullptr));
        if(dest.mWidth<=0||dest.mHeight<=0)return;
        valid.mX+=dest.mX-ix;valid.mY+=dest.mY-iy;valid.mWidth=dest.mWidth;valid.mHeight=dest.mHeight;
        auto source=dynamic_cast<MemoryImage*>(image);
        bool copied=false;
        if(color==Color::White&&mode==Graphics::DRAWMODE_NORMAL)
            copied=reduced?H1ImageCacheBltStorageOpaqueRGB(source,mScreenImage.get(),dest.mX,dest.mY,valid,src):
                H1ImageCacheBltOpaqueRGB(source,mScreenImage.get(),dest.mX,dest.mY,valid);
        if(!copied){
            if(reduced)BltClipF(image,x,y,src,nullptr,color,mode,false);
            else mScreenImage->MemoryImage::Blt(image,dest.mX,dest.mY,valid,color,mode,linear);
        }
        H1TracePixel(mScreenImage.get(),image,"blit");
    }else BltClipF(image,x,y,src,nullptr,color,mode,linear);
}
void GLInterface::BltClipF(Image *image,float x,float y,const Rect& src,const Rect *clip,
                          const Color& color,int mode,bool linear)
{
    SexyTransform2D transform;transform.Translate(x,y);
    BltTransformed(image,clip,color,mode,src,transform,linear);
}
void GLInterface::BltMirror(Image *image,float x,float y,const Rect& src,const Color& color,int mode,bool linear)
{
    SexyTransform2D transform;transform.Scale(-1,1);transform.Translate(x+src.mWidth,y);
    BltTransformed(image,nullptr,color,mode,src,transform,linear);
}
void GLInterface::StretchBlt(Image *image,const Rect& dest,const Rect& src,const Rect *clip,
                            const Color& color,int mode,bool fast,bool mirror)
{
    if(src.mWidth<=0||src.mHeight<=0||dest.mWidth<=0||dest.mHeight<=0)return;
    SexyTransform2D transform;
    transform.Scale((mirror?-1.0f:1.0f)*dest.mWidth/src.mWidth,(float)dest.mHeight/src.mHeight);
    transform.Translate((float)(dest.mX+(mirror?dest.mWidth:0)),(float)dest.mY);
    BltTransformed(image,clip,color,mode,src,transform,!fast);
}
void GLInterface::BltRotated(Image *image,float x,float y,const Rect *clip,const Color& color,
                            int mode,double rotation,float centerX,float centerY,const Rect& src)
{
    SexyTransform2D transform;transform.Translate(-centerX,-centerY);
    transform.RotateRad((float)rotation);transform.Translate(x+centerX,y+centerY);
    BltTransformed(image,clip,color,mode,src,transform,true);
}
void GLInterface::BltTransformed(Image *image,const Rect *clip,const Color& color,int mode,
                                const Rect& src,const SexyMatrix3& matrix,bool linear,float x,float y,bool center)
{
    if(!PreDraw())return;
    SexyTransform2D transform(matrix);transform.Translate(x,y);
    if(!mTransformStack.empty())transform=mTransformStack.back()*transform;
    H1ScreenScaleMatrix(mScreenImage.get(),transform);
    H1DrawTransformed(H1ScreenTarget(mScreenImage.get()),image,src,H1ScreenMapRect(mScreenImage.get(),ScreenClip(this,clip)),color,mode,transform,linear,0,0,center);
    H1ScreenNoteDraw(mScreenImage.get());
}
void GLInterface::DrawLine(double x1,double y1,double x2,double y2,const Color& color,int mode)
{
    if(!PreDraw())return;
    auto p1=TransformPoint(this,(float)x1,(float)y1),p2=TransformPoint(this,(float)x2,(float)y2);
    x1=p1.x;y1=p1.y;x2=p2.x;y2=p2.y;
    if(ClipLine(x1,y1,x2,y2,mWidth,mHeight)){
        if(auto *physical=H1GetPhysicalScreen(mScreenImage.get());physical&&physical->Active()){
            x1*=physical->ScaleX();x2*=physical->ScaleX();y1*=physical->ScaleY();y2*=physical->ScaleY();
        }
        H1ScreenTarget(mScreenImage.get())->MemoryImage::DrawLine(x1,y1,x2,y2,color,mode);
        H1ScreenNoteDraw(mScreenImage.get());
    }
}
void GLInterface::FillRect(const Rect& rect,const Color& color,int mode)
{
    if(!PreDraw())return;
    if(mTransformStack.empty()){
        H1FillRect(H1ScreenTarget(mScreenImage.get()),H1ScreenMapRect(mScreenImage.get(),rect),color,mode);
        H1ScreenNoteDraw(mScreenImage.get());return;
    }
    Point vertices[4]={{rect.mX,rect.mY},{rect.mX+rect.mWidth,rect.mY},
                       {rect.mX+rect.mWidth,rect.mY+rect.mHeight},{rect.mX,rect.mY+rect.mHeight}};
    FillPoly(vertices,4,nullptr,color,mode,0,0);
}
void GLInterface::DrawTriangle(const TriVertex& p1,const TriVertex& p2,const TriVertex& p3,const Color& color,int mode)
{
    TriVertex vertices[1][3]={{p1,p2,p3}};
    DrawTrianglesTex(vertices,1,color,mode,nullptr,0,0,false);
}
void GLInterface::DrawTriangleTex(const TriVertex& p1,const TriVertex& p2,const TriVertex& p3,
                                 const Color& color,int mode,Image *texture,bool linear)
{
    TriVertex vertices[1][3]={{p1,p2,p3}};
    DrawTrianglesTex(vertices,1,color,mode,texture,0,0,linear);
}
void GLInterface::DrawTrianglesTex(const TriVertex vertices[][3],int count,const Color& color,
                                  int mode,Image *texture,float tx,float ty,bool linear)
{
    if(!PreDraw())return;
    if(mTransformStack.empty())H1ScreenDrawTriangles(mScreenImage.get(),texture,vertices,count,ScreenClip(this,nullptr),color,mode,tx,ty,linear);
    else for(int i=0;i<count;++i){
        TriVertex triangle[1][3]={{vertices[i][0],vertices[i][1],vertices[i][2]}};
        for(int j=0;j<3;++j){auto p=TransformPoint(this,triangle[0][j].x+tx,triangle[0][j].y+ty);triangle[0][j].x=p.x;triangle[0][j].y=p.y;}
        H1ScreenDrawTriangles(mScreenImage.get(),texture,triangle,1,ScreenClip(this,nullptr),color,mode,0,0,linear);
    }
}
void GLInterface::DrawTrianglesTexMixed(const TriVertex vertices[][3],int count,const Color& color,
                                       Image *texture,const uint8_t *modes,float tx,float ty,bool linear)
{
    for(int i=0;i<count;++i)DrawTrianglesTex(vertices+i,1,color,modes?modes[i]:Graphics::DRAWMODE_NORMAL,texture,tx,ty,linear);
}
void GLInterface::DrawTrianglesTexStrip(const TriVertex vertices[],int count,const Color& color,
                                       int mode,Image *texture,float tx,float ty,bool linear)
{
    for(int i=0;i<count;++i){
        TriVertex triangle[1][3]={{vertices[i],vertices[i+1],vertices[i+2]}};
        if(i&1)std::swap(triangle[0][0],triangle[0][1]);
        DrawTrianglesTex(triangle,1,color,mode,texture,tx,ty,linear);
    }
}
void GLInterface::FillPoly(const Point vertices[],int count,const Rect *clip,const Color& color,int mode,int tx,int ty)
{
    if(!PreDraw()||count<3)return;
    auto origin=TransformPoint(this,(float)(vertices[0].mX+tx),(float)(vertices[0].mY+ty));
    for(int i=1;i+1<count;++i){
        auto p1=TransformPoint(this,(float)(vertices[i].mX+tx),(float)(vertices[i].mY+ty));
        auto p2=TransformPoint(this,(float)(vertices[i+1].mX+tx),(float)(vertices[i+1].mY+ty));
        TriVertex triangle[1][3]={{{origin.x,origin.y},{p1.x,p1.y},{p2.x,p2.y}}};
        H1ScreenDrawTriangles(mScreenImage.get(),nullptr,triangle,1,ScreenClip(this,clip),color,mode,0,0,false);
    }
}
