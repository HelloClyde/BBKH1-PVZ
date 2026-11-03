/* Host-only pressure regression around original synchronous triangle groups
 * and atlas creation. Local fixture widgets do not mutate Lawn game state. */
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
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstring>

extern "C" size_t pvzh1_cache_memory_pressure(size_t);
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" int pvzh1_widget_screen_batch_active();
void ReanimationCreateAtlas(ReanimatorDefinition*,ReanimationType);
using namespace Sexy;
namespace {
// Forced-pressure fixtures model allocation retries through exhaustion.
size_t DrainPressure(size_t requested){
    size_t total=0;while(size_t n=pvzh1_cache_memory_pressure(requested))total+=n;return total;
}
void Check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
uint64_t Hash(const uint32_t *pixels,size_t count){uint64_t h=1469598103934665603ull;while(count--){uint32_t p=*pixels++;for(int i=0;i<4;++i){h^=(p>>(i*8))&255;h*=1099511628211ull;}}return h;}
using Images=std::array<MemoryImage*,3>;
uint64_t Atlas(MemoryImage *small,MemoryImage *token,bool allowed){
    token->GetBits();
    ReanimatorTransform transform;transform.mImage=small;
    ReanimatorTrack track;track.mTransforms={&transform,1};
    ReanimatorDefinition definition;definition.mTracks={&track,1};
    ReanimationCreateAtlas(&definition,ReanimationType::REANIM_PEASHOOTER);
    std::unique_ptr<ReanimAtlas> atlas(definition.mReanimAtlas);
    Check(atlas&&atlas->mMemoryImage&&atlas->mImageArray.size()==1,"original atlas fixture did not build");
    DrainPressure(1);
    Check((token->mBits==nullptr)==allowed,"new-atlas epoch escaped its drawing scope");
    const uint64_t hash=Hash(atlas->mMemoryImage->GetBits(),(size_t)atlas->mMemoryImage->mWidth*atlas->mMemoryImage->mHeight);
    token->GetBits();ReanimationCreateAtlas(&definition,ReanimationType::REANIM_PEASHOOTER);
    DrainPressure(1);Check(token->mBits!=nullptr,"atlas early-return advanced epoch");
    ReanimatorDefinition skipped;
    ReanimationCreateAtlas(&skipped,ReanimationType::REANIM_LOADBAR_SPROUT);
    DrainPressure(1);Check(!skipped.mReanimAtlas&&token->mBits,"NO_ATLAS early-return advanced epoch");
    return hash;
}
struct GroupWidget:Widget {
    Images images;MemoryImage *small;bool pressure,allowed;size_t freed=0;unsigned checks=0;uint64_t atlasHash=0;
    GroupWidget(Images list,MemoryImage *s,bool force,bool safe):images(list),small(s),pressure(force),allowed(safe){Resize(0,0,800,600);}
    void Draw(Graphics *g)override{
        Check((pvzh1_widget_screen_batch_active()!=0)==allowed,"screen scope does not distinguish nested/offscreen draws");
        const uint64_t frames=H1ImageCacheGetStats().frames;
        g->SetColor(Color(12,24,36));g->FillRect(0,0,800,600);
        PvzpTriangleGroup group;
        const int order[4]={0,1,2,0}; // Original XML track order: Center, Left, Right; then repeat Center to verify reload.
        const int xs[3]={80,0,71},ys[3]={310,0,41};
        for(int i=0;i<4;++i){
            MemoryImage *source=images[order[i]];
            SexyMatrix3 matrix;matrix.LoadIdentity();matrix.m02=xs[order[i]]+source->mWidth*0.5f;matrix.m12=ys[order[i]]+source->mHeight*0.5f;
            group.AddTriangle(g,source,matrix,Rect(0,0,800,600),Color::White,Graphics::DRAWMODE_NORMAL,Rect(0,0,source->mWidth,source->mHeight));
            Check(group.mImage==source&&group.mTriangleCount>0,"original group lost pending image/vertex metadata");
            if(pressure&&i){
                MemoryImage *previous=images[order[i-1]];
                freed+=DrainPressure((size_t)source->mWidth*source->mHeight*4+4);
                Check((previous->mBits==nullptr)==allowed,"completed triangle batch did not respect scope pin");
                Check(previous->mWidth>0&&!previous->mFilePath.empty(),"pressure destroyed retained Image metadata");++checks;
            }
        }
        group.DrawGroup(g);Check(group.mTriangleCount==0&&!group.mHasMixedDrawModes&&!gPvzpTriangleDrawAdditive,"group state was not reset");
        images[0]->GetBits();group.DrawGroup(g);DrainPressure(1);
        Check(images[0]->mBits!=nullptr,"empty group advanced epoch");
        Check(H1ImageCacheGetStats().frames==frames,"triangle batches changed physical-present count");
        atlasHash=Atlas(small,images[0],allowed);
    }
};
struct Nested:Widget {
    Images images;MemoryImage *small;bool offscreen;size_t freed=0;
    Nested(Images list,MemoryImage *s,bool to):images(list),small(s),offscreen(to){Resize(0,0,800,600);}
    void Draw(Graphics *g)override{
        WidgetManager inner(gLawnApp);inner.mImage=static_cast<MemoryImage*>(g->mDestImage);inner.Resize(Rect(0,0,800,600),Rect(0,0,800,600));
        GroupWidget fixture(images,small,true,false);inner.AddWidget(&fixture);
        const uint32_t *held=images[0]->GetBits();const uint32_t expected=held[10];
        if(offscreen)inner.DrawWidgetsTo(g);else inner.DrawScreen();
        Check(images[0]->mBits&&held[10]==expected,"nested group/atlas invalidated outer source pointer");
        inner.RemoveWidget(&fixture);freed=fixture.freed;
    }
};
std::vector<uint32_t> Draw(Images sources,MemoryImage *small,bool pressure,int mode,size_t& freed,unsigned& checks,uint64_t& atlasHash){
    pvzh1_cache_complete_draw_batch();DrainPressure(1);
    MemoryImage *screen=gLawnApp->mGLInterface->GetScreenImage();
    WidgetManager manager(gLawnApp);manager.mImage=screen;manager.Resize(Rect(0,0,800,600),Rect(0,0,800,600));
    if(mode<2){
        GroupWidget fixture(sources,small,pressure,mode==0);manager.AddWidget(&fixture);
        if(mode==0)manager.DrawScreen();else{Graphics graphics(screen);manager.DrawWidgetsTo(&graphics);}
        freed=fixture.freed;checks=fixture.checks;atlasHash=fixture.atlasHash;manager.RemoveWidget(&fixture);
    }else{
        Nested nested(sources,small,mode==2);manager.AddWidget(&nested);manager.DrawScreen();manager.RemoveWidget(&nested);
    }
    const uint32_t *pixels=screen->GetBits();return std::vector<uint32_t>(pixels,pixels+800*600);
}
}
extern "C" void pvzh1_host_probe_triangles(){
    static bool ran=false;const char *setting=getenv("PVZ_HOST_TRIANGLE_PROBE");if(ran||!setting||setting[0]!='1')return;ran=true;
    std::array<std::unique_ptr<GLImage>,3> owned;
    const char *paths[3]={"reanim/selectorscreen_bg_center","reanim/selectorscreen_bg_left","reanim/selectorscreen_bg_right"};
    Images images;
    for(int i=0;i<3;++i){owned[i].reset(gLawnApp->GetImage(paths[i],false));Check(owned[i]!=nullptr,"missing original selector triangle texture");images[i]=owned[i].get();}
    Check(images[0]->mWidth==720&&images[0]->mHeight==350&&images[1]->mWidth==476&&images[1]->mHeight==680&&images[2]->mWidth==730&&images[2]->mHeight==560,"original selector texture dimensions changed");
    std::unique_ptr<GLImage> small(gLawnApp->GetImage("reanim/twinsunflower_stem1",false));
    Check(small&&small->mWidth<=254&&small->mHeight<=254,"missing original atlas fixture image");
    MemoryImage *screen=gLawnApp->mGLInterface->GetScreenImage();const uint32_t *old=screen->GetBits();std::vector<uint32_t> saved(old,old+800*600);
    size_t ignored=0,freed=0;unsigned count=0,ignoredCount=0;uint64_t atlasBefore=0,atlasAfter=0;
    const uint64_t outsideAtlas=Atlas(small.get(),images[0],true);
    auto baseline=Draw(images,small.get(),false,0,ignored,ignoredCount,atlasBefore);
    auto pressure=Draw(images,small.get(),true,0,freed,count,atlasAfter);
    Check(baseline==pressure&&atlasBefore==atlasAfter&&outsideAtlas==atlasBefore,"triangle/atlas pressure changed original pixel output");
    auto offscreenBaseline=Draw(images,small.get(),false,1,ignored,ignoredCount,atlasBefore);
    auto offscreenPressure=Draw(images,small.get(),true,1,ignored,ignoredCount,atlasAfter);
    Check(offscreenBaseline==offscreenPressure,"offscreen triangle pixels changed");
    Draw(images,small.get(),true,2,ignored,ignoredCount,atlasAfter);Draw(images,small.get(),true,3,ignored,ignoredCount,atlasAfter);
    std::memcpy(screen->GetBits(),saved.data(),saved.size()*4);
    const char *directory=getenv("PVZ_HOST_OUTPUT");std::ofstream out(std::filesystem::path(directory?directory:".")/"triangle-epoch-verification.json");
    out<<"{\"scope\":\"Original TriangleGroup AddTriangle change-image flush on the three original full-size Selector textures; original new-atlas builder on a local original-image fixture\","
       <<"\"baseline_pixel_hash\":"<<Hash(baseline.data(),baseline.size())<<",\"pressure_pixel_hash\":"<<Hash(pressure.data(),pressure.size())
       <<",\"pressure_bytes_freed\":"<<freed<<",\"group_image_metadata_checks\":"<<count<<",\"atlas_pixel_hash\":"<<atlasBefore
       <<",\"actual_screen_only\":true,\"offscreen_pixels_equal\":true,\"nested_outer_raw_pointer_safe\":true,\"atlas_noop_paths_do_not_advance_epoch\":true,\"physical_frame_count_preserved\":true,\"all_passed\":true}\n";
    pvzp_log_line("HOST TRIANGLE PROBE: three original selector textures, synchronous group pressure, atlas creation and nested pointer pins passed");
}
