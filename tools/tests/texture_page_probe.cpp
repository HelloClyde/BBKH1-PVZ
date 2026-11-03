#include "LawnApp.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/GLImage.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImageCache.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <cstdlib>
#include <new>
using namespace Sexy;
static int64_t outstanding=0;static bool failRow=false,measure=false,releasedBeforeARGB=false;
static MemoryImage *watched=nullptr;static unsigned fullARGB=0;
void *operator new(std::size_t n){if(failRow&&n==8760)throw std::bad_alloc();if(measure&&n==1635204){++fullARGB;if(watched){H1ImageCacheFontPageStats s{};H1ImageCacheGetFontPageStats(watched,&s);releasedBeforeARGB=!s.pages&&!s.bytes&&!s.tableBytes;}}if(void *p=std::malloc(n?n:1)){++outstanding;return p;}throw std::bad_alloc();}
void *operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void *p)noexcept{if(p){--outstanding;std::free(p);}}
void operator delete[](void *p)noexcept{::operator delete(p);}
void operator delete(void *p,std::size_t)noexcept{::operator delete(p);}
void operator delete[](void *p,std::size_t)noexcept{::operator delete(p);}
extern "C" size_t pvzh1_cache_memory_pressure(size_t);
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" uint64_t pvzh1_image_pixel_bytes_loaded();
namespace {
void Check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
uint64_t Hash(const uint32_t *p,size_t n){uint64_t h=1469598103934665603ull;while(n--){auto x=*p++;for(int b=0;b<32;b+=8){h^=(x>>b)&255;h*=1099511628211ull;}}return h;}
H1ImageCacheFontPageStats Stats(MemoryImage *p){H1ImageCacheFontPageStats s{};H1ImageCacheGetFontPageStats(p,&s);return s;}
void Drain(){for(unsigned i=0;i<4096&&pvzh1_cache_memory_pressure((size_t)-1);++i){}}
void Cold(){pvzh1_cache_complete_draw_batch();Drain();}
Reanimation *Selector(){auto r=gLawnApp->AddReanimation(.5f,.5f,0,ReanimationType::REANIM_SELECTOR_SCREEN);r->PlayReanim("anim_open",ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD,0,30);r->AssignRenderGroupToPrefix("flower",RENDER_GROUP_HIDDEN);r->AssignRenderGroupToPrefix("leaf",RENDER_GROUP_HIDDEN);r->AssignRenderGroupToTrack("SelectorScreen_BG",1);int start,count;r->GetFramesForLayer("anim_sign",start,count);r->mFrameBasePose=start+count-1;r->mAnimTime=.65f;return r;}
void Free(Reanimation *p){gLawnApp->mEffectSystem->mReanimationHolder->mReanimations.DataArrayFree(p);}
uint64_t Draw(Reanimation *r){auto screen=gLawnApp->mGLInterface->GetScreenImage();Graphics g(screen);g.SetLinearBlend(true);g.SetColor(Color(8,16,24));g.FillRect(0,0,800,600);r->DrawRenderGroup(&g,1);r->DrawRenderGroup(&g,RENDER_GROUP_NORMAL);return Hash(screen->GetBits(),800u*600u);}
}
extern "C" void pvzh1_texture_page_probe(){
 static bool done=false;if(done)return;done=true;
 auto r=Selector();Check(!r->mDefinition->mReanimAtlas,"Selector did not use original NO_ATLAS path");
 auto source=static_cast<MemoryImage*>(r->GetCurrentTrackImage("SelectorScreen_BG_Right"));Check(source&&source->mWidth==730&&source->mHeight==560,"actual Selector Right source changed");
 const auto original=Draw(r);Check(source->mBits!=nullptr,"first original startup sanding was changed");std::vector<uint32_t> expected(source->mBits.get(),source->mBits.get()+408800);
 // Exercise the actual Selector definition retirement/reload used on menu return.
 Free(r);auto &def=gReanimatorDefArray[(unsigned)ReanimationType::REANIM_SELECTOR_SCREEN];ReanimationFreeDefinition(&def);Check(!def.mTracks.tracks&&def.mTracks.count==0,"Selector definition was not reset");r=Selector();Check(source==r->GetCurrentTrackImage("SelectorScreen_BG_Right"),"original shared image lifetime changed on definition reload");
 Cold();Check(!source->mBits&&!Stats(source).pages,"completed old source was not evicted");fullARGB=0;measure=true;const auto paged=Draw(r);measure=false;
 Check(original==paged,"actual Selector NO_ATLAS screen pixels changed after sanded reload");Check(fullARGB==0&&!source->mBits&&Stats(source).pages==400&&Stats(source).bytes==1635200&&Stats(source).maxPageBytes==4096,"actual sanded Right did not use 400 pages without full ARGB");
 const uint32_t *const *view=nullptr;Check(H1ImageCachePrepareTexturePages(source,&view)&&view,"actual sanded texture view missing");for(size_t i=0;i<expected.size();++i)Check(view[i>>10][i&1023]==expected[i],"paged original sanding changed full RGBA including transparent RGB");
 auto read=pvzh1_image_pixel_bytes_loaded();Check(Draw(r)==original&&pvzh1_image_pixel_bytes_loaded()==read,"warm Selector pages reread files or changed pixels");Drain();Check(Stats(source).pages==400,"current raster view pages were evicted");Cold();Check(!Stats(source).pages,"completed raster pages were not evicted");
 for(auto fault:{std::pair<int,int>{0,-1},{4,-1},{-1,3}}){const auto before=outstanding;H1ImageCacheFontPageTestFailure(fault.first,fault.second);view=nullptr;Check(!H1ImageCachePrepareTexturePages(source,&view)&&!view,"texture partial fault did not fail closed");H1ImageCacheFontPageTestFailure(-1,-1);Check(!Stats(source).pages&&!Stats(source).tableBytes&&outstanding==before,"texture partial load leaked/published pages");}
 const auto before=outstanding;failRow=true;view=nullptr;Check(!H1ImageCachePrepareTexturePages(source,&view)&&!view,"sanding scratch allocation failure did not fail closed");failRow=false;Check(!Stats(source).pages&&!Stats(source).tableBytes&&outstanding==before,"sanding scratch failure leaked pages");
 Check(H1ImageCachePrepareTexturePages(source,&view),"texture recipe failed to reload after injected failures");for(size_t i=0;i<expected.size();++i)Check(view[i>>10][i&1023]==expected[i],"reloaded recipe lost exact RGBA");
 fullARGB=0;releasedBeforeARGB=false;watched=source;measure=true;auto raw=source->GetBits();measure=false;watched=nullptr;Check(fullARGB==1&&releasedBeforeARGB&&!Stats(source).pages&&raw[408800]==MEMORYCHECK_ID&&!std::memcmp(raw,expected.data(),expected.size()*4),"generic GetBits fallback did not preserve original sanding/clear pages first");auto held=raw;view=nullptr;Check(!H1ImageCachePrepareTexturePages(source,&view)&&!view&&source->mBits.get()==held,"provider invalidated an existing contiguous loan");
 Free(r);std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"texture-page-verification.json");out<<"{\"test_only\":true,\"actual_selector_definition_reload\":true,\"original_no_atlas_matrix_path\":true,\"first_startup_sanding_unchanged\":true,\"baseline_screen_hash\":"<<original<<",\"paged_screen_hash\":"<<paged<<",\"right_pages\":400,\"right_bytes\":1635200,\"max_page_bytes\":4096,\"actual_right_full_argb_allocations\":0,\"all_408800_sanded_rgba_pixels_equal\":true,\"warm_no_file_reads\":true,\"current_pin_old_epoch_reclaim\":true,\"three_partial_load_failures_balanced\":true,\"sanding_scratch_failure_balanced\":true,\"generic_getbits_clears_pages_and_preserves_rgba\":true,\"existing_raw_loan_preserved\":true,\"all_passed\":true}\n";
 std::fprintf(stderr,"ACTUAL SELECTOR SANDED RELOAD/400-PAGE SAMPLER/FULL RGBA/FAULT CHECKS PASSED\n");
}
