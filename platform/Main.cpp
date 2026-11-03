/* LGPL-3.0-or-later. Only host lifecycle; all game flow is upstream main. */
#include "h1_host.h"
#include "graphics/SoftwareDisplay.h"
#include "resources/h1_resources.h"
#include "graphics/MemoryImageCache.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"

int pvzp_upstream_main(int,char**);
extern "C" void pvzp_music_shutdown(void);
extern "C" void pvzh1_reanim_atlas_end_frame(void*);
extern "C" void pvzh1_render_plan_shutdown(void) __attribute__((weak));
static int present(const uint32_t *pixels,int w,int h,void*){
    if(gLawnApp){
        pvzp_diagnostic[7]=static_cast<uint32_t>(gLawnApp->mGameScene);
        pvzp_diagnostic[8]=gLawnApp->mUpdateCount;
        if(gLawnApp->mBoard){const Board *b=gLawnApp->mBoard;
            pvzp_diagnostic[9]=b->mLevel;pvzp_diagnostic[10]=b->mSunMoney;
            pvzp_diagnostic[11]=b->mCurrentWave;pvzp_diagnostic[12]=b->mMainCounter;
        }
        pvzp_diagnostic[13]=gLawnApp->mPlayerInfo?gLawnApp->mPlayerInfo->mLevel:0;
    }
    pvzp_present_argb(pixels,w,h);return pvzp_diagnostic[3]==2?0:1;
}
static void frame_complete(void* context){
    pvzh1_reanim_atlas_end_frame(context);
    // EndFrame has already published the complete accounting. Do not walk
    // every source a second time just to populate these diagnostics.
    auto cache=Sexy::H1ImageCacheCompletedFrameStats();
    pvzp_diagnostic[20]=(uint32_t)cache.residentBytes;
    pvzp_diagnostic[21]=(uint32_t)cache.peakBytes;
    pvzp_diagnostic[22]=(uint32_t)cache.loadFailures;
}
extern "C" int pvz_portable_main(void){
    const char *root=getenv("PVZ_RESOURCE_DIR");h1_resources_set_root(root?root:"/PVZ/");
    h1_resources_set_logger(pvzp_log_line);
    Sexy::H1ImageCacheConfigureReducedPack();
    Sexy::H1ImageCacheSetBudget(7u*1024u*1024u);
    pvzh1_graphics_set_display_size(480,272);
    pvzh1_graphics_set_present(present,nullptr);
    pvzh1_graphics_set_frame_complete(frame_complete,nullptr);
    char program[]="H1PVZPortable.bda";char *args[]={program,nullptr};
    int result=pvzp_upstream_main(1,args);
    gLawnApp=nullptr; // Upstream main has deleted the app; host diagnostics must not dereference it.
    // Release optional rendering plans before closing either host or native I/O.
    if(pvzh1_render_plan_shutdown)pvzh1_render_plan_shutdown();
    pvzp_music_shutdown();
    pvzp_window_close();return result;
}
