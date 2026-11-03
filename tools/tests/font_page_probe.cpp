#include "LawnApp.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/ImageFont.h"
#include "h1_resources.h"
#include <set>
#include <iomanip>
#include "graphics/MemoryImageCache.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
#include <new>
using namespace Sexy;
static int64_t outstanding=0;static unsigned argbArrays=0;static bool measure=false;static MemoryImage *watchSource=nullptr;static bool pageReleaseObserved=false;
void *operator new(std::size_t n){if(void *p=std::malloc(n?n:1)){++outstanding;if(measure&&n==1109204){++argbArrays;if(watchSource){H1ImageCacheFontPageStats s{};H1ImageCacheGetFontPageStats(watchSource,&s);pageReleaseObserved=!s.pages&&!s.bytes&&!s.tableBytes;}}return p;}throw std::bad_alloc();}
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
void Clear(MemoryImage *p){H1ImageCacheFontPageStats s{};H1ImageCacheGetFontPageStats(p,&s);if(s.pages)(void)p->GetBits();p->mBits.reset();p->mColorIndices.reset();p->mColorTable.reset();p->mNativeAlphaData.reset();p->mRLAlphaData.reset();p->mRLAdditiveData.reset();p->mBitsChanged=false;}
void Drain(){for(unsigned i=0;i<4096&&pvzh1_cache_memory_pressure((size_t)-1);++i){}}
unsigned drawnDelta=0;bool sourceDrawn=false;
uint64_t Draw(MemoryImage *source,bool alpha,Color color,int mode,const Rect& rect=Rect(991,0,170,59)){MemoryImage dest(gLawnApp);dest.Create(800,180);dest.SetImageMode(alpha,alpha);Graphics g(&dest);g.SetColor(Color(31,47,59,alpha?90:255));g.FillRect(0,0,800,180);source->mDrawn=false;const unsigned before=dest.mBitsChangedCount;dest.MemoryImage::Blt(source,3,7,rect,color,mode,false);drawnDelta=dest.mBitsChangedCount-before;sourceDrawn=source->mDrawn;return Hash(dest.GetBits(),800u*180u);}
unsigned Glyphs(){
 ImageFont original(gLawnApp,"data/DwarvenTodcraft36GreenInset.txt");Check(original.mFontData&&original.mFontData->mInitialized,"actual Dwarf36 descriptor did not load");
 struct Case{double scale;int point;Color color;bool alpha,multi;};
 const Case cases[]={{1,0,Color::White,false,false},{1,0,Color(80,190,220),false,false},{1,0,Color(220,90,40,91),false,false},{1,0,Color::White,false,true},{1,0,Color(230,80,160,130),true,true},{.75,0,Color::White,false,true},{1.5,0,Color(90,200,80),false,false},{1,3,Color::White,false,false}};
 std::vector<MemoryImage*> images;std::vector<std::vector<uint32_t>> pixels;
 for(auto &layer:original.mFontData->mFontLayerList){MemoryImage *image=layer.mImage;if(!image)continue;images.push_back(image);auto p=image->GetBits();pixels.emplace_back(p,p+(size_t)image->mWidth*image->mHeight);}
 Check(images.size()==1&&pixels[0].size()==277300,"Dwarf36 original layer layout changed");
 auto render=[&](const Case& c){ImageFont font(original);font.mActiveLayerList.clear();font.mActiveListValid=false;MemoryImage target(gLawnApp);target.Create(800,240);target.SetImageMode(c.alpha,c.alpha);Graphics g(&target);g.SetColor(Color(21,37,59,c.alpha?90:255));g.FillRect(0,0,800,240);font.SetPointSize(original.mPointSize+c.point);font.SetScale(c.scale);font.Prepare();int width=0;font.DrawStringEx(&g,10,90,"THE ZOMBIES ARE COMING! 123 AV WA",c.color,nullptr,&width);if(c.multi){font.DrawStringEx(&g,10,170,"color fade",Color(120,210,60,90),nullptr,nullptr);font.DrawStringEx(&g,290,170,"RGBA",Color(220,90,170),nullptr,nullptr);}return Hash(target.GetBits(),800u*240u)^(unsigned)width;};
 unsigned count=0;
 for(auto &c:cases){
  for(size_t i=0;i<images.size();++i){Clear(images[i]);auto bits=std::make_unique<uint32_t[]>(pixels[i].size()+1);std::memcpy(bits.get(),pixels[i].data(),pixels[i].size()*4);bits[pixels[i].size()]=MEMORYCHECK_ID;images[i]->mBits=std::move(bits);}
  const auto baseline=render(c);
  for(auto image:images){auto path=image->mFilePath;Check(H1ImageCacheDescribe(image,path,true,0xffffff),"actual font logical-path descriptor reload failed");}
  argbArrays=0;measure=true;const auto candidate=render(c);measure=false;Check(baseline==candidate,"actual Dwarf36 glyph pixels/width changed");
  if(c.scale==1&&c.point==0){H1ImageCacheFontPageStats st{};H1ImageCacheGetFontPageStats(images[0],&st);Check(argbArrays==0&&st.pages==271&&!images[0]->mBits,"1x Dwarf36 did not retain original RGBA pages");}
  else Check(argbArrays>0&&images[0]->mBits,"scaled Dwarf36 did not exercise original GetBits fallback");
  ++count;
 }
 return count;
}
}
extern "C" void pvzh1_font_page_probe(){
 static bool done=false;if(done)return;done=true;
 std::unique_ptr<GLImage> owner(gLawnApp->GetImage("data/dwarventodcraft36greeninset",true));auto source=owner.get();Check(source&&source->mWidth==4700&&source->mHeight==59,"actual Dwarf36 source dimensions changed");
 auto path=source->mFilePath;auto original=source->GetBits();std::vector<uint32_t> expected(original,original+277300);std::set<uint32_t> colors(expected.begin(),expected.end());Check(colors.size()==6226,"actual complete-RGBA color count changed");
 auto reset=[&](){Check(H1ImageCacheDescribe(source,path,true,0xffffff),"Dwarf36 descriptor reset failed");Check(!H1ImageCachePrepareAlphaPalette(source),"Dwarf36 >256 colors unexpectedly accepted a palette");};
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"font-page-verification.json");out<<"{\"test_only\":true,\"actual_dwarf36_rgba_colors\":6226,\"direct_blits\":[";unsigned cases=0;
 for(bool alpha:{false,true})for(Color color:{Color::White,Color(180,70,230,91)})for(int mode:{0,1}){
  Clear(source);(void)source->GetBits();auto baseline=Draw(source,alpha,color,mode);const auto baselineDelta=drawnDelta;const auto baselineDrawn=sourceDrawn;reset();
  argbArrays=0;measure=true;auto candidate=Draw(source,alpha,color,mode);measure=false;H1ImageCacheFontPageStats s{};Check(H1ImageCacheGetFontPageStats(source,&s),"font page stats missing");
  Check(baseline==candidate&&drawnDelta==baselineDelta&&sourceDrawn==baselineDrawn,"original page/tile blitter changed RGBA pixels or draw side effects");Check(!argbArrays&&!source->mBits&&s.pages==271&&s.bytes==1109200&&s.tableBytes==271*sizeof(void*)&&s.maxPageBytes==4096,"page geometry or absence of full ARGB is wrong");
  if(cases)out<<',';out<<"{\"alpha_dest\":"<<(alpha?"true":"false")<<",\"mode\":"<<mode<<",\"hash\":"<<baseline<<",\"equal\":true,\"full_argb_arrays\":0}";out.flush();++cases;
 }
 for(unsigned alpha:{0u,255u}){size_t at=0;while(at<expected.size()&&((expected[at]>>24)!=alpha||!(expected[at]&0xffffffu)))++at;Check(at<expected.size(),"full-image mode crop fixture missing");Rect crop((int)(at%4700),(int)(at/4700),1,1);Clear(source);(void)source->GetBits();const auto base=Draw(source,false,Color::White,1,crop);reset();const auto next=Draw(source,false,Color::White,1,crop);Check(base==next&&!source->mBits,"tile local alpha classification changed original additive rounding");}
 auto stats=[&](){H1ImageCacheFontPageStats s{};H1ImageCacheGetFontPageStats(source,&s);return s;};
 MemoryImage target(gLawnApp);target.Create(800,180);target.SetImageMode(false,false);Graphics g(&target);g.SetColor(Color(31,47,59));g.FillRect(0,0,800,180);
 const Rect rect(991,0,170,59);auto perform=[&](){return H1ImageCacheBltFontGlyph(source,&target,3,7,rect,Color::White,0);};
 reset();Check(perform(),"direct font glyph page load failed");auto loaded=pvzh1_image_pixel_bytes_loaded();Check(perform()&&pvzh1_image_pixel_bytes_loaded()==loaded,"warm pages reread source");
 Drain();Check(stats().pages==271,"current font pages were evicted");pvzh1_cache_complete_draw_batch();Drain();Check(!stats().pages&&!stats().tableBytes,"old font pages were not reclaimed");Check(perform(),"font pages did not reload");
 for(auto fault:{std::pair<int,int>{0,-1},{1,-1},{4,-1},{-1,0},{-1,3}}){
  reset();const auto hash=Hash(target.GetBits(),800u*180u);const auto delta=target.mBitsChangedCount;const auto before=outstanding;
  H1ImageCacheFontPageTestFailure(fault.first,fault.second);Check(!perform(),"font page fault did not fail");H1ImageCacheFontPageTestFailure(-1,-1);
  Check(!stats().pages&&!stats().bytes&&!stats().tableBytes&&outstanding==before,"partial font-page load leaked/published data");Check(Hash(target.GetBits(),800u*180u)==hash&&target.mBitsChangedCount==delta,"page failure wrote destination or changed flags");
 }
 Check(perform(),"font page recovery failed");const auto resident=stats();const auto before=outstanding;const auto hash=Hash(target.GetBits(),800u*180u);const auto delta=target.mBitsChangedCount;
 H1ImageCacheFontPageTestFailure(0,-1);Check(!perform(),"warm glyph tile allocation failure did not fail");H1ImageCacheFontPageTestFailure(-1,-1);
 Check(!stats().pages&&!stats().bytes&&!stats().tableBytes&&outstanding==before-(int64_t)resident.pages-1,"tile failure did not clear all pages/table or leaked temporary image");Check(Hash(target.GetBits(),800u*180u)==hash&&target.mBitsChangedCount==delta,"tile failure partly wrote destination");
 Check(perform(),"reload after tile failure failed");argbArrays=0;pageReleaseObserved=false;watchSource=source;measure=true;auto raw=source->GetBits();measure=false;watchSource=nullptr;
 Check(argbArrays==1&&pageReleaseObserved&&!stats().pages&&raw[277300]==MEMORYCHECK_ID&&!std::memcmp(raw,expected.data(),expected.size()*4),"generic GetBits did not release pages before exact full-ARGB expansion");
 auto held=raw;const auto sample=raw[10];Check(!perform()&&source->mBits.get()==held&&held[10]==sample,"glyph path invalidated existing contiguous pixel loan");
 reset();const auto destination=Hash(target.GetBits(),800u*180u);Check(!H1ImageCacheBltFontGlyph(source,source,0,0,rect,Color::White,0),"self glyph blit accepted");Check(!H1ImageCacheBltFontGlyph(source,&target,0,0,Rect(0,0,300,59),Color::White,0),"unbounded tile accepted");Check(!stats().pages&&Hash(target.GetBits(),800u*180u)==destination,"rejected glyph path allocated pages or wrote destination");
 H1ImageCacheAfterSanding(source,[](Image*){});Check(!perform(),"sanded font glyph accepted");reset();Check(H1ImageCacheSetAlpha(source,path,1,1),"matching alpha-grid setup failed");Check(!perform(),"grid font glyph accepted");
 const auto glyphs=Glyphs();
 out<<"],\"case_count\":"<<cases<<",\"actual_dwarf36_glyph_cases\":"<<glyphs<<",\"rgba_page_bytes\":1109200,\"page_count\":271,\"max_page_bytes\":4096,\"opaque_and_transparent_crop_full_source_mode_exact\":true,\"drawn_and_bitschanged_side_effects_equal\":true,\"cross_row_and_page_copy_exact\":true,\"current_pages_pinned\":true,\"old_pages_reclaimed_and_reloaded\":true,\"warm_reads_unchanged\":true,\"five_partial_page_failures_balanced\":true,\"tile_failure_no_writes_and_cleans_pages\":true,\"getbits_frees_pages_before_full_argb\":true,\"all_full_rgba_and_canary_equal\":true,\"existing_raw_loan_preserved\":true,\"self_oversize_grid_sanded_rejected\":true,\"all_passed\":true}\n";
 std::fprintf(stderr,"DWARF36 PAGED RGBA/ORIGINAL GLYPH BLITTER/FALLBACK/FAILURE CHECKS PASSED\n");
}
