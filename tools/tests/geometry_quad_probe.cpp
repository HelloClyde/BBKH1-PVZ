#include "LawnApp.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/MemoryImageCache.h"
#include <vector>
#include <stdexcept>
#include <filesystem>
#include <fstream>
using namespace Sexy;
extern "C" void pvzh1_geometry_probe(){
 auto* device=gLawnApp->mGLInterface.get();auto saved=std::move(device->mScreenImage);
 device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,800,600,360,270);auto* target=H1ScreenTarget(device->GetScreenImage());
 H1RenderPlanTestEnable(false);unsigned cases=0;
 {
  GLImage image(device);if(!H1ImageCacheDescribe(&image,"reanim/selectorscreen_bg_right",true,0xffffff))throw std::runtime_error("missing oracle texture");
  for(unsigned geometry=0;geometry<12;++geometry)for(bool center:{false,true})for(bool cropped:{false,true})for(bool tint:{false,true})for(int mode:{0,1}){
   SexyTransform2D matrix;matrix.Scale(geometry%3==0?.83f:.45f,geometry%3==1?.63f:.45f);
   if(geometry==1||geometry==2)matrix.RotateDeg(geometry==1?17.f:-33.5f);
   if(geometry==3)matrix.Scale(-1,1);
   if(geometry==4){matrix.m01=.117f;matrix.m10=-.071f;}
   if(geometry==5){matrix.m20=.25f;matrix.m21=-.12f;matrix.m22=.7f;}
   matrix.Translate(geometry==6?-91.125f:geometry==7?331.75f:57.375f,geometry==8?-107.25f:geometry==9?251.5f:51.125f);
   Rect src=cropped?Rect(7,3,image.mWidth-15,image.mHeight-8):Rect(0,0,image.mWidth,image.mHeight);
   Rect clip=geometry==10?Rect(31,27,281,209):Rect(0,0,360,270);Color color=tint?Color(143,91,207,129):Color::White;
   std::vector<uint32_t> expected;
   for(bool fast:{false,true}){
    H1GeometryFastTestEnable(fast);auto* pixels=target->GetBits();for(unsigned i=0;i<360*270;++i)pixels[i]=0xff000000u|((i*0x13579bu+geometry*217)&0xffffffu);target->BitsChanged();
    H1DrawTransformed(target,&image,src,clip,color,mode,matrix,true,geometry==11?7.125f:0.f,geometry==11?-3.75f:0.f,center);
    pixels=target->GetBits();std::vector<uint32_t> actual(pixels,pixels+360*270);
    if(!fast)expected=std::move(actual);else if(actual!=expected)throw std::runtime_error("2D corner/clip shortcut changed original pixels");
   }++cases;
  }
 }
 device->mScreenImage=std::move(saved);H1RenderPlanTestEnable(true);H1GeometryFastTestEnable(true);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"geometry-verification.json");out<<"{\"passed\":true,\"whole_frame_cases\":"<<cases<<",\"original_3D_matrix_oracle\":true,\"rotation\":true,\"shear\":true,\"mirroring\":true,\"cropped_sources\":true,\"tints\":true,\"additive\":true,\"centered\":true}\n";
}
