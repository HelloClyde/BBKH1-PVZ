/* Compare the physical-screen mapper to its original 64-bit equations. */
#include "LawnApp.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/GLInterface.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
extern "C" volatile uint32_t pvzh1_clip_map_diagnostic[4];
namespace {
int Boundary(int v,int l,int p){int64_t n=int64_t(v)*p*2-l,d=int64_t(l)*2;return int(n>=0?(n+d-1)/d:n/d);}
Sexy::Rect Reference(const Sexy::Rect& input,int lw,int lh,int pw,int ph){
 auto r=input.Intersection(Sexy::Rect(0,0,lw,lh));
 if(r.mX==0&&r.mY==0&&r.mWidth==lw&&r.mHeight==lh)return Sexy::Rect(0,0,pw,ph);
 int x=Boundary(r.mX,lw,pw),y=Boundary(r.mY,lh,ph),right=Boundary(r.mX+r.mWidth,lw,pw),bottom=Boundary(r.mY+r.mHeight,lh,ph);
 return Sexy::Rect(x,y,std::max(0,right-x),std::max(0,bottom-y));
}
}
extern "C" void pvzh1_clip_mapping_probe(){
 using namespace Sexy;unsigned checks=0;
 for(auto dims:{std::pair<int,int>{1060,600},{800,600},{1234,777}}){
  const int pw=dims.first==800?360:480,ph=dims.first==800?270:272;
  H1PhysicalScreen screen(gLawnApp->mGLInterface.get(),dims.first,dims.second,pw,ph);
  auto check=[&](Rect r){auto expected=Reference(r,screen.mWidth,screen.mHeight,pw,ph);if(screen.MapRect(r)!=expected||screen.MapRect(r)!=expected)throw std::runtime_error("Physical clip center boundary/cache changed pixels");checks+=2;};
  for(int x=-1;x<=dims.first+1;++x)for(int width:{0,1,2,3,17,130,480,800,1060})for(int y:{-1,0,1,137,271,599,777})check(Rect(x,y,width,149));
  for(int y=-1;y<=dims.second+1;++y)for(int height:{0,1,2,3,8,27,272,600})check(Rect(130,y,800,height));
  check(Rect(0,0,dims.first,dims.second));
  ++screen.mWidth;check(Rect(130,0,800,600));--screen.mWidth;check(Rect(130,0,800,600));
  screen.GetBits();auto r=Rect(-7,9,800,600);if(screen.MapRect(r)!=r.Intersection(Rect(0,0,screen.mWidth,screen.mHeight)))throw std::runtime_error("Logical raw-canvas clipping changed");++checks;
 }
 if(!pvzh1_clip_map_diagnostic[0]||!pvzh1_clip_map_diagnostic[2])throw std::runtime_error("Clip memo or bounded 32-bit path was not exercised");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"clip-mapping-verification.json");out<<"{\"passed\":true,\"original_64bit_equation_checks\":"<<checks<<",\"one_pixel_edges_and_empty_clips\":true,\"dimension_changes\":true,\"raw_logical_canvas\":true}\n";
}
