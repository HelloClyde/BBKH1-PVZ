/* Compare general reduced-UV spans and actual Boss frames with SWTri. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/SWTri.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/ReanimTrig.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstring>
using namespace Sexy;
extern "C" volatile uint32_t pvzh1_axis_span_diagnostic[8];
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
uint32_t Pixel(uint32_t p,uint32_t t,bool alpha){
 unsigned a=alpha?t>>24:255;if(a<=8)return p;if(a>=240)return t|0xff000000u;
 return 0xff000000u|(((((t&0xff00ffu)*a)>>8)&0xff00ffu)|((((t&0xff00u)*a)>>8)&0xff00u))+
                    (((((p&0xff00ffu)*(255-a))>>8)&0xff00ffu)|((((p&0xff00u)*(255-a))>>8)&0xff00u));
}
}
extern "C" void pvzh1_rotated_reduced_span_probe(){
 std::vector<uint32_t> texture(512*64);std::vector<uint32_t*> pages(32);
 for(unsigned i=0;i<texture.size();++i)texture[i]=((i%256)<<24)|((i*2246822519u)&0xffffffu);
 for(unsigned i=0;i<pages.size();++i)pages[i]=texture.data()+i*1024;
 SWHelper::SWTextureInfo info{};info.pitch=512;info.endpos=texture.size();info.reducedClamp=true;
 info.clampLeft=1;info.clampRight=511;info.clampTop=1;info.clampBottom=63;
 unsigned direct=0;
 for(bool paged:{false,true})for(bool alpha:{false,true})for(int n:{8,17,32})
 for(int du:{0,1,32768,65536,-65536})for(int dv:{1,16384,65536,-32768,-65536}){
  info.pTexture=paged?nullptr:texture.data();info.pages=paged?pages.data():nullptr;
  unsigned u=(du<0?300:160)*65536u+31923,v=(dv<0?50:10)*65536u+31779;
  std::vector<uint32_t> actual(n+8,0xff739ac1),expected=actual;
  for(unsigned i=0,x=u,y=v;i<unsigned(n);++i,x+=unsigned(du),y+=unsigned(dv))
   expected[i+4]=Pixel(expected[i+4],info.ReadClamped(x>>16,y>>16),alpha);
  Check(H1FastReducedSpan(actual.data()+4,n,&info,u,v,du,dv,alpha),"Interior rotated span rejected");
  Check(actual==expected,"Rotated span differs from original sampler or wrote outside span");++direct;
 }
 for(unsigned v:{0u,UINT32_MAX,62u*65536u}){
  std::vector<uint32_t> pixels(40,0x197532ab),before=pixels;
  Check(!H1FastReducedSpan(pixels.data()+4,32,&info,100*65536u,v,65536,65536,true),"Out-of-cell span accepted");
  Check(pixels==before,"Rejected rotated span wrote output");++direct;
 }
 PlayerInfo profile;profile.mLevel=50;profile.mName="ROTATED_BOSS";auto* old=gLawnApp->mPlayerInfo;
 gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* z=gLawnApp->mBoard->GetBossZombie();if(!z)z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_BOSS,0,-2);
 auto* r=gLawnApp->ReanimationGet(z->mBodyReanimID);Check(r,"No original Boss");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();
 auto* bits=target->GetBits();h1DirtyCanvas.enabled=false;H1RenderPlanTestEnable(false);
 r->mOverlayMatrix.m02=750;r->mOverlayMatrix.m12=0;r->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;
 const unsigned frames=r->mDefinition->mTracks.tracks[0].mTransforms.count;unsigned compared=0;
 const unsigned rotatedBefore=pvzh1_axis_span_diagnostic[7];
 for(unsigned f=0;f+1<frames;f+=37)for(unsigned fractional:{0u,1u})for(unsigned bg=0;bg<2;++bg){
  r->mFrameStart=f;r->mFrameCount=2;r->mAnimTime=fractional*.5f;std::vector<uint32_t> expected;
  for(bool fast:{false,true}){
   H1FastReducedSpanTestEnable(fast);H1ReanimTrigTestEnable(fast);
   for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);
   Graphics g(screen);r->DrawAllRenderGroups(&g);
   if(!fast)expected.assign(bits,bits+480*272);
   else{Check(std::equal(expected.begin(),expected.end(),bits),"Actual Boss pixels differ");++compared;}
  }
 }
 const unsigned rotated=pvzh1_axis_span_diagnostic[7]-rotatedBefore;
 Check(compared>50&&rotated>1000,"Actual Boss did not exercise rotated span optimization");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"rotated-reduced-span-verification.json");
 out<<"{\"passed\":true,\"direct_span_checks\":"<<direct<<",\"boss_frame_comparisons\":"<<compared
    <<",\"boss_rotated_pixels\":"<<rotated<<",\"contiguous_and_paged_storage\":true,\"resource_bytes_added\":0}\n";
 H1FastReducedSpanTestEnable(true);H1ReanimTrigTestEnable(true);H1RenderPlanTestEnable(true);h1DirtyCanvas.enabled=true;
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
