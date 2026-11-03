/* Host regression using the real WidgetManager, Widget::DrawAll, ImageFont and
 * software renderer. Fixture widgets simulate pressure between drawing calls;
 * they do not replace or mutate any Lawn gameplay state. */
#include "LawnApp.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/ImageFont.h"
#include "graphics/MemoryImageCache.h"
#include "misc/ResourceManager.h"
#include "widget/Widget.h"
#include "widget/WidgetManager.h"
#include "h1_host.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>

extern "C" size_t pvzh1_cache_memory_pressure(size_t);
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" int pvzh1_widget_draw_active();
using namespace Sexy;
namespace {
// Forced-pressure fixtures model allocation retries through exhaustion.
size_t DrainPressure(size_t requested){
    size_t total=0;while(size_t n=pvzh1_cache_memory_pressure(requested))total+=n;return total;
}
void Require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
struct Background:Widget {
    MemoryImage *image;bool overlay;
    Background(MemoryImage *source,bool defer):image(source),overlay(defer){Resize(0,0,800,600);}
    void Draw(Graphics *g)override{
        g->SetColor(Color(9,18,27));g->FillRect(0,0,800,600);g->DrawImage(image,0,0);
        if(overlay)mWidgetManager->DeferOverlay(this,10);
    }
    void DrawOverlay(Graphics *g,int)override{g->DrawImage(image,650,450,Rect(0,0,64,64));}
};
struct Dialog:Widget {
    MemoryImage *background;ImageFont *font;bool pressure,eligible;size_t freed=0;uint64_t frames;
    Dialog(MemoryImage *bg,ImageFont *f,bool force,bool reclaimable):background(bg),font(f),pressure(force),eligible(reclaimable){
        Resize(0,0,800,600);frames=H1ImageCacheGetStats().frames;
    }
    void Draw(Graphics *g)override{
        Require(pvzh1_widget_draw_active()!=0,"widget draw depth is not active");
        Require(background->mBits!=nullptr,"batch callback unexpectedly evicted background");
        if(pressure){
            freed=DrainPressure(509908);
            Require((background->mBits==nullptr)==eligible,"widget epoch released current pixels or retained completed source");
            if(eligible)Require(freed>=1294724,"completed selector background was not reclaimed");
        }
        Require(H1ImageCacheGetStats().frames==frames,"drawing batch advanced physical-present frame count");
        g->SetColor(Color(36,43,58));g->FillRect(140,150,500,180);
        font->DrawStringEx(g,190,225,"CREATE A NEW USER",Color(240,220,180),nullptr,nullptr);
        font->DrawStringEx(g,190,285,"AV To 0123456789",Color(180,240,210),nullptr,nullptr);
    }
};
uint64_t Hash(const std::vector<uint32_t>& pixels){
    uint64_t h=1469598103934665603ull;for(auto p:pixels)for(int i=0;i<4;++i){h^=(p>>(i*8))&255;h*=1099511628211ull;}return h;
}
std::vector<uint32_t> Draw(MemoryImage *source,ImageFont *font,bool force,bool screen,size_t& freed){
    pvzh1_cache_complete_draw_batch();DrainPressure(1);
    MemoryImage surface(gLawnApp);surface.Create(800,600);surface.GetBits();
    WidgetManager manager(gLawnApp);manager.mImage=&surface;manager.Resize(Rect(0,0,800,600),Rect(0,0,800,600));
    Background background(source,screen);Dialog dialog(source,font,force,screen);
    manager.AddWidget(&background);manager.AddWidget(&dialog);
    if(screen)Require(manager.DrawScreen(),"real WidgetManager did not draw fixture widgets");
    else{Graphics g(&surface);manager.DrawWidgetsTo(&g);}
    freed=dialog.freed;manager.RemoveWidget(&dialog);manager.RemoveWidget(&background);
    const uint32_t *pixels=surface.GetBits();return std::vector<uint32_t>(pixels,pixels+800*600);
}
struct Nested:Widget {
    MemoryImage *source;ImageFont *font;bool offscreen;
    Nested(MemoryImage *s,ImageFont *f,bool to):source(s),font(f),offscreen(to){Resize(0,0,800,600);}
    void Draw(Graphics *g)override{
        MemoryImage surface(gLawnApp);surface.Create(800,600);surface.GetBits();
        WidgetManager inner(gLawnApp);inner.mImage=&surface;inner.Resize(Rect(0,0,800,600),Rect(0,0,800,600));
        Background bg(source,false);Dialog dialog(source,font,true,false);
        inner.AddWidget(&bg);inner.AddWidget(&dialog);
        const uint32_t *held=source->GetBits();const uint32_t expected=held[100];
        if(offscreen){Graphics nested(&surface);inner.DrawWidgetsTo(&nested);}
        else inner.DrawScreen();
        Require(source->mBits&&held[100]==expected,"nested draw invalidated outer raw source pointer");
        inner.RemoveWidget(&dialog);inner.RemoveWidget(&bg);g->DrawImage(source,0,0);
    }
};
void CheckNested(MemoryImage *source,ImageFont *font,bool offscreen){
    pvzh1_cache_complete_draw_batch();DrainPressure(1);
    MemoryImage surface(gLawnApp);surface.Create(800,600);surface.GetBits();
    WidgetManager manager(gLawnApp);manager.mImage=&surface;manager.Resize(Rect(0,0,800,600),Rect(0,0,800,600));
    Nested nested(source,font,offscreen);manager.AddWidget(&nested);manager.DrawScreen();manager.RemoveWidget(&nested);
}
}
extern "C" void pvzh1_host_probe_widgets(){
    static bool ran=false;const char *setting=getenv("PVZ_HOST_WIDGET_PROBE");
    if(ran||!setting||setting[0]!='1')return;ran=true;
    auto resource=gLawnApp->mResourceManager->mFontMap.find("FONT_DWARVENTODCRAFT24");
    Require(resource!=gLawnApp->mResourceManager->mFontMap.end(),"missing original Font24 resource");
    auto *font=dynamic_cast<ImageFont*>(static_cast<ResourceManager::FontRes*>(resource->second.get())->mFont.get());
    Require(font!=nullptr,"original Font24 is not loaded");
    std::unique_ptr<GLImage> source(gLawnApp->GetImage("reanim/SelectorScreen_BG_Left",false));
    Require(source&&source->mWidth==476&&source->mHeight==680,"original selector background dimensions differ");
    size_t ignored=0,freed=0;auto baseline=Draw(source.get(),font,false,true,ignored);
    auto pressure=Draw(source.get(),font,true,true,freed);Require(baseline==pressure,"top-level widget pressure changed final pixels");
    auto offscreenBaseline=Draw(source.get(),font,false,false,ignored);
    auto offscreenPressure=Draw(source.get(),font,true,false,ignored);
    Require(offscreenBaseline==offscreenPressure,"offscreen widget pressure changed pixels");
    CheckNested(source.get(),font,true);CheckNested(source.get(),font,false);
    Require(pvzh1_widget_draw_active()==0,"widget drawing depth leaked after returning");
    const char *directory=getenv("PVZ_HOST_OUTPUT");
    std::ofstream out(std::filesystem::path(directory?directory:".")/"widget-epoch-verification.json");
    out<<"{\"scope\":\"Host fixture using original WidgetManager/DrawAll, Selector BG_Left pixels and Font24 renderer\","
          "\"screen_baseline_pixel_hash\":"<<Hash(baseline)<<",\"screen_pressure_pixel_hash\":"<<Hash(pressure)
       <<",\"offscreen_baseline_pixel_hash\":"<<Hash(offscreenBaseline)<<",\"offscreen_pressure_pixel_hash\":"<<Hash(offscreenPressure)
       <<",\"pressure_bytes_freed\":"<<freed<<",\"deferred_overlay_reload_equal\":true,\"nested_screen_outer_pointer_safe\":true,"
         "\"nested_offscreen_outer_pointer_safe\":true,\"epoch_does_not_evict_or_present\":true,\"all_passed\":true}\n";
    pvzp_log_line("HOST WIDGET PROBE: real manager, completed source pressure, overlay reload and nested raw pointers passed");
}
