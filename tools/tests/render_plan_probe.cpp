#include "LawnApp.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/MemoryImageCache.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
using namespace Sexy;
namespace {
void Check(bool value,const char* message){if(!value){fprintf(stderr,"PLAN PIXEL FAILURE: %s\n",message);throw std::runtime_error(message);}}
void Background(MemoryImage* image,unsigned seed){auto*p=image->GetBits();for(size_t i=0,n=(size_t)image->mWidth*image->mHeight;i<n;++i)p[i]=0xff000000u|((i*0x13759bu+seed)&0xffffffu);image->BitsChanged();}
std::vector<uint32_t> Pixels(MemoryImage* image){auto*p=image->GetBits();return {p,p+(size_t)image->mWidth*image->mHeight};}
void Occluder(MemoryImage* target,int kind){if(kind)H1FillRect(target,Rect(51,43,137,109),Color(29,107,193,117),0);}
}
extern "C" void pvzh1_render_plan_probe(){
 auto*device=gLawnApp->mGLInterface.get();auto saved=std::move(device->mScreenImage);device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,800,600,360,270);auto* target=H1ScreenTarget(device->GetScreenImage());unsigned pixelCases=0,guardCases=0;uint32_t totalHits=0,copyPixels=0,partialPixels=0,skipPixels=0,peak=0;
 // Exercise the opaque-target fill shortcut against MemoryImage's complete
 // general-alpha oracle, including pixels that violate the usual alpha=255.
 for(unsigned alpha=0;alpha<256;++alpha)for(bool opaque:{false,true}){
  std::vector<uint32_t> initial(360*270);for(unsigned i=0;i<initial.size();++i)initial[i]=((opaque?255u:i%256)<<24)|((i*0x315793u+alpha)&0xffffffu);
  const Rect rectangle(7,11,341,247);const Color color(41,157,239,alpha);
  std::memcpy(target->GetBits(),initial.data(),initial.size()*4);target->MemoryImage::FillRect(rectangle,color,0);auto expected=Pixels(target);
  std::memcpy(target->GetBits(),initial.data(),initial.size()*4);unsigned before=target->mBitsChangedCount;H1FillRect(target,rectangle,color,0);
  Check(expected==Pixels(target),"screen fill differs at alpha/boundary");Check(target->mBitsChangedCount==before+1,"screen fill notification differs");
 }
 {
 GLImage base(device),right(device),left(device),center(device);Check(H1ImageCacheDescribe(&base,"reanim/selectorscreen_bg",true,0xffffff),"base missing");Check(H1ImageCacheDescribe(&right,"reanim/selectorscreen_bg_right",true,0xffffff),"right missing");Check(H1ImageCacheDescribe(&left,"reanim/selectorscreen_bg_left",true,0xffffff),"left missing");Check(H1ImageCacheDescribe(&center,"reanim/selectorscreen_bg_center",true,0xffffff),"center missing");Image* images[]={&base,&right,&left,&center};
 for(int kind=0;kind<10;++kind){
  pvzh1_render_plan_shutdown();Rect clip=kind==2?Rect(13,17,331,239):Rect(0,0,360,270);
  auto draw=[&](unsigned index){SexyTransform2D matrix;float scale=.45f;if(!index)scale*=8.f;matrix.Scale(scale,scale);float px[]={0,71,0,80},py[]={0,41,-79.8f,250};matrix.Translate(px[index]*.45f+(kind==1?17.125f:kind==3?-33.5f:0),py[index]*.45f+(kind==1?11.75f:kind==4?-71.25f:0));Rect source(0,0,images[index]->mWidth,images[index]->mHeight);
   if(kind>=7){int edge=kind==7?1:kind==8?7:13;source=Rect(edge,edge,source.mWidth-2*edge,source.mHeight-2*edge);}
   H1DrawTransformed(target,images[index],source,clip,Color::White,0,matrix,true);};
  unsigned before=pvzh1_render_plan_diagnostic[3];
  for(unsigned warm=0;warm<6;++warm){H1RenderPlanTestEnable(false);Background(target,0x213141+warm*37);for(unsigned i=0;i<4;++i){draw(i);if(i==1)Occluder(target,kind==5||kind==6);}auto expected=Pixels(target);unsigned changed=target->mBitsChangedCount;
   H1RenderPlanTestEnable(true);Background(target,0x213141+warm*37);unsigned start=target->mBitsChangedCount;for(unsigned i=0;i<4;++i){draw(i);if(i==1)Occluder(target,kind==5||kind==6);}Check(expected==Pixels(target),"real four BG replay/underlay/occluder pixels changed");Check(target->mBitsChangedCount-start==9u+(kind==5||kind==6?1u:0u)-1u,"two-triangle target BitsChanged count changed");(void)changed;++pixelCases;
  }
  Check(pvzh1_render_plan_diagnostic[3]>before,"real steady BG never hit a plan");
  Check(pvzh1_render_plan_diagnostic[1]+pvzh1_render_plan_diagnostic[23]<=512*1024,"real plan capacity exceeded bounded budget");
 }
 // Full geometry/mode/target guards must fall back even while another key is warm.
 for(int kind=0;kind<8;++kind){SexyTransform2D matrix;matrix.Scale(.45f,.45f);if(kind==0)matrix.RotateDeg(17);if(kind==1)matrix.Scale(-1,1);Color color=kind==2?Color(101,179,231,137):Color::White;int mode=kind==3?Graphics::DRAWMODE_ADDITIVE:0;Rect src=kind==4?Rect(1,0,right.mWidth-1,right.mHeight):Rect(0,0,right.mWidth,right.mHeight);unsigned oldFlags=right.mRenderFlags;if(kind==5)right.mRenderFlags|=RenderImageFlag_Repeat;
  MemoryImage offscreen(gLawnApp);if(kind==6){offscreen.Create(360,270);offscreen.SetImageMode(false,false);}MemoryImage* dest=kind==6?&offscreen:target;
  H1RenderPlanTestEnable(false);Background(dest,17);H1DrawTransformed(dest,&right,src,Rect(0,0,360,270),color,mode,matrix,true);auto expected=Pixels(dest);
  H1RenderPlanTestEnable(true);Background(dest,17);unsigned before=pvzh1_render_plan_diagnostic[3];H1DrawTransformed(dest,&right,src,Rect(0,0,360,270),color,mode,matrix,true);Check(expected==Pixels(dest),"guard fallback changed actual pixels");if(kind<7)Check(pvzh1_render_plan_diagnostic[3]==before,"unsupported mode/geometry/target used a cached plan");right.mRenderFlags=oldFlags;++guardCases;
 }
 // Re-description of the same Image address must reject its old generation.
 const auto oldRevision=H1ImageCacheStorageRevision(&right);H1ImageCacheForget(&right);Check(H1ImageCacheDescribe(&right,"reanim/selectorscreen_bg_right",true,0xffffff),"re-describe failed");SexyTransform2D matrix;matrix.Scale(.45f,.45f);unsigned before=pvzh1_render_plan_diagnostic[3];H1DrawTransformed(target,&right,Rect(0,0,right.mWidth,right.mHeight),Rect(0,0,360,270),Color::White,0,matrix,true);Check(pvzh1_render_plan_diagnostic[3]==before,"changed Source cookie hit stale plan");Check(H1ImageCacheStorageRevision(&right)!=oldRevision,"fixture failed to change source generation");++guardCases;
 totalHits=pvzh1_render_plan_diagnostic[3];copyPixels=pvzh1_render_plan_diagnostic[19];partialPixels=pvzh1_render_plan_diagnostic[21];skipPixels=pvzh1_render_plan_diagnostic[18];peak=pvzh1_render_plan_diagnostic[2];
 Check(copyPixels&&partialPixels&&skipPixels,"real BG tests missed memcpy/partial/transparent replay tiers");
 }
 // All source Image objects are already destroyed: shutdown must only free its
 // own pages and must not inspect Source/Image storage during teardown.
 pvzh1_render_plan_shutdown();Check(pvzh1_render_plan_diagnostic[0]==0&&pvzh1_render_plan_diagnostic[1]==0,"shutdown after Image destruction retained plans");device->mScreenImage=std::move(saved);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"render-plan-verification.json");out<<"{\"passed\":true,\"actual_BG_pixel_cases\":"<<pixelCases<<",\"actual_guard_cases\":"<<guardCases<<",\"replays\":"<<totalHits<<",\"opaque_memcpy_pixels\":"<<copyPixels<<",\"partial_pixels\":"<<partialPixels<<",\"transparent_skipped_pixels\":"<<skipPixels<<",\"peak_capacity_bytes\":"<<peak<<",\"source_images_destroyed_before_shutdown\":true,\"shutdown_entries\":0,\"shutdown_bytes\":0}\n";
 fprintf(stderr,"RENDER PLAN %u BG + %u GUARD pixels PASS, hits=%u copy=%u\n",pixelCases,guardCases,totalHits,copyPixels);
}
