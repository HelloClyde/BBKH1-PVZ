#ifndef PVZH1_GRAPHICS_TEST_APP_H
#define PVZH1_GRAPHICS_TEST_APP_H
/* Unit-test fixture only. Production builds use the original SexyAppBase. */
#include "Common.h"
#include "graphics/GLInterface.h"
#include <thread>
#include <memory>
namespace Sexy {
class SexyAppBase {
public:
    int mWidth=64,mHeight=64;
    uchar mAdd8BitMaxTable[512];
    std::thread::id mPrimaryThreadId=std::this_thread::get_id();
    std::unique_ptr<GLInterface> mGLInterface;
    SexyAppBase(){for(int i=0;i<512;++i)mAdd8BitMaxTable[i]=(uchar)std::min(i,255);}
    bool Is3DAccelerated(){return true;}
    void ColorizeImage(Image*,const Color&);
    GLImage* CreateColorizedImage(Image*,const Color&);
    void AddMemoryImage(MemoryImage*){}
    void RemoveMemoryImage(MemoryImage*){}
    void Remove3DData(MemoryImage *image){if(mGLInterface)mGLInterface->Remove3DData(image);}
};
extern SexyAppBase *gSexyAppBase;
}
#endif
