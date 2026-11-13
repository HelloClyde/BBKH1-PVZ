/* Compare early rejection to the original textured triangle entry. */
#include "LawnApp.h"
#include "graphics/MemoryImage.h"
#include "graphics/SoftwareRaster.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
extern "C" void pvzh1_triangle_culling_probe(){
 using namespace Sexy;
 MemoryImage texture(gLawnApp),a(gLawnApp),b(gLawnApp);
 texture.Create(16,16);a.Create(64,48);b.Create(64,48);
 for(unsigned i=0;i<256;++i)texture.GetBits()[i]=((i*37u%256)<<24)|(i*0x164b35u&0xffffffu);
 texture.SetImageMode(true,true);texture.BitsChanged();unsigned cases=0;
 const Rect clip(11,9,39,28);
 for(float x:{-40.f,10.999f,11.f,11.001f,49.999f,50.f,50.001f,80.f})
 for(float y:{-40.f,8.999f,9.f,9.001f,36.999f,37.f,37.001f,80.f})
 for(bool negative:{false,true})for(bool vertex:{false,true}){
  float extent=negative?-17.f:17.f;
  TriVertex v[1][3];float xs[]={x,x+extent,x},ys[]={y,y,y+extent};
  for(unsigned j=0;j<3;++j){v[0][j].x=xs[j];v[0][j].y=ys[j];v[0][j].u=j==1?1.f:0.f;v[0][j].v=j==2?1.f:0.f;v[0][j].color=vertex?0x8fd37b29u:0;}
  for(auto*d:{&a,&b}){auto*p=d->GetBits();for(unsigned i=0;i<64*48;++i)p[i]=0xff000000u|(i*0x163b75u&0xffffffu);d->BitsChanged();}
  a.MemoryImage::BltTrianglesTexHelper(&texture,v,1,clip,Color::White,0,a.GetBits(),64*4,0x8888,0,0,true);
  H1DrawTriangles(&b,&texture,v,1,clip,Color::White,0,0,0,true);
  if(!std::equal(a.GetBits(),a.GetBits()+64*48,b.GetBits()))throw std::runtime_error("Early triangle rejection changed clip-edge pixels");
  ++cases;
 }
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"triangle-culling-verification.json");
 out<<"{\"passed\":true,\"original_shader_comparisons\":"<<cases<<",\"exact_and_fractional_clip_boundaries\":true,\"reversed_triangles\":true,\"vertex_alpha\":true}\n";
}
