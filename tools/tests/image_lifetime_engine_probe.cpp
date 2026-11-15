/* Actual image storage and real widget/effect destruction, without eviction. */
#define pvzh1_ui_flow_probe pvzh1_lifetime_ui_flow
#include "ui_flow_engine_probe.cpp"
#undef pvzh1_ui_flow_probe
#include "graphics/ImageLifetime.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/MemoryImage.h"
#include "widget/Widget.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpParticle.h"
#include "PvzpLib/Trail.h"
extern "C" size_t pvzh1_cache_memory_pressure(size_t);
namespace {
void CheckLifetime(bool ok,const char* message){
    if(!ok){std::fprintf(stderr,"IMAGE LIFETIME: %s\n",message);throw std::runtime_error(message);}
}
void DescribeLifetime(Sexy::MemoryImage& image){
    CheckLifetime(Sexy::H1ImageCacheDescribe(&image,"particles/pea_splats",true,0xffffff),"Fixture image missing");
}
void OwnerChecks(){
    using namespace Sexy;
    MemoryImage image(gLawnApp);DescribeLifetime(image);int first=0,second=0;
    uint32_t* shared=nullptr;
    {H1ImageUseScope use(H1ImageOwner::Operation,&first);shared=image.GetBits();}
    CheckLifetime(shared&&H1ImageResourceOwnerCount(&image)==1,"First explicit owner not retained");
    {H1ImageUseScope use(H1ImageOwner::Operation,&second);CheckLifetime(image.GetBits()==shared,"Second owner duplicated backing");}
    CheckLifetime(H1ImageResourceOwnerCount(&image)==2,"Shared image does not have both owners");
    H1ImageResourcesReleaseOwner(H1ImageOwner::Operation,&first);
    CheckLifetime(H1ImageResourceOwnerCount(&image)==1&&image.mBits.get()==shared,"First release invalidated the second owner");
    const auto loads=H1ImageCacheGetStats().loads;
    CheckLifetime(pvzh1_cache_memory_pressure(SIZE_MAX)==0,"Allocator reclaimed an owned image");
    for(unsigned i=0;i<120;++i)H1ImageCacheEndFrame();
    CheckLifetime(image.mBits.get()==shared&&H1ImageCacheGetStats().loads==loads,"Frame processing discarded live storage");
    H1ImageResourcesReleaseOwner(H1ImageOwner::Operation,&second);
    CheckLifetime(!image.mBits&&H1ImageResourceOwnerCount(&image)==0,"Last owner did not release immediately");
    // Reusing an owner address must obtain a fresh live reference.
    {H1ImageUseScope use(H1ImageOwner::Operation,&second);CheckLifetime(image.GetBits()!=nullptr,"Owner address reuse failed");}
    H1ImageResourcesReleaseOwner(H1ImageOwner::Operation,&second);
    CheckLifetime(!image.mBits,"Reused owner's storage remained resident");
    // Pixel mutation transfers the data to the ordinary Image object.
    {H1ImageUseScope use(H1ImageOwner::Operation,&first);shared=image.GetBits();image.BitsChanged();}
    CheckLifetime(!H1ImageCacheContains(&image),"Mutable image remained in immutable registry");
    H1ImageResourcesReleaseOwner(H1ImageOwner::Operation,&first);
    CheckLifetime(image.mBits.get()==shared,"Resource release destroyed mutable image data");
}
void ObjectChecks(){
    using namespace Sexy;
    MemoryImage image(gLawnApp);DescribeLifetime(image);
    {Widget widget;{H1ImageUseScope use(H1ImageOwner::Widget,&widget);image.GetBits();}
        widget.SetVisible(false);CheckLifetime(!image.mBits,"Hidden widget retained its textures");}
    auto* holder=gLawnApp->mEffectSystem->mReanimationHolder.get();
    auto* first=holder->AllocReanimation(0,0,0,REANIM_FLAG);
    auto* second=holder->AllocReanimation(0,0,0,REANIM_FLAG);
    {H1ImageUseScope use(H1ImageOwner::Animation,first);image.GetBits();}
    {H1ImageUseScope use(H1ImageOwner::Animation,second);image.GetBits();}
    first->ReanimationDie();CheckLifetime(image.mBits&&H1ImageResourceOwnerCount(&image)==1,"Animation death ignored sharing");
    second->ReanimationDie();CheckLifetime(!image.mBits,"Finished animation retained its texture");
    holder->mReanimations.DataArrayFree(first);holder->mReanimations.DataArrayFree(second);
    {PvzpParticleSystem particles;{H1ImageUseScope use(H1ImageOwner::Particle,&particles);image.GetBits();}
        particles.ParticleSystemDie();CheckLifetime(!image.mBits,"Dead particle system retained its texture");}
    {Trail trail;TrailDefinition definition;trail.mDefinition=&definition;trail.mTrailDuration=1;
        {H1ImageUseScope use(H1ImageOwner::Trail,&trail);image.GetBits();}
        trail.Update();CheckLifetime(trail.mDead&&!image.mBits,"Expired trail retained its texture");}
}
void SceneChecks(){
    using namespace Sexy;
    MemoryImage image(gLawnApp),common(gLawnApp);DescribeLifetime(image);DescribeLifetime(common);
    int widget=0,animation=0,ui=0;
    {H1ImageUseScope use(H1ImageOwner::Widget,&widget);image.GetBits();}
    {H1ImageUseScope use(H1ImageOwner::Animation,&animation);image.GetBits();}
    {H1ImageUseScope use(H1ImageOwner::CommonUI,&ui);common.GetBits();}
    const auto uiPixels=common.mBits.get();
    H1ImageResourcesBeginScene(H1ImageScene::Menu);
    CheckLifetime(!image.mBits&&H1ImageResourceOwnerCount(&image)==0,"Previous scene retained object textures");
    CheckLifetime(uiPixels&&common.mBits.get()==uiPixels,"Scene exit released declared common UI");
    // Deferred deletion of the old widget must not affect the next scene.
    {H1ImageUseScope use(H1ImageOwner::Widget,&widget);image.GetBits();}
    H1ImageResourcesReleaseOwner(H1ImageOwner::Animation,&animation);
    CheckLifetime(image.mBits&&H1ImageResourceOwnerCount(&image)==1,"Old animation released a new scene's image");
    H1ImageResourcesReleaseOwner(H1ImageOwner::Widget,&widget);
    H1ImageResourcesBeginScene(H1ImageScene::Opening);
    CheckLifetime(!common.mBits&&H1ImageResourceOwnerCount(&common)==0,"Noninteractive opening retained interactive UI images");
    H1ImageResourcesReleaseOwner(H1ImageOwner::CommonUI,&ui);
    CheckLifetime(!image.mBits&&!common.mBits,"Explicit scene/UI release incomplete");
}
}
extern "C" void pvzh1_image_lifetime_probe(){
    std::fprintf(stderr,"LIFETIME owner checks\n");OwnerChecks();
    std::fprintf(stderr,"LIFETIME object checks\n");ObjectChecks();
    std::fprintf(stderr,"LIFETIME scene checks\n");SceneChecks();
    std::fprintf(stderr,"LIFETIME original UI and complete opening\n");
    H1IntroMovieTestEnable(true);pvzh1_lifetime_ui_flow();
    const auto stats=Sexy::H1ImageResourcesStats();
    CheckLifetime(stats.scene==Sexy::H1ImageScene::Menu&&stats.sourceReleases>0,"Real scene transitions did not release resources");
    CheckLifetime(Sexy::H1ImageCacheGetStats().evictions==0,"Automatic image eviction is still active");
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"image-lifetime-verification.json");
    out<<"{\"passed\":true,\"shared_last_owner_release\":true,\"widget_hide_release\":true,\"animation_death_release\":true,\"particle_death_release\":true,\"trail_expiry_release\":true,\"scene_exit_release\":true,\"common_UI_shared\":true,\"allocator_cannot_evict_live_images\":true,\"full_opening_and_first_level\":true,\"automatic_evictions\":0,\"source_releases\":"<<stats.sourceReleases<<",\"released_bytes\":"<<stats.releasedBytes<<",\"menu_resident_bytes\":"<<stats.residentBytes<<",\"menu_owner_references\":"<<stats.references<<"}\n";
}
