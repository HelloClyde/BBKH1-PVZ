#ifndef PVZH1_PHYSICAL_SCREEN_H
#define PVZH1_PHYSICAL_SCREEN_H
#include "graphics/GLImage.h"

namespace Sexy {
/* Only the screen is a logical proxy. Ordinary offscreen images retain their
 * original dimensions, stride and raw-pixel contracts. */
class H1PhysicalScreen final: public GLImage {
    MemoryImage physical;
    bool fullCanvas=false;
    mutable bool clipCached=false;
    mutable Rect clipInput,clipOutput;
    mutable int clipLogicalWidth=0,clipLogicalHeight=0,clipPhysicalWidth=0,clipPhysicalHeight=0;
public:
    H1PhysicalScreen(GLInterface *device,int logicalWidth,int logicalHeight,int width,int height);
    ~H1PhysicalScreen() override;
    uint32_t *GetBits() override;
    void BitsChanged() override;
    void Clear() override;
    void ClearRect(const Rect& rect) override;
    void FillScanLines(Span *spans,int count,const Color& color,int mode) override;
    void FillScanLinesWithCoverage(Span *spans,int count,const Color& color,int mode,
        const uint8_t *coverage,int x,int y,int width,int height) override;
    void BltTrianglesTex(Image *texture,const TriVertex vertices[][3],int count,const Rect& clip,
        const Color& color,int mode,float tx,float ty,bool linear) override;
    MemoryImage *Target(){return fullCanvas?this:&physical;}
    bool Active()const{return !fullCanvas;}
    float ScaleX()const{return fullCanvas?1.0f:(float)physical.mWidth/mWidth;}
    float ScaleY()const{return fullCanvas?1.0f:(float)physical.mHeight/mHeight;}
    Rect MapRect(const Rect& logical)const;
    void NoteDraw();
};
H1PhysicalScreen *H1GetPhysicalScreen(GLImage *screen);
MemoryImage *H1ScreenTarget(GLImage *screen);
Rect H1ScreenMapRect(GLImage *screen,const Rect& logical);
void H1ScreenScaleMatrix(GLImage *screen,SexyMatrix3& matrix);
void H1ScreenDrawTriangles(GLImage *screen,Image *texture,const TriVertex vertices[][3],int count,
    const Rect& clip,const Color& color,int mode,float tx,float ty,bool linear);
void H1ScreenNoteDraw(GLImage *screen);
}
#endif
