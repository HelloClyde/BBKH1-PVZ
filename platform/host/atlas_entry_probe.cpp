/* Host fixture for the original atlas collector at root screen entry.
 * The real definitions, holder, builder, WidgetManager and renderer are used. */
#include "LawnApp.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/ReanimAtlas.h"
#include "graphics/GLImage.h"
#include "graphics/GLInterface.h"
#include "graphics/MemoryImageCache.h"
#include "widget/Widget.h"
#include "widget/WidgetManager.h"
#include "h1_host.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>

extern "C" void pvzh1_reanim_atlas_cache_stats(uint64_t*,size_t*);
using namespace Sexy;
namespace {
constexpr ReanimationType unusedType=ReanimationType::REANIM_SUNFLOWER;
void Check(bool ok,const char *text){if(!ok)throw std::runtime_error(text);}
uint64_t Hash(const uint32_t *pixels,size_t count){
    uint64_t h=1469598103934665603ull;
    while(count--){uint32_t p=*pixels++;for(int s=0;s<32;s+=8){h^=(p>>s)&255;h*=1099511628211ull;}}
    return h;
}
uint64_t Handles(ReanimatorDefinition& definition,ReanimAtlas *atlas){
    uint64_t h=1469598103934665603ull;
    for(int t=0;t<definition.mTracks.count;++t)for(int f=0;f<definition.mTracks.tracks[t].mTransforms.count;++f){
        Image *image=definition.mTracks.tracks[t].mTransforms.mTransforms[f].mImage;
        ReanimAtlasImage *encoded=atlas?atlas->GetEncodedReanimAtlas(image):nullptr;
        uintptr_t value=(uintptr_t)(encoded?encoded->mOriginalImage:image);
        for(unsigned s=0;s<sizeof(value)*8;s+=8){h^=(value>>s)&255;h*=1099511628211ull;}
    }
    return h;
}
ReanimatorDefinition& Definition(){return gReanimatorDefArray[(int)unusedType];}
ReanimAtlas *Preload(){ReanimatorEnsureDefinitionLoaded(unusedType,false);ReanimationPreload(unusedType);return Definition().mReanimAtlas;}
uint64_t AtlasHash(ReanimAtlas *atlas){
    Check(atlas&&atlas->mMemoryImage,"missing original sunflower atlas");
    return Hash(atlas->mMemoryImage->GetBits(),(size_t)atlas->mMemoryImage->mWidth*atlas->mMemoryImage->mHeight);
}
struct Fixture:Widget {
    bool expectCollected;uint64_t expectedHandles,expectedPixels;bool observed=false;
    Fixture(bool collected,uint64_t handles,uint64_t pixels):expectCollected(collected),expectedHandles(handles),expectedPixels(pixels){Resize(0,0,800,600);}
    void Draw(Graphics *g)override{
        Check((Definition().mReanimAtlas==nullptr)==expectCollected,"atlas collection did not respect root screen scope");
        if(expectCollected)Check(Handles(Definition(),nullptr)==expectedHandles,"encoded image handles were not restored before first draw");
        Reanimation animation;animation.ReanimationInitializeType(340,250,unusedType);
        animation.SetFramesForLayer("anim_idle");animation.mAnimTime=0.15f;
        Check(AtlasHash(Definition().mReanimAtlas)==expectedPixels,"original atlas rebuild changed pixels");
        g->SetColor(Color(17,29,43));g->FillRect(0,0,800,600);animation.Draw(g);observed=true;
    }
};
struct Empty:Widget{Empty(){Resize(0,0,800,600);}void Draw(Graphics*)override{}};
struct Nested:Widget {
    bool offscreen;bool observed=false;
    Nested(bool to):offscreen(to){Resize(0,0,800,600);}
    void Draw(Graphics *g)override{
        ReanimAtlas *atlas=Preload();const uint32_t *held=atlas->mMemoryImage->GetBits();const uint32_t pixel=held[1];
        WidgetManager inner(gLawnApp);inner.mImage=static_cast<MemoryImage*>(g->mDestImage);inner.Resize(Rect(0,0,800,600),Rect(0,0,800,600));
        Empty child;inner.AddWidget(&child);
        if(offscreen)inner.DrawWidgetsTo(g);else inner.DrawScreen();
        Check(Definition().mReanimAtlas==atlas&&held[1]==pixel,"nested draw freed an outer atlas/raw pointer");
        inner.RemoveWidget(&child);observed=true;
    }
};
std::vector<uint32_t> Draw(Fixture& widget,MemoryImage *destination,bool screen){
    WidgetManager manager(gLawnApp);manager.mImage=destination;manager.Resize(Rect(0,0,800,600),Rect(0,0,800,600));manager.AddWidget(&widget);
    if(screen)manager.DrawScreen();else{Graphics graphics(destination);manager.DrawWidgetsTo(&graphics);}
    manager.RemoveWidget(&widget);Check(widget.observed,"original widget draw was not executed");
    const uint32_t *pixels=destination->GetBits();return {pixels,pixels+800*600};
}
}
extern "C" void pvzh1_host_probe_atlas_entry(){
    static bool ran=false;const char *setting=getenv("PVZ_HOST_ATLAS_ENTRY_PROBE");if(ran||!setting||setting[0]!='1')return;ran=true;
    auto *holder=gLawnApp->mEffectSystem->mReanimationHolder.get();
    for(Reanimation *animation:holder->mReanimations)Check(animation->mDefinition!=&Definition(),"sunflower fixture definition is already live");
    Reanimation *active=holder->AllocReanimation(0,0,0,ReanimationType::REANIM_PEASHOOTER);
    Reanimation *dead=holder->AllocReanimation(0,0,0,ReanimationType::REANIM_WALLNUT);dead->ReanimationDie();
    ReanimAtlas *activeAtlas=active->mDefinition->mReanimAtlas,*deadAtlas=dead->mDefinition->mReanimAtlas;
    Check(activeAtlas&&deadAtlas,"active/dead holder atlas fixture did not initialize");
    ReanimAtlas *unused=Preload();const uint64_t expectedPixels=AtlasHash(unused),expectedHandles=Handles(Definition(),unused);
    const size_t unusedBytes=(size_t)unused->mMemoryImage->mWidth*unused->mMemoryImage->mHeight*4+4;
    MemoryImage *screen=gLawnApp->mGLInterface->GetScreenImage();const uint32_t *old=screen->GetBits();std::vector<uint32_t> saved(old,old+800*600);
    const uint64_t frames=H1ImageCacheGetStats().frames;
    Fixture baseline(false,expectedHandles,expectedPixels);auto before=Draw(baseline,screen,false);
    uint64_t countBefore=0,countAfter=0;size_t bytesBefore=0,bytesAfter=0;pvzh1_reanim_atlas_cache_stats(&countBefore,&bytesBefore);
    Fixture collect(true,expectedHandles,expectedPixels);auto after=Draw(collect,screen,true);
    pvzh1_reanim_atlas_cache_stats(&countAfter,&bytesAfter);
    Check(before==after,"pre-draw atlas GC changed the complete screen pixels");
    Check(countAfter>countBefore&&bytesAfter-bytesBefore>=unusedBytes,"unused atlas was not collected before rendering");
    Check(active->mDefinition->mReanimAtlas==activeAtlas&&dead->mDefinition->mReanimAtlas==deadAtlas,"active or dead holder atlas was collected");
    MemoryImage offscreen(gLawnApp);offscreen.Create(800,600);Fixture other(false,expectedHandles,expectedPixels);auto off=Draw(other,&offscreen,true);
    Fixture offscreenBaseline(false,expectedHandles,expectedPixels);auto offBefore=Draw(offscreenBaseline,&offscreen,false);
    Check(offBefore==off,"offscreen root entry changed offscreen pixels");
    for(bool to:{false,true}){
        WidgetManager manager(gLawnApp);manager.mImage=screen;manager.Resize(Rect(0,0,800,600),Rect(0,0,800,600));
        Nested nested(to);manager.AddWidget(&nested);manager.DrawScreen();manager.RemoveWidget(&nested);Check(nested.observed,"nested atlas fixture was not drawn");
    }
    Check(H1ImageCacheGetStats().frames==frames,"pre-draw atlas GC presented/advanced physical frames");
    std::memcpy(screen->GetBits(),saved.data(),saved.size()*4);
    holder->mReanimations.DataArrayFree(active);holder->mReanimations.DataArrayFree(dead);
    const char *directory=getenv("PVZ_HOST_OUTPUT");std::ofstream out(std::filesystem::path(directory?directory:".")/"atlas-entry-verification.json");
    out<<"{\"scope\":\"Real original sunflower definition/atlas builder, holder instances and WidgetManager root screen entry; isolated host fixture\","
       <<"\"baseline_screen_hash\":"<<Hash(before.data(),before.size())<<",\"collected_rebuilt_screen_hash\":"<<Hash(after.data(),after.size())
       <<",\"original_rebuilt_atlas_hash\":"<<expectedPixels<<",\"unused_atlas_pixel_bytes\":"<<unusedBytes<<",\"collector_bytes_released\":"<<bytesAfter-bytesBefore
       <<",\"active_holder_preserved\":true,\"dead_holder_preserved\":true,\"encoded_handles_restored\":true,\"offscreen_root_preserved\":true,\"nested_outer_raw_pointer_preserved\":true,\"physical_frames_unchanged\":true,\"all_passed\":true}\n";
    pvzp_log_line("HOST ATLAS ENTRY PROBE: root-only unused collector, active/dead guards, restored handles, original atlas/pixel rebuild and nested pointers passed");
}
