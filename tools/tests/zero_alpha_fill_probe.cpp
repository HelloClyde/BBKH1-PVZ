/* Zero normal alpha must preserve every destination bit, including alpha. */
#include "LawnApp.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/Graphics.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <stdexcept>
extern "C" void pvzh1_zero_alpha_fill_probe(){
 using namespace Sexy;unsigned cases=0;
 for(unsigned pattern=0;pattern<16;++pattern){
  MemoryImage a(gLawnApp),b(gLawnApp);a.Create(37,23);b.Create(37,23);
  a.mForcedMode=b.mForcedMode=true;a.mHasAlpha=b.mHasAlpha=false;a.mHasTrans=b.mHasTrans=false;
  auto* x=a.GetBits();auto* y=b.GetBits();for(unsigned i=0;i<37*23;++i)x[i]=y[i]=i*0x137ac45u+pattern*0x31512u;
  const Rect r(-5,2,39,19);const Color color(217,57,91,0);
  H1FillRect(&a,r,color,Graphics::DRAWMODE_NORMAL);b.MemoryImage::FillRect(r.Intersection(Rect(0,0,37,23)),color,Graphics::DRAWMODE_NORMAL);
  if(!std::equal(x,x+37*23,y))throw std::runtime_error("Zero-alpha destination changed");++cases;
 }
 std::ofstream f(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"zero-alpha-fill-verification.json");f<<"{\"passed\":true,\"whole_image_comparisons\":"<<cases<<",\"mixed_destination_alpha\":true,\"clipped_rectangles\":true}\n";
}
