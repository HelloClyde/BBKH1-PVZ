/* Real-cache lifecycle fixture, linked to frozen complete-game host objects.
 * No device, game clock, profile or production resource is modified. */
#include "LawnApp.h"
#include "graphics/MemoryImageCache.h"
#include "h1_resources.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <type_traits>
using namespace Sexy;
extern "C" size_t pvzh1_cache_memory_pressure(size_t);
extern "C" void pvzh1_cache_complete_draw_batch();
namespace {
size_t newCalls=0,hookCalls=0,hookRequest=0,hookHint=0;
bool hookReentryBlocked=false;
void *planAllocation=nullptr;
void Check(bool yes,const char *message){if(!yes)throw std::runtime_error(message);}
uint64_t Hash(const uint32_t *p,size_t n){uint64_t h=1469598103934665603ull;while(n--){h^=*p++;h*=1099511628211ull;}return h;}
uint32_t SandPixel(MemoryImage*,uint32_t*,int,int){return 0x002468acu;}
void WriteImage(const std::filesystem::path& path){
    std::ofstream f(path,std::ios::binary);
    const uint32_t header[]={0x32493148,12,8,4,0,6*4*4,6,4};
    f.write((const char*)header,sizeof header);
    for(unsigned i=0;i<24;++i){const unsigned char rgba[]={(unsigned char)(i*7),33,99,(unsigned char)(i%3?255:0)};f.write((const char*)rgba,4);}
    Check((bool)f,"synthetic reduced source write failed");
}
uint64_t Prepare(MemoryImage& image,const uint32_t **bits=nullptr){
    const uint32_t *pixels=nullptr;uint32_t w=0,h=0;
    Check(H1ImageCachePrepareStorageTexture(&image,&pixels,&w,&h)&&w==6&&h==4,"physical dense preparation failed");
    const size_t before=newCalls;const auto revision=H1ImageCacheStorageRevision(&image);
    Check(revision&&newCalls==before,"live cookie is zero or query allocates");
    if(bits)*bits=pixels;return revision;
}
}
void *operator new(size_t n){++newCalls;if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](size_t n){return ::operator new(n);}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete[](void *p) noexcept {std::free(p);}
void operator delete(void *p,size_t) noexcept {std::free(p);}
void operator delete[](void *p,size_t) noexcept {std::free(p);}
extern "C" size_t pvzh1_render_plan_release_pressure(size_t requested){
    ++hookCalls;hookRequest=requested;
    if(!planAllocation)return 0;
    hookReentryBlocked=pvzh1_cache_memory_pressure(requested)==0;
    std::free(planAllocation);planAllocation=nullptr;const auto hint=hookHint;hookHint=0;return hint;
}
extern "C" void pvzh1_storage_revision_probe(){
    const auto output=std::filesystem::path(std::getenv("PVZ_HOST_OUTPUT"));
    const auto root=output/"revision-resources";std::filesystem::create_directories(root);WriteImage(root/"revision.h1i");
    const std::string originalRoot=h1_resources_get_root();h1_resources_set_root(root.string().c_str());
    unsigned checks=0;uint64_t originalHash=0,sandedHash=0,lastCookie=0;
    Check(H1ImageCacheStorageRevision(nullptr)==0,"null query must be zero");++checks;
    {
        MemoryImage image(gLawnApp);
        Check(H1ImageCacheDescribe(&image,"revision",false,0xffffff),"descriptor failed");
        const size_t before=newCalls;
        Check(H1ImageCacheStorageRevision(&image)==0&&newCalls==before,"descriptor-only query loaded or allocated");++checks;
        const uint32_t *pixels=nullptr;auto first=Prepare(image,&pixels);originalHash=Hash(pixels,24);
        Check(Prepare(image)==first,"warm preparation changed content identity");++checks;
        auto stats=H1ImageCacheGetStats();planAllocation=std::malloc(16384);Check(planAllocation,"fake idle plan allocation failed");hookHint=16384;
        const size_t callsBefore=newCalls;
        Check(pvzh1_cache_memory_pressure(37)==16384&&hookRequest==37&&hookReentryBlocked&&newCalls==callsBefore,"plan pressure hook/reentry/no-allocation contract failed");
        Check(H1ImageCacheStorageRevision(&image)==first&&Hash(pixels,24)==originalHash&&H1ImageCacheGetStats().evictions==stats.evictions,"metadata pressure evicted or changed source pixels");++checks;
        /* Even when there are no idle plans, an active source remains pinned. */
        pvzh1_cache_memory_pressure(37);
        Check(H1ImageCacheStorageRevision(&image)==first&&Hash(pixels,24)==originalHash,"current-batch storage pointer was reclaimed");++checks;
        pvzh1_cache_complete_draw_batch();
        for(unsigned i=0;i<4096&&pvzh1_cache_memory_pressure((size_t)-1);++i){}
        Check(H1ImageCacheStorageRevision(&image)==0,"released source retained a live cookie");++checks;
        auto second=Prepare(image,&pixels);Check(second>first&&Hash(pixels,24)==originalHash,"reloaded pixels reused a cookie or changed content");++checks;
        Check(H1ImageCacheSandingStorage(&image,nullptr,SandPixel),"physical sanding failed");
        auto third=H1ImageCacheStorageRevision(&image);sandedHash=Hash(pixels,24);
        Check(third>second&&sandedHash!=originalHash,"sanding did not replace content identity");++checks;
        Check(Prepare(image)==third,"already-sanded warm preparation changed cookie");++checks;
        pvzh1_cache_complete_draw_batch();for(unsigned i=0;i<4096&&pvzh1_cache_memory_pressure((size_t)-1);++i){}
        Check(!H1ImageCacheStorageRevision(&image),"sanded eviction retained identity");
        auto fourth=Prepare(image,&pixels);Check(fourth>third&&Hash(pixels,24)==sandedHash,"sanding recipe reload failed identity or content");++checks;
        const auto path=image.mFilePath;image.mFilePath="changed";Check(!H1ImageCacheStorageRevision(&image),"path mutation retained plan identity");image.mFilePath=path;++checks;
        ++image.mWidth;Check(!H1ImageCacheStorageRevision(&image),"dimension mutation retained plan identity");--image.mWidth;++checks;
        auto *logical=image.GetBits();Check(logical&&!H1ImageCacheStorageRevision(&image),"generic GetBits did not invalidate dense identity");++checks;
        H1ImageCacheStorageStats dense{};Check(H1ImageCacheGetStorageStats(&image,&dense)&&dense.bytes==0,"generic GetBits retained physical storage");++checks;
        image.BitsChanged();Check(!H1ImageCacheStorageRevision(&image),"modified source retained identity");++checks;
        lastCookie=fourth;
    }
    /* Real object destruction and construction at exactly the same address. */
    std::aligned_storage_t<sizeof(MemoryImage),alignof(MemoryImage)> storage;
    for(unsigned generation=0;generation<6;++generation){
        auto *image=::new(&storage)MemoryImage(gLawnApp);Check(!H1ImageCacheStorageRevision(image),"Image address ABA leaked a cookie before registration");
        Check(H1ImageCacheDescribe(image,"revision",false,0xffffff),"ABA descriptor failed");auto revision=Prepare(*image);
        Check(revision>lastCookie,"Image address reuse repeated a generation");lastCookie=revision;image->~MemoryImage();++checks;
    }
    h1_resources_set_root(originalRoot.c_str());
    std::ofstream f(output/"storage-revision-verification.json");
    f<<"{\"test_only\":true,\"real_cache\":true,\"checks\":"<<checks<<",\"same_address_generations\":6,\"original_pixel_hash\":"<<originalHash<<",\"sanded_pixel_hash\":"<<sandedHash<<",\"descriptor_query_no_allocation\":true,\"current_pointer_pin\":true,\"release_reload_invalidates\":true,\"sanding_revision_and_reload\":true,\"generic_materialization_invalidates\":true,\"plan_pressure_before_source_eviction\":true,\"pressure_reentry_guard\":true,\"plan_bytes_are_hints_not_native_span\":true,\"all_passed\":true}\n";
    std::fprintf(stderr,"STORAGE REVISION %u CHECKS PASSED\n",checks);
}
