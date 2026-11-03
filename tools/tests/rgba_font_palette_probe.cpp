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
static int64_t outstanding=0;static unsigned argbArrays=0;static bool measure=false;
void *operator new(std::size_t n){if(void *p=std::malloc(n?n:1)){++outstanding;if(measure&&n==934204)++argbArrays;return p;}throw std::bad_alloc();}
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
unsigned Glyphs(){
 ImageFont original(gLawnApp,"data/HouseofTerror28.txt");Check(original.mFontData&&original.mFontData->mInitialized,"actual House28 descriptor did not load");
 struct Case{double scale;int point;Color color;bool alpha,multi;};
 const Case cases[]={{1,0,Color::White,false,false},{1,0,Color(80,190,220),false,false},{1,0,Color(220,90,40,91),false,false},{1,0,Color::White,false,true},{1,0,Color(230,80,160,130),true,true},{.75,0,Color::White,false,true},{1.5,0,Color(90,200,80),false,false},{1,3,Color::White,false,false}};
 std::vector<MemoryImage*> images;std::vector<std::vector<uint32_t>> pixels;
 for(auto &layer:original.mFontData->mFontLayerList){MemoryImage *image=layer.mImage;if(!image)continue;images.push_back(image);auto p=image->GetBits();pixels.emplace_back(p,p+(size_t)image->mWidth*image->mHeight);}
 Check(images.size()==1&&pixels[0].size()==233550,"House28 original layer layout changed");
 auto render=[&](const Case& c){ImageFont font(original);font.mActiveLayerList.clear();font.mActiveListValid=false;MemoryImage target(gLawnApp);target.Create(800,240);target.SetImageMode(c.alpha,c.alpha);Graphics g(&target);g.SetColor(Color(21,37,59,c.alpha?90:255));g.FillRect(0,0,800,240);font.SetPointSize(original.mPointSize+c.point);font.SetScale(c.scale);font.Prepare();int width=0;font.DrawStringEx(&g,10,90,"THE ZOMBIES ARE COMING! 123 AV WA",c.color,nullptr,&width);if(c.multi){font.DrawStringEx(&g,10,170,"color fade",Color(120,210,60,90),nullptr,nullptr);font.DrawStringEx(&g,290,170,"RGBA",Color(220,90,170),nullptr,nullptr);}return Hash(target.GetBits(),800u*240u)^(unsigned)width;};
 unsigned count=0;
 for(auto &c:cases){
  for(size_t i=0;i<images.size();++i){Clear(images[i]);auto bits=std::make_unique<uint32_t[]>(pixels[i].size()+1);std::memcpy(bits.get(),pixels[i].data(),pixels[i].size()*4);bits[pixels[i].size()]=MEMORYCHECK_ID;images[i]->mBits=std::move(bits);}
  const auto baseline=render(c);
  for(auto image:images){auto path=image->mFilePath;Check(H1ImageCacheDescribe(image,path,true,0xffffff),"actual font logical-path descriptor reload failed");}
  argbArrays=0;measure=true;const auto candidate=render(c);measure=false;Check(baseline==candidate,"actual House28 glyph pixels/width changed");
  if(c.scale==1&&c.point==0)Check(argbArrays==0&&images[0]->mColorIndices&&!images[0]->mBits,"1x House28 did not retain production palette");
  else Check(argbArrays>0&&images[0]->mBits,"scaled House28 did not exercise original GetBits fallback");
  ++count;
 }
 return count;
}
void Reject257(){
 const std::string previous=h1_resources_get_root();std::filesystem::path root=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"synthetic-resources";std::filesystem::create_directories(root/"data");
 const uint32_t header[]={0x31493148u,257,1,4,0,1028}; // H1I1, 257 exact RGBA colors.
 {std::ofstream file(root/"data/colors257.h1i",std::ios::binary);file.write((const char*)header,sizeof(header));for(unsigned i=0;i<257;++i){const unsigned char rgba[]={(unsigned char)i,(unsigned char)(i>>8),17,(unsigned char)(i?255:0)};file.write((const char*)rgba,4);}}
 h1_resources_set_root(root.string().c_str());
 {MemoryImage image(gLawnApp);Check(H1ImageCacheDescribe(&image,"data/colors257",false,0xffffff),"257-color fixture metadata failed");const auto before=outstanding;Check(!H1ImageCachePrepareAlphaPalette(&image),"257 exact colors were incorrectly quantized");Check(!image.mBits&&!image.mColorIndices&&!image.mColorTable&&outstanding==before,"257-color rejection leaked or published a representation");Check(!H1ImageCachePrepareAlphaPalette(&image),"257-color rejected source unexpectedly became eligible");auto raw=image.GetBits();for(unsigned i=0;i<257;++i){uint32_t expected=((i?255u:0u)<<24)|((i&255u)<<16)|((i>>8)<<8)|17;Check(raw[i]==expected,"257-color fallback changed transparent RGB or colors");}}
 h1_resources_set_root(previous.c_str());
}
}
extern "C" void pvzh1_rgba_font_probe(){
 static bool done=false;if(done)return;done=true;
 std::unique_ptr<GLImage> image(gLawnApp->GetImage("data/houseofterror28",true));auto source=image.get();Check(source&&source->mWidth==4325&&source->mHeight==54,"actual RGBA House28 font dimensions changed");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"rgba-font-palette-verification.json");out<<"{\"test_only\":true,\"production_loader\":true,\"direct_blits\":[";unsigned cases=0;
 for(bool alpha:{false,true})for(Color color:{Color::White,Color(180,70,230,91)})for(int mode:{0,1}){
  Clear(source);(void)source->GetBits();auto baseline=Draw(source,alpha,color,mode);Clear(source);
  argbArrays=0;measure=true;auto candidate=Draw(source,alpha,color,mode);measure=false;
  Check(baseline==candidate,"production RGBA direct normal/additive blit changed pixels");Check(!argbArrays&&!source->mBits&&source->mColorIndices&&source->mColorTable,"RGBA font blit materialized full ARGB");
  if(cases)out<<',';out<<"{\"alpha_dest\":"<<(alpha?"true":"false")<<",\"mode\":"<<mode<<",\"hash\":"<<baseline<<",\"equal\":true,\"argb_arrays\":0}";out.flush();++cases;
 }
 Clear(source);Check(H1ImageCachePrepareAlphaPalette(source),"direct A8 production preparation failed");auto loaded=pvzh1_image_pixel_bytes_loaded();auto palette=source->mColorIndices.get();Check(H1ImageCachePrepareAlphaPalette(source)&&source->mColorIndices.get()==palette&&pvzh1_image_pixel_bytes_loaded()==loaded,"warm palette reread or replaced indices");
 Drain();Check(source->mColorIndices&&source->mColorTable,"current palette was evicted");pvzh1_cache_complete_draw_batch();Drain();Check(!source->mColorIndices&&!source->mColorTable,"completed palette was not reclaimed");
 Check(H1ImageCachePrepareAlphaPalette(source),"palette did not reload after pressure");
 for(auto fault:{std::pair<int,int>{0,-1},{1,-1},{-1,0},{-1,3}}){Clear(source);H1ImageCacheFontPaletteTestFailure(fault.first,fault.second);const auto count=outstanding;Check(!H1ImageCachePrepareAlphaPalette(source),"alpha fault injection did not fail");H1ImageCacheFontPaletteTestFailure(-1,-1);Check(!source->mBits&&!source->mColorIndices&&!source->mColorTable,"partial alpha load published source representation");Check(outstanding==count,"partial alpha load leaked C++ allocation");}
 Check(H1ImageCachePrepareAlphaPalette(source),"palette did not recover after failures");std::vector<uint32_t> expected(4325u*54u);for(size_t i=0;i<expected.size();++i)expected[i]=source->mColorTable[source->mColorIndices[i]];
 argbArrays=0;measure=true;auto raw=source->GetBits();measure=false;Check(argbArrays==1&&!source->mColorIndices&&!source->mColorTable&&raw[expected.size()]==MEMORYCHECK_ID&&!std::memcmp(raw,expected.data(),expected.size()*4),"GetBits palette expansion did not preserve exact ARGB/canary");
 auto held=raw;auto pixel=raw[10];Check(!H1ImageCachePrepareAlphaPalette(source)&&source->mBits.get()==held&&held[10]==pixel,"palette loader invalidated existing raw loan");
 Clear(source);source->MemoryImage::NormalBlt(source,0,0,Rect(0,0,4,4),Color::White);Check(source->mBits&&!source->mColorIndices&&!source->mColorTable,"self-blit introduced a palette/borrow conflict");
 std::set<uint32_t> colors(expected.begin(),expected.end());Check(colors.size()==43,"House28 exact full-RGBA color count changed");
 auto verifyReject=[&](const char *path,bool autoAlpha){MemoryImage other(gLawnApp);Check(H1ImageCacheDescribe(&other,path,autoAlpha,0xffffff),"negative source metadata missing");Check(!H1ImageCachePrepareAlphaPalette(&other)&&!other.mBits&&!other.mColorIndices&&!other.mColorTable,"nonmask source was accepted as A8");};
 verifyReject("images/background3",false);verifyReject("images/seedbank",true);
 MemoryImage grid(gLawnApp);Check(H1ImageCacheDescribe(&grid,"data/houseofterror28",true,0xffffff),"grid source metadata missing");Check(H1ImageCacheSetAlpha(&grid,"data/houseofterror28",1,1),"matching alpha grid setup failed");Check(!H1ImageCachePrepareAlphaPalette(&grid),"alpha-grid source was accepted");
 MemoryImage sanded(gLawnApp);Check(H1ImageCacheDescribe(&sanded,"data/houseofterror28",true,0xffffff),"sanding source metadata missing");Check(H1ImageCacheAfterSanding(&sanded,[](Image*){}),"sanding marker failed");Check(!H1ImageCachePrepareAlphaPalette(&sanded),"sanded source was accepted");
 MemoryImage dynamic(gLawnApp);dynamic.Create(16,16);Check(!H1ImageCachePrepareAlphaPalette(&dynamic),"dynamic source was accepted");
 const auto glyphs=Glyphs();Reject257();
 out<<"],\"case_count\":"<<cases<<",\"actual_house28_glyph_cases\":"<<glyphs<<",\"current_palette_pinned\":true,\"completed_palette_reclaimed_and_reloaded\":true,\"warm_reads_unchanged\":true,\"four_partial_failure_cleanups_balanced\":true,\"getbits_expands_exact_argb_once\":true,\"existing_raw_loan_preserved\":true,\"self_blit_skips_preparation\":true,\"all_43_full_rgba_colors_exact\":true,\"nonfont_rgba_rgb_grid_sanded_dynamic_rejected\":true,\"color257_rejected_without_loss_or_leak\":true,\"all_passed\":true}\n";
 std::fprintf(stderr,"RGBA FONT PRODUCTION LOADER/PRESSURE/FAILURE/FALLBACK CHECKS PASSED\n");
}
