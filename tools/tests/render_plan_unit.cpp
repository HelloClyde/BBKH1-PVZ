#include "RenderPlanCache.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <stdexcept>
using namespace Sexy;using namespace Sexy::H1PlanTest;
namespace {
unsigned checks=0;void Check(bool v,const char*m){++checks;if(!v){fprintf(stderr,"PLAN UNIT FAILURE: %s\n",m);throw std::runtime_error(m);}}
struct Source {std::vector<uint32_t> pixels;uint64_t revision=1;bool live=true;Source():pixels(360*270){};};
struct Fake {unsigned allocations=0,releases=0,live=0;int failAfter=-1;bool evictOnAllocate=false,trimOnPrepare=false;unsigned allocatingWhileBorrowed=0;Source* source=nullptr;};
bool Prepare(void* c,const void* s,View* v){auto& f=*(Fake*)c;auto& source=*(Source*)s;if(f.trimOnPrepare){pvzh1_render_plan_release_pressure(1);f.trimOnPrepare=false;}if(!source.live)return false;*v={source.pixels.data(),360,270,source.revision};return true;}
uint64_t Revision(void*,const void* s){auto& source=*(Source*)s;return source.live?source.revision:0;}
void* Allocate(void* c,size_t n){auto& f=*(Fake*)c;if(Borrowed())++f.allocatingWhileBorrowed;++f.allocations;if(f.failAfter==0)return nullptr;if(f.failAfter>0)--f.failAfter;if(f.evictOnAllocate){f.evictOnAllocate=false;Check(!Borrowed(),"source evicted while a pixel pointer is borrowed");f.source->pixels.clear();f.source->pixels.shrink_to_fit();f.source->live=false;++f.source->revision;}void*p=malloc(n);if(p)++f.live;return p;}
void Release(void*c,void*p){auto& f=*(Fake*)c;++f.releases;--f.live;free(p);}
Hooks Hook(Fake& f){return {&f,Prepare,Revision,Allocate,Release};}
uint32_t Composite(uint32_t d,uint32_t s,bool alpha){unsigned a=alpha?s>>24:255;if(a<=8)return d;if(a>=240)return s|0xff000000u;unsigned inv=255-a;uint32_t a1=(((((s&0xff00ffu)*a)>>8)&0xff00ffu)|((((s&0xff00u)*a)>>8)&0xff00u)),a2=(((((d&0xff00ffu)*inv)>>8)&0xff00ffu)|((((d&0xff00u)*inv)>>8)&0xff00u));return 0xff000000u|(a1+a2);}
unsigned cl=0,ct=0,cr=360,cb=270;
uint32_t Sample(const Source&s,uint32_t u,uint32_t v){unsigned x=u>>16,y=v>>16;if(x<cl)x=cl;if(x>=cr)x=cr-1;if(y<ct)y=ct;if(y>=cb)y=cb-1;return s.pixels[y*360+x];}
void Paint(Source& s,int kind){s.live=true;s.pixels.resize(360*270);for(unsigned i=0;i<s.pixels.size();++i){unsigned x=i%360,a=kind==1?255:kind==2?0:x<120?255:x<180?240+(x%15):x<200?8:9+(x%231);s.pixels[i]=(a<<24)|((i*0x153785u+kind*37)&0xffffffu);}++s.revision;}
void Original(const Source&s,std::vector<uint32_t>& d,int step,int dv,bool alpha,bool record){for(unsigned y=0;y<270;++y){uint32_t u=step<0?359u*65536u:123u,v=y*65536u+53;if(record&&pvzh1_render_plan_capture_active)H1RenderPlanRecordSpan(d.data()+y*360,360,s.pixels.data(),360,cl,ct,cr,cb,u,v,step,dv,alpha);for(unsigned x=0;x<360;++x,u+=(unsigned)step,v+=(unsigned)dv)d[y*360+x]=Composite(d[y*360+x],Sample(s,u,v),alpha);}}
bool Frame(Request& req,Source& s,Fake& f,int step,int dv,bool alpha,unsigned seed){auto* d=req.destination;std::vector<uint32_t> actual(360*270),expected(360*270);for(unsigned i=0;i<actual.size();++i)actual[i]=expected[i]=0xff000000u|((i*0x194137u+seed)&0xffffffu);req.destination=actual.data();Original(s,expected,step,dv,alpha,false);H1RenderPlanToken token;bool hit=DrawOrBegin(req,Hook(f),token);if(!hit){Original(s,actual,step,dv,alpha,true);H1RenderPlanFinish(token);}Check(actual==expected,"replay changed nearest/alpha/boundary pixels");req.destination=d;return hit;}
}
int main(){
 try{
  Source source;Fake fake;fake.source=&source;int target=0;Request req{};req.key.source=&source;req.key.destination=&target;req.width=360;req.height=270;req.destination=(uint32_t*)&target;
  for(int step:{32768,65535,65536,98304,-65536})for(int dv:{0,17,-17})for(bool alpha:{false,true}){
   Reset();Paint(source,0);req.key.words[0]=(unsigned)step;req.key.words[1]=(unsigned)dv;req.key.words[2]=alpha;
   Check(!Frame(req,source,fake,step,dv,alpha,1),"first key should be original");Check(!Frame(req,source,fake,step,dv,alpha,2),"count should be original");Check(!Frame(req,source,fake,step,dv,alpha,3),"fill should be original");Check(Frame(req,source,fake,step,dv,alpha,4),"warm plan failed to replay");Check(Frame(req,source,fake,step,dv,alpha,5),"warm plan failed on changed destination");Check(fake.allocatingWhileBorrowed==0,"allocation occurred during recording/replay");
  }
  // Cropped cells must clamp all four sides, including wrapped unsigned UVs.
  cl=17;ct=23;cr=337;cb=251;
  for(int step:{32768,65536,98304,-65536})for(int dv:{0,17,-17})for(bool alpha:{false,true}){
   Reset();Paint(source,0);for(unsigned i=0;i<5;++i)Frame(req,source,fake,step,dv,alpha,i);
   Check(pvzh1_render_plan_diagnostic[3]>=2,"cropped physical cell did not replay");
  }
  cl=ct=0;cr=360;cb=270;
  Reset();Paint(source,1);for(int i=0;i<4;++i)Frame(req,source,fake,65536,0,true,i);Check(pvzh1_render_plan_diagnostic[19]>0,"true alpha255 contiguous pixels never used memcpy");
  for(unsigned word:{0u,4u,8u,10u,14u,24u}){++req.key.words[word];Check(!Frame(req,source,fake,65536,0,true,7),"geometry/clip/matrix/target key mutation reused plan");}
  for(int i=0;i<4;++i)Frame(req,source,fake,65536,0,true,i);
  Paint(source,0);Check(!Frame(req,source,fake,65536,0,true,9),"new storage generation reused plan");
  Reset();Paint(source,1);Frame(req,source,fake,65536,0,true,0);fake.evictOnAllocate=true;Frame(req,source,fake,65536,0,true,0);Check(!source.live&&Capacity()==0,"source eviction during reserve retained stale plan");Check(fake.allocatingWhileBorrowed==0,"reserve source eviction happened during borrowing");
  Paint(source,1);Reset();fake.failAfter=1;Frame(req,source,fake,65536,0,true,0);Frame(req,source,fake,65536,0,true,0);Check(Capacity()==0&&fake.live==0,"optional partial allocation failure leaked pages");fake.failAfter=-1;
  Reset();for(unsigned id=0;id<12;++id){req.key.words[0]=id;for(unsigned i=0;i<4;++i)Frame(req,source,fake,65536,0,true,i);Check(Capacity()+pvzh1_render_plan_diagnostic[23]<=512*1024,"cache exceeded capacity including metadata");Check(pvzh1_render_plan_diagnostic[0]<=64,"entry table exceeded 64 plans");}
  auto before=Capacity();auto freed=pvzh1_render_plan_release_pressure(1);Check(freed>0&&Capacity()+freed==before,"pressure did not free exactly one owned plan");Check(source.live,"plan pressure release evicted source pixels");
  fake.trimOnPrepare=true;Frame(req,source,fake,65536,0,true,7);Check(fake.allocatingWhileBorrowed==0,"prepare-trim caused stale entry/pixel borrowing");
  Reset();Frame(req,source,fake,65536,0,true,0);std::vector<uint32_t>d(360*270);req.destination=d.data();H1RenderPlanToken token;Check(!DrawOrBegin(req,Hook(fake),token)&&token.value,"count capture did not start");Check(!pvzh1_render_plan_release_pressure(1),"pressure freed an active capture");H1RenderPlanAbort(token);Check(!Borrowed()&&Capacity()==0,"aborted draw retained busy/borrowed state");
  pvzh1_render_plan_shutdown();Check(fake.live==0&&Capacity()==0&&pvzh1_render_plan_diagnostic[0]==0,"shutdown did not release all owned pages");
  printf("{\"passed\":true,\"checks\":%u,\"allocations_during_borrow\":%u,\"live_allocations_after_shutdown\":%u}\n",checks,fake.allocatingWhileBorrowed,fake.live);
 }catch(const std::exception& e){fprintf(stderr,"%s\n",e.what());return 1;}
}
