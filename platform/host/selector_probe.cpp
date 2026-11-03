/* Real NO_ATLAS selector regression: original full definition and original
 * Reanimation::DrawRenderGroup, with pressure observed after actual matrix
 * draw returns. This fixture changes no live Lawn board/profile state. */
#include "LawnApp.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/GLImage.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImageCache.h"
#include "widget/Widget.h"
#include "widget/WidgetManager.h"
#include "h1_host.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>

extern "C" size_t pvzh1_cache_memory_pressure(size_t);
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" int pvzh1_widget_screen_batch_active();
using namespace Sexy;
namespace {
// Forced-pressure fixtures model allocation retries through exhaustion.
size_t DrainPressure(size_t requested){
    size_t total=0;while(size_t n=pvzh1_cache_memory_pressure(requested))total+=n;return total;
}
void Check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
struct Observation {
    bool force=false,allowed=false;
    std::array<MemoryImage*,3> backgrounds{};
    unsigned calls=0,backgroundsSeen[3]{0,0,0};size_t freed=0;
};
Observation *observation=nullptr;
uint64_t Hash(const std::vector<uint32_t>& pixels){uint64_t h=1469598103934665603ull;for(uint32_t p:pixels)for(int s=0;s<32;s+=8){h^=(p>>s)&255;h*=1099511628211ull;}return h;}
struct Selector:Widget {
    Reanimation animation;Observation stats;uint64_t frames=0;
    Selector(bool force,bool allowed,bool extra){
        Resize(0,0,800,600);stats.force=force;stats.allowed=allowed;
        animation.ReanimationInitializeType(0.5f,0.5f,ReanimationType::REANIM_SELECTOR_SCREEN);
        animation.PlayReanim("anim_open",ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD,0,30.0f);
        animation.AssignRenderGroupToPrefix("flower",RENDER_GROUP_HIDDEN);
        animation.AssignRenderGroupToPrefix("leaf",RENDER_GROUP_HIDDEN);
        animation.AssignRenderGroupToTrack("SelectorScreen_BG",1);
        int start,count;animation.GetFramesForLayer("anim_sign",start,count);animation.mFrameBasePose=start+count-1;
        animation.mAnimTime=0.65f;
        animation.mEnableExtraAdditiveDraw=extra;animation.mExtraAdditiveColor=Color(20,30,40,50);
        animation.mEnableExtraOverlayDraw=extra;animation.mExtraOverlayColor=Color(200,220,240,25);
        Check(animation.mDefinition==&gReanimatorDefArray[(int)ReanimationType::REANIM_SELECTOR_SCREEN],"probe does not use complete original selector definition");
        Check(!animation.mDefinition->mReanimAtlas&&(gReanimationParamArray[(int)ReanimationType::REANIM_SELECTOR_SCREEN].mReanimParamFlags&1),"selector fixture did not use real NO_ATLAS path");
        stats.backgrounds={static_cast<MemoryImage*>(animation.GetCurrentTrackImage("SelectorScreen_BG_Center")),static_cast<MemoryImage*>(animation.GetCurrentTrackImage("SelectorScreen_BG_Left")),static_cast<MemoryImage*>(animation.GetCurrentTrackImage("SelectorScreen_BG_Right"))};
        for(auto *image:stats.backgrounds)Check(image!=nullptr,"original selector background track is absent");
    }
    void Draw(Graphics *g)override{
        frames=H1ImageCacheGetStats().frames;g->SetLinearBlend(true);g->SetColor(Color(8,16,24));g->FillRect(0,0,800,600);
        Observation *previous=observation;observation=&stats;
        animation.DrawRenderGroup(g,1);animation.DrawRenderGroup(g,RENDER_GROUP_NORMAL);
        observation=previous;
        Check(H1ImageCacheGetStats().frames==frames,"matrix completion changed physical-present frame count");
    }
};
struct Nested:Widget {
    bool offscreen,extra;Observation result;
    Nested(bool to,bool effects):offscreen(to),extra(effects){Resize(0,0,800,600);}
    void Draw(Graphics *g)override{
        Selector selector(true,false,extra);
        WidgetManager inner(gLawnApp);inner.mImage=static_cast<MemoryImage*>(g->mDestImage);inner.Resize(Rect(0,0,800,600),Rect(0,0,800,600));inner.AddWidget(&selector);
        MemoryImage *image=selector.stats.backgrounds[0];const uint32_t *held=image->GetBits();const uint32_t expected=held[1];
        if(offscreen)inner.DrawWidgetsTo(g);else inner.DrawScreen();
        Check(image->mBits&&held[1]==expected,"NO_ATLAS nested matrix draw invalidated outer source pixels");
        inner.RemoveWidget(&selector);result=selector.stats;
    }
};
std::vector<uint32_t> Draw(bool force,int mode,bool extra,Observation& result,unsigned& tracks,unsigned& transforms){
    pvzh1_cache_complete_draw_batch();DrainPressure(1);
    MemoryImage *screen=gLawnApp->mGLInterface->GetScreenImage();
    WidgetManager manager(gLawnApp);manager.mImage=screen;manager.Resize(Rect(0,0,800,600),Rect(0,0,800,600));
    if(mode<2){
        Selector selector(force,mode==0,extra);manager.AddWidget(&selector);
        tracks=selector.animation.mDefinition->mTracks.count;transforms=0;
        for(unsigned i=0;i<tracks;++i)transforms+=selector.animation.mDefinition->mTracks.tracks[i].mTransforms.count;
        if(mode==0)manager.DrawScreen();else{Graphics graphics(screen);manager.DrawWidgetsTo(&graphics);}
        manager.RemoveWidget(&selector);result=selector.stats;
    }else{
        Nested nested(mode==2,extra);manager.AddWidget(&nested);manager.DrawScreen();manager.RemoveWidget(&nested);result=nested.result;
    }
    const uint32_t *pixels=screen->GetBits();return std::vector<uint32_t>(pixels,pixels+800*600);
}
}
extern "C" void pvzh1_host_reanim_batch_completed(void *pointer){
    if(!observation)return;
    auto *image=dynamic_cast<MemoryImage*>(static_cast<Image*>(pointer));
    Check((pvzh1_widget_screen_batch_active()!=0)==observation->allowed,"matrix batch scope unexpectedly enabled for nested/offscreen draw");
    ++observation->calls;
    const int width=image?image->mWidth:0,height=image?image->mHeight:0;
    if(observation->force)observation->freed+=DrainPressure((size_t)width*height*4+4);
    for(int i=0;i<3;++i)if(image==observation->backgrounds[i]){
        ++observation->backgroundsSeen[i];
        Check(width==image->mWidth&&height==image->mHeight&&!image->mFilePath.empty(),"matrix pressure destroyed Image metadata");
        if(observation->force)Check((image->mBits==nullptr)==observation->allowed,"real NO_ATLAS matrix source was not correctly pinned/reclaimed");
    }
}
extern "C" void pvzh1_host_probe_selector(){
    static bool ran=false;const char *setting=getenv("PVZ_HOST_SELECTOR_PROBE");if(ran||!setting||setting[0]!='1')return;ran=true;
    /* Production Selector instances belong to the holder. Keep an equivalent
     * real pin while these isolated fixture widgets use stack instances. */
    auto *holder=gLawnApp->mEffectSystem->mReanimationHolder.get();
    Reanimation *definitionPin=holder->AllocReanimation(0,0,0,ReanimationType::REANIM_SELECTOR_SCREEN);
    MemoryImage *screen=gLawnApp->mGLInterface->GetScreenImage();const uint32_t *old=screen->GetBits();std::vector<uint32_t> saved(old,old+800*600);
    Observation baselineStats,pressureStats,other;unsigned tracks=0,transforms=0;
    auto baseline=Draw(false,0,false,baselineStats,tracks,transforms);
    auto pressure=Draw(true,0,false,pressureStats,tracks,transforms);
    Check(baseline==pressure,"complete original selector NO_ATLAS pressure changed pixels");
    Check(pressureStats.calls>3,"original selector matrix path was not exercised");
    for(int count:pressureStats.backgroundsSeen)Check(count>0,"one original selector background did not use ReanimBltMatrix");
    auto offscreenBase=Draw(false,1,false,other,tracks,transforms);
    auto offscreenPressure=Draw(true,1,false,other,tracks,transforms);
    Check(offscreenBase==offscreenPressure,"offscreen original selector pressure changed pixels");
    Draw(true,2,false,other,tracks,transforms);Draw(true,3,false,other,tracks,transforms);
    auto extraBase=Draw(false,0,true,other,tracks,transforms);
    auto extraPressure=Draw(true,0,true,other,tracks,transforms);
    Check(extraBase==extraPressure,"original selector additive/overlay re-fetch changed pixels");
    std::memcpy(screen->GetBits(),saved.data(),saved.size()*4);
    holder->mReanimations.DataArrayFree(definitionPin);
    const char *directory=getenv("PVZ_HOST_OUTPUT");std::ofstream out(std::filesystem::path(directory?directory:".")/"selector-no-atlas-verification.json");
    out<<"{\"scope\":\"Complete pinned SelectorScreen definition and real Reanimation::DrawRenderGroup -> DrawTrack -> ReanimBltMatrix NO_ATLAS path, not manual quads\","
       <<"\"definition_tracks\":"<<tracks<<",\"definition_transforms\":"<<transforms<<",\"matrix_calls\":"<<pressureStats.calls
       <<",\"background_matrix_calls\":["<<pressureStats.backgroundsSeen[0]<<','<<pressureStats.backgroundsSeen[1]<<','<<pressureStats.backgroundsSeen[2]<<']'
       <<",\"baseline_pixel_hash\":"<<Hash(baseline)<<",\"pressure_pixel_hash\":"<<Hash(pressure)<<",\"pressure_bytes_freed\":"<<pressureStats.freed
       <<",\"extra_draw_baseline_pixel_hash\":"<<Hash(extraBase)<<",\"extra_draw_pressure_pixel_hash\":"<<Hash(extraPressure)
       <<",\"offscreen_pixels_equal\":true,\"nested_outer_raw_pointer_safe\":true,\"physical_frame_count_preserved\":true,\"no_atlas\":true,\"all_passed\":true}\n";
    pvzp_log_line("HOST SELECTOR PROBE: complete original NO_ATLAS definition/matrix draws, all backgrounds, pressure pixels and nested pins passed");
}
