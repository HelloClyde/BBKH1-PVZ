#include "LawnApp.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImageCache.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <new>
using namespace Sexy;
static int64_t outstanding=0;static unsigned argbArrays=0;static bool measure=false;
void *operator new(std::size_t n){if(void *p=std::malloc(n?n:1)){++outstanding;if(measure&&n==900508)++argbArrays;return p;}throw std::bad_alloc();}
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
uint64_t Hash(const uint32_t *p,size_t n){uint64_t h=1469598103934665603ull;while(n--){uint32_t x=*p++;for(int s=0;s<32;s+=8){h^=(x>>s)&255;h*=1099511628211ull;}}return h;}
void Clear(MemoryImage *p){p->mBits.reset();p->mColorIndices.reset();p->mColorTable.reset();p->mNativeAlphaData.reset();p->mRLAlphaData.reset();p->mRLAdditiveData.reset();p->mBitsChanged=false;}
void Drain(){for(unsigned i=0;i<4096&&pvzh1_cache_memory_pressure((size_t)-1);++i){}}
uint64_t Draw(MemoryImage *source,bool alpha,Color color,int mode){MemoryImage dest(gLawnApp);dest.Create(800,180);dest.SetImageMode(alpha,alpha);Graphics g(&dest);g.SetColor(Color(31,47,59,alpha?90:255));g.FillRect(0,0,800,180);dest.MemoryImage::Blt(source,3,7,Rect(0,0,797,54),color,mode,false);return Hash(dest.GetBits(),800u*180u);}
}
extern "C" void pvzh1_alpha_loader_probe(){
 static bool done=false;if(done)return;done=true;
 std::unique_ptr<GLImage> image(gLawnApp->GetImage("data/briannetod32outline",true));auto source=image.get();Check(source&&source->mWidth==4169&&source->mHeight==54,"actual A8 font dimensions changed");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"alpha-loader-verification.json");out<<"{\"test_only\":true,\"production_loader\":true,\"direct_blits\":[";unsigned cases=0;
 for(bool alpha:{false,true})for(Color color:{Color::White,Color(180,70,230,91)})for(int mode:{0,1}){
  Clear(source);(void)source->GetBits();auto baseline=Draw(source,alpha,color,mode);Clear(source);
  argbArrays=0;measure=true;auto candidate=Draw(source,alpha,color,mode);measure=false;
  Check(baseline==candidate,"production A8 direct normal/additive blit changed pixels");Check(!argbArrays&&!source->mBits&&source->mColorIndices&&source->mColorTable,"A8 blit materialized full ARGB");
  if(cases)out<<',';out<<"{\"alpha_dest\":"<<(alpha?"true":"false")<<",\"mode\":"<<mode<<",\"hash\":"<<baseline<<",\"equal\":true,\"argb_arrays\":0}";out.flush();++cases;
 }
 Clear(source);Check(H1ImageCachePrepareAlphaPalette(source),"direct A8 production preparation failed");auto loaded=pvzh1_image_pixel_bytes_loaded();auto palette=source->mColorIndices.get();Check(H1ImageCachePrepareAlphaPalette(source)&&source->mColorIndices.get()==palette&&pvzh1_image_pixel_bytes_loaded()==loaded,"warm palette reread or replaced indices");
 Drain();Check(source->mColorIndices&&source->mColorTable,"current palette was evicted");pvzh1_cache_complete_draw_batch();Drain();Check(!source->mColorIndices&&!source->mColorTable,"completed palette was not reclaimed");
 Check(H1ImageCachePrepareAlphaPalette(source),"palette did not reload after pressure");
 for(auto fault:{std::pair<int,int>{0,-1},{1,-1},{-1,0},{-1,3}}){Clear(source);H1ImageCacheAlphaTestFailure(fault.first,fault.second);const auto count=outstanding;Check(!H1ImageCachePrepareAlphaPalette(source),"alpha fault injection did not fail");H1ImageCacheAlphaTestFailure(-1,-1);Check(!source->mBits&&!source->mColorIndices&&!source->mColorTable,"partial alpha load published source representation");Check(outstanding==count,"partial alpha load leaked C++ allocation");}
 Check(H1ImageCachePrepareAlphaPalette(source),"palette did not recover after failures");std::vector<uint32_t> expected(4169u*54u);for(size_t i=0;i<expected.size();++i)expected[i]=source->mColorTable[source->mColorIndices[i]];
 argbArrays=0;measure=true;auto raw=source->GetBits();measure=false;Check(argbArrays==1&&!source->mColorIndices&&!source->mColorTable&&raw[expected.size()]==MEMORYCHECK_ID&&!std::memcmp(raw,expected.data(),expected.size()*4),"GetBits palette expansion did not preserve exact ARGB/canary");
 auto held=raw;auto pixel=raw[10];Check(!H1ImageCachePrepareAlphaPalette(source)&&source->mBits.get()==held&&held[10]==pixel,"palette loader invalidated existing raw loan");
 Clear(source);source->MemoryImage::NormalBlt(source,0,0,Rect(0,0,4,4),Color::White);Check(source->mBits&&!source->mColorIndices&&!source->mColorTable,"self-blit introduced a palette/borrow conflict");
 MemoryImage custom(gLawnApp);Check(H1ImageCacheDescribe(&custom,"data/briannetod32outline",true,0x2468ac),"custom compose A8 metadata failed");Check(H1ImageCachePrepareAlphaPalette(&custom),"custom compose palette failed");for(unsigned a=0;a<256;++a)Check(custom.mColorTable[a]==((a<<24)|0x2468acu),"compose-color table changed RGB including alpha0");
 auto verifyReject=[&](const char *path,bool autoAlpha){MemoryImage other(gLawnApp);Check(H1ImageCacheDescribe(&other,path,autoAlpha,0xffffff),"negative source metadata missing");Check(!H1ImageCachePrepareAlphaPalette(&other)&&!other.mBits&&!other.mColorIndices&&!other.mColorTable,"nonmask source was accepted as A8");};
 verifyReject("images/background3",false);verifyReject("images/seedbank",true);
 MemoryImage grid(gLawnApp);Check(H1ImageCacheDescribe(&grid,"data/briannetod32",true,0xffffff),"grid source metadata missing");Check(H1ImageCacheSetAlpha(&grid,"data/briannetod32outline",1,1),"matching alpha grid setup failed");Check(!H1ImageCachePrepareAlphaPalette(&grid),"alpha-grid source was accepted");
 MemoryImage sanded(gLawnApp);Check(H1ImageCacheDescribe(&sanded,"data/briannetod32",true,0xffffff),"sanding source metadata missing");Check(H1ImageCacheAfterSanding(&sanded,[](Image*){}),"sanding marker failed");Check(!H1ImageCachePrepareAlphaPalette(&sanded),"sanded source was accepted");
 MemoryImage dynamic(gLawnApp);dynamic.Create(16,16);Check(!H1ImageCachePrepareAlphaPalette(&dynamic),"dynamic source was accepted");
 out<<"],\"case_count\":"<<cases<<",\"current_palette_pinned\":true,\"completed_palette_reclaimed_and_reloaded\":true,\"warm_reads_unchanged\":true,\"four_partial_failure_cleanups_balanced\":true,\"getbits_expands_exact_argb_once\":true,\"existing_raw_loan_preserved\":true,\"self_blit_skips_preparation\":true,\"compose_color_all_256_entries_exact\":true,\"rgb_rgba_grid_sanded_dynamic_rejected\":true,\"all_passed\":true}\n";
 std::fprintf(stderr,"ALPHA PRODUCTION LOADER/PRESSURE/FAILURE/FALLBACK CHECKS PASSED\n");
}
