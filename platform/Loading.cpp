/* LGPL-3.0-or-later. Present the original loader on H1's single UI task.
 * Called only after a resource has finished loading, never from GetBits(). */
#include "LawnApp.h"
#include "Lawn/Widget/TitleScreen.h"
#include "widget/WidgetManager.h"
#include "SDL.h"
#include <algorithm>
extern "C" int pvzh1_widget_draw_active();

extern "C" void pvzh1_native_loading_progress()
{
#ifdef PVZ_NATIVE_LOADING_PROGRESS
    static bool drawing=false;
    static uint32_t lastTick=0;
    if(drawing||!gLawnApp||gLawnApp->mShutdown||gLawnApp->mIsDrawing||pvzh1_widget_draw_active()||!gLawnApp->mTitleScreen||
       !gLawnApp->mTitleScreen->mLoaderScreenIsLoaded)return;
    uint32_t now=SDL_GetTicks(),elapsed=now-lastTick;
    // Loading is synchronous; repainting the full illustrated title four times
    // per second competes with resource parsing and storage reads.
    if(elapsed<1000)return;
    drawing=true;
    // Advance only the original title widget; game simulation has not started.
    unsigned steps=std::min(50u,elapsed/10u);
    for(unsigned i=0;i<steps;++i)gLawnApp->mTitleScreen->Update();
    gLawnApp->mWidgetManager->MarkAllDirty();
    gLawnApp->DrawDirtyStuff();
    lastTick=SDL_GetTicks();
    drawing=false;
#endif
}
