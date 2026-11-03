/* LGPL-3.0-or-later. H1 backend for the upstream window lifecycle. */
#include "SexyAppBase.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "widget/WidgetManager.h"
#include "h1_host.h"
using namespace Sexy;
void SexyAppBase::MakeWindow(){
    if(!pvzp_window_open()){mShutdown=true;return;}
    mWindow=reinterpret_cast<void*>(1);mContext=nullptr;
    if(!mGLInterface){mGLInterface=std::make_unique<GLInterface>(this);if(!InitGLInterface()){mGLInterface.reset();mShutdown=true;return;}}
    mActive=true;mMinimized=mPhysMinimized=false;
    ReInitImages();mWidgetManager->mImage=mGLInterface->GetScreenImage();
    mWidgetManager->MarkAllDirty();mGLInterface->UpdateViewport();
    mWidgetManager->Resize(mScreenBounds,mGLInterface->mPresentationRect);
}
