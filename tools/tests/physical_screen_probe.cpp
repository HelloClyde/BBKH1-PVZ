#include "LawnApp.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImage.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/SoftwareRaster.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdio>
using namespace Sexy;
extern "C" volatile uint32_t pvzh1_physical_screen_diagnostic[8];
namespace {
void Check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
uint64_t Hash(const uint32_t*p,size_t n){uint64_t h=1469598103934665603ull;while(n--){auto x=*p++;for(int s=0;s<32;s+=8){h^=(x>>s)&255;h*=1099511628211ull;}}return h;}
Rect ReferenceRect(const Rect&r){int l=360,t=270,right=0,bottom=0;for(int y=0;y<270;++y)for(int x=0;x<360;++x)if((int64_t)(2*x+1)*800>=(int64_t)2*360*r.mX&&(int64_t)(2*x+1)*800<(int64_t)2*360*(r.mX+r.mWidth)&&(int64_t)(2*y+1)*600>=(int64_t)2*270*r.mY&&(int64_t)(2*y+1)*600<(int64_t)2*270*(r.mY+r.mHeight)){l=std::min(l,x);t=std::min(t,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);}return Rect(l,t,std::max(0,right-l),std::max(0,bottom-t));}
bool Equal(MemoryImage*a,MemoryImage*b){return !std::memcmp(a->GetBits(),b->GetBits(),360u*270u*4);}
void Background(MemoryImage *p){p->MemoryImage::FillRect(Rect(0,0,p->mWidth,p->mHeight),Color(13,29,47),0);}
void SampleEqual(MemoryImage *physical,MemoryImage *logical){auto a=physical->GetBits(),b=logical->GetBits();for(int y=0;y<270;++y)for(int x=0;x<360;++x){int lx=(2*x+1)*800/(2*360),ly=(2*y+1)*600/(2*270);Check(a[y*360+x]==b[ly*800+lx],"physical span/coverage differs from original logical-center sample");}}
}
extern "C" void pvzh1_physical_probe(){
 static bool once=false;if(once)return;once=true;auto device=gLawnApp->mGLInterface.get();auto saved=std::move(device->mScreenImage);auto owner=std::make_unique<H1PhysicalScreen>(device,800,600,360,270);auto screen=owner.get();device->mScreenImage=std::move(owner);auto target=screen->Target();Check(screen->mWidth==800&&screen->mHeight==600&&target->mWidth==360&&target->mHeight==270,"logical proxy/physical dimensions changed");
 MemoryImage reference(gLawnApp);reference.Create(360,270);reference.SetImageMode(false,false);Background(target);Background(&reference);unsigned checks=0;
 const Rect rects[]={Rect(0,0,800,600),Rect(-13,-7,82,59),Rect(13,17,1,1),Rect(13,17,337,299),Rect(799,599,10,10),Rect(19,23,113,87),Rect(-20,97,55,59)};
 for(auto r:rects){for(int mode:{0,1}){Color c(73,141,207,mode?113:173);screen->FillRect(r,c,mode);reference.MemoryImage::FillRect(ReferenceRect(r),c,mode);Check(Equal(target,&reference),"physical fill rectangle/clip differs from center-selected original primitive");++checks;}}
 screen->ClearRect(Rect(17,29,171,113));reference.ClearRect(ReferenceRect(Rect(17,29,171,113)));Check(Equal(target,&reference),"physical ClearRect differs");++checks;
 MemoryImage texture(gLawnApp);texture.Create(31,23);texture.SetImageMode(false,true);auto pixels=texture.GetBits();for(int y=0;y<23;++y)for(int x=0;x<31;++x)pixels[y*31+x]=((unsigned)(80+(x*3+y*5)%176)<<24)|(x*7<<16)|(y*9<<8)|(x+y);texture.BitsChanged();
 for(int kind=0;kind<4;++kind){SexyTransform2D matrix;if(kind==1){matrix.Scale(-1,1);matrix.Translate(93,31);}else if(kind==2){matrix.RotateDeg(27);matrix.Translate(191,139);}else matrix.Translate(kind?-.7f:23.25f,47.5f);Rect clip=kind==3?Rect(9,37,41,27):Rect(0,0,800,600);Background(target);Background(&reference);device->BltTransformed(&texture,&clip,Color(170,90,220,129),0,Rect(0,0,31,23),matrix,false);SexyTransform2D scale;scale.Scale(.45f,.45f);SexyMatrix3 expected=scale*matrix;H1DrawTransformed(&reference,&texture,Rect(0,0,31,23),ReferenceRect(clip),Color(170,90,220,129),0,expected,false);Check(Equal(target,&reference),"physical transformed texture/half-pixel phase differs from explicit physical reference");++checks;}
 TriVertex tris[1][3]={{{17,19,0,0},{115,21,1,0},{57,137,0,1}}};TriVertex mapped[1][3]={{tris[0][0],tris[0][1],tris[0][2]}};for(auto &v:mapped[0]){v.x=(v.x+3)*.45f;v.y=(v.y+5)*.45f;}Background(target);Background(&reference);screen->BltTrianglesTex(&texture,tris,1,Rect(11,17,180,167),Color::White,0,3,5,false);H1DrawTriangles(&reference,&texture,mapped,1,ReferenceRect(Rect(11,17,180,167)),Color::White,0,0,0,false);Check(Equal(target,&reference),"direct GLImage triangle bypassed physical routing");++checks;
 MemoryImage logical(gLawnApp);logical.Create(800,600);logical.SetImageMode(false,false);std::vector<Span> spans(600);for(int y=0;y<600;++y){spans[y].mY=y;spans[y].mX=7+y%13;spans[y].mWidth=733-y%19;}
 Background(target);Background(&logical);screen->FillScanLines(spans.data(),(int)spans.size(),Color(71,191,39,117),0);logical.FillScanLines(spans.data(),(int)spans.size(),Color(71,191,39,117),0);SampleEqual(target,&logical);++checks;
 std::vector<uint8_t> coverage(781*577);for(size_t i=0;i<coverage.size();++i)coverage[i]=(uint8_t)((i*37+13)%256);for(int mode:{0,1}){Background(target);Background(&logical);screen->FillScanLinesWithCoverage(spans.data(),(int)spans.size(),Color(151,81,231,131),mode,coverage.data(),11,9,781,577);H1Coverage(&logical,spans.data(),(int)spans.size(),Color(151,81,231,131),mode,coverage.data(),11,9,781,577);SampleEqual(target,&logical);++checks;}
 Check(screen->Active()&&!screen->mBits,"ordinary physical drawing forced raw logical fallback");const auto beforeFallback=pvzh1_physical_screen_diagnostic[5];const auto physicalHash=Hash(target->GetBits(),360u*270u);std::vector<uint32_t> snapshot(target->GetBits(),target->GetBits()+360*270);auto raw=screen->GetBits();for(int y=0;y<600;++y)for(int x=0;x<800;++x)Check(raw[y*800+x]==snapshot[(y*270/600)*360+x*360/800],"GetBits snapshot did not preserve original logical stride via nearest expansion");Check(!screen->Active()&&screen->Target()==screen&&pvzh1_physical_screen_diagnostic[5]==beforeFallback+1,"GetBits did not permanently lock full-canvas fallback");raw[12345]=0xffa1b2c3;screen->BitsChanged();Check(screen->GetBits()==raw&&raw[12345]==0xffa1b2c3,"raw logical loan was invalidated or writes lost");screen->FillRect(Rect(791,591,9,9),Color(240,20,30),0);Check(screen->GetBits()==raw&&raw[599*800+799]==0xfff0141e&&pvzh1_physical_screen_diagnostic[5]==beforeFallback+1,"post-snapshot drawing failed ordinary original logical bounds or relatched fallback");checks+=3;
 device->mScreenImage=std::move(saved);std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"physical-screen-verification.json");out<<"{\"test_only\":true,\"logical_size\":[800,600],\"physical_size\":[360,270],\"reference_checks\":"<<checks<<",\"physical_hash\":"<<physicalHash<<",\"rect_clip_center_oracle\":true,\"transformed_halfpixel_reference\":true,\"direct_triangle_routing\":true,\"span_and_coverage_original_center_samples\":true,\"ordinary_drawing_raw_fallbacks\":0,\"snapshot_full_stride_and_writable_rawloan\":true,\"fallback_permanent_once\":true,\"all_passed\":true}\n";std::fprintf(stderr,"PHYSICAL SCREEN %u REFERENCE/CLIP/COVERAGE/RAW-LOAN CHECKS PASSED\n",checks);
}
