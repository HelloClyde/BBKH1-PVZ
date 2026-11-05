/* Opaque tile copies replace repeated grass alpha composition. The authored
 * roll and particle/reanimation clocks remain original. LGPL-3.0-or-later. */
#include "EarlyLawn.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Resources.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "PhysicalScreen.h"
#include "DirtyCanvas.h"
#include "PixelCopy.h"
#include "Common.h"
#include <cstdio>
#include <algorithm>
#include <new>
#include <zlib.h>
extern "C" {volatile uint32_t pvzh1_early_lawn_diagnostic[8];volatile uint32_t pvzh1_camera_lawn_diagnostic[8];}
namespace {
constexpr unsigned Cells=30*34,Frames=6;
struct Cache {uint32_t** pages=nullptr;uint16_t* map=nullptr;unsigned tiles=0,npages=0;int which=-1;bool tried=false,enabled=true;} cache;
struct Panorama {uint32_t** pages=nullptr;unsigned count=0;int which=-1;} panorama;
void DropPan(){if(panorama.pages){for(unsigned i=0;i<panorama.count;++i)delete[] panorama.pages[i];delete[] panorama.pages;}panorama={};pvzh1_camera_lawn_diagnostic[0]=0;}
uint16_t U16(const unsigned char* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
uint32_t U32(const unsigned char* p){return U16(p)|(uint32_t(U16(p+2))<<16);}
void Drop(){DropPan();if(cache.pages){for(unsigned i=0;i<cache.npages;++i)delete[] cache.pages[i];delete[] cache.pages;}delete[] cache.map;cache.pages=nullptr;cache.map=nullptr;cache.tiles=cache.npages=0;cache.which=-1;pvzh1_early_lawn_diagnostic[0]=0;}
int Case(Board* b){if(!b||b->mApp->mGameMode!=GAMEMODE_ADVENTURE||!b->mApp->IsFirstTimeAdventureMode()||b->mBackground!=BACKGROUND_1_DAY)return -1;
 if(b->mLevel==1)return 0;if(b->mLevel==2||b->mLevel==3)return 1;if(b->mLevel==4)return 2;return -1;}
bool Load(int which){FILE* f=fopen(Sexy::GetResourcePath("early-lawns.h1lb").c_str(),"rb");if(!f)return false;unsigned char header[64]{};bool okay=fread(header,1,64,f)==64;uint32_t h[16]{};for(unsigned i=0;i<16;++i)h[i]=U32(header+i*4);
 if(!okay||h[0]!=0x424c3148||h[1]!=1||h[2]!=480||h[3]!=272||h[4]!=16||h[5]!=8||h[6]!=6||!h[7]||h[7]>Frames*Cells||h[8]!=64||h[9]!=64+Frames*Cells*2||h[10]!=h[9]+h[7]*512){fclose(f);return false;}
 cache.tiles=h[7];cache.npages=(cache.tiles+7)/8;cache.map=new(std::nothrow) uint16_t[Frames*Cells];cache.pages=new(std::nothrow) uint32_t*[cache.npages]{};
 okay=cache.map&&cache.pages;uint32_t crc=crc32(0,nullptr,0);
 if(okay){okay=fread(cache.map,2,Frames*Cells,f)==Frames*Cells;crc=crc32(crc,reinterpret_cast<const Bytef*>(cache.map),Frames*Cells*2);
  for(unsigned i=0;okay&&i<Frames*Cells;++i)if(cache.map[i]>=cache.tiles)okay=false;}
 bool needed[(Frames*Cells+7)/8]{};unsigned resident=0;uint32_t scratch[1024];
 if(okay)for(unsigned i=which*2*Cells;i<unsigned(which+1)*2*Cells;++i)needed[cache.map[i]/8]=true;
 for(unsigned i=0;okay&&i<cache.npages;++i){unsigned words=std::min(1024u,(cache.tiles*128)-i*1024);uint32_t* target=scratch;if(needed[i]){cache.pages[i]=new(std::nothrow) uint32_t[words];target=cache.pages[i];resident+=words*4;}okay=target&&fread(target,4,words,f)==words;if(okay)crc=crc32(crc,reinterpret_cast<const Bytef*>(target),words*4);}
 if(okay)okay=crc==h[11]&&fgetc(f)==EOF;fclose(f);if(!okay){Drop();return false;}
 cache.which=which;pvzh1_early_lawn_diagnostic[0]=resident+Frames*Cells*2+cache.npages*sizeof(void*);pvzh1_early_lawn_diagnostic[4]=resident/512;return true;
}
const uint32_t* Tile(unsigned frame,unsigned cell){unsigned n=cache.map[frame*Cells+cell];return cache.pages[n/8]+(n%8)*128;}
int Boundary(int coordinate){int n=coordinate*480*2-1060;return n>=0?(n+2119)/2120:n/2120;}
bool LoadPan(int which){
 constexpr unsigned bytes=634*272*4,words=634*272;FILE* f=fopen(Sexy::GetResourcePath("camera-lawns.h1cp").c_str(),"rb");if(!f)return false;
 unsigned char header[64]{};bool okay=fread(header,1,64,f)==64;uint32_t h[16]{};for(unsigned i=0;i<16;++i)h[i]=U32(header+i*4);
 okay=okay&&h[0]==0x50433148&&h[1]==1&&h[2]==634&&h[3]==272&&h[4]==3&&h[5]==bytes&&h[6]==64&&which>=0&&which<3;
 if(okay)okay=fseek(f,64+which*bytes,SEEK_SET)==0;
 panorama.count=(words+1023)/1024;if(okay)panorama.pages=new(std::nothrow) uint32_t*[panorama.count]{};okay=okay&&panorama.pages;
 uint32_t crc=crc32(0,nullptr,0);
 for(unsigned i=0;okay&&i<panorama.count;++i){unsigned n=std::min(1024u,words-i*1024);panorama.pages[i]=new(std::nothrow) uint32_t[n];okay=panorama.pages[i]&&fread(panorama.pages[i],4,n,f)==n;if(okay)crc=crc32(crc,reinterpret_cast<const Bytef*>(panorama.pages[i]),n*4);}
 fclose(f);if(!okay||crc!=h[7+which]){DropPan();return false;}
 panorama.which=which;pvzh1_camera_lawn_diagnostic[0]=bytes+panorama.count*sizeof(void*);++pvzh1_camera_lawn_diagnostic[2];return true;
}
void PanCopy(uint32_t* out,unsigned at,unsigned n){while(n){unsigned chunk=std::min(n,1024u-(at&1023));H1CopyPixels(out,panorama.pages[at>>10]+(at&1023),chunk);out+=chunk;at+=chunk;n-=chunk;}}
bool DrawPan(Board* b,Sexy::Graphics* g,int which){
 if(!panorama.pages||panorama.which!=which||b->mApp->mGameScene!=SCENE_LEVEL_INTRO||b->mSodPosition||b->mX==130||b->mX< -120||b->mX>220||b->mY||g->mTransX!=b->mX||g->mTransY||g->mScaleX!=1||g->mScaleY!=1||g->mColorizeImages||g->mDrawMode!=Sexy::Graphics::DRAWMODE_NORMAL||g->mClipRect!=Sexy::Rect(0,0,1060,600))return false;
 auto* screen=Sexy::H1GetPhysicalScreen(dynamic_cast<Sexy::GLImage*>(g->mDestImage));if(!screen||!screen->Active()||!screen->mGLInterface->mTransformStack.empty())return false;auto* target=screen->Target();if(target!=h1DirtyCanvas.owner||!target->mBits)return false;
 const unsigned offset=std::clamp(((220-b->mX)*480+530)/1060,0,154);const uint64_t key[3]={0x483150414eull,(uintptr_t)b,(uint64_t(unsigned(which))<<32)|offset};
 bool foundation=h1DirtyCanvas.Root(target,key,sizeof(key)),limited=foundation&&h1DirtyCanvas.limited;if(!foundation)h1DirtyCanvas.MarkAll();if(h1AdditiveNormalDraw)h1AdditiveNormalDraw();auto*out=target->mBits.get();
 for(unsigned y=0;y<272;++y){if(!limited){PanCopy(out+y*480,y*634+offset,480);continue;}unsigned mask=h1DirtyCanvas.RestoreRow(y);for(unsigned x=0;x<480;x+=16)if(mask&(1u<<(x/16)))PanCopy(out+y*480+x,y*634+offset+x,16);}
 target->BitsChanged();target->mBitsChangedCount+=which?3:1;Sexy::H1ScreenNoteDraw(screen);if(which)Sexy::H1ScreenNoteDraw(screen);
 ++pvzh1_camera_lawn_diagnostic[1];pvzh1_camera_lawn_diagnostic[4]=offset;pvzh1_camera_lawn_diagnostic[5]=which;++pvzh1_camera_lawn_diagnostic[limited?7:6];return true;
}

}
void H1EarlyLawnPrepare(Board* b){int which=Case(b);if(which!=cache.which){Drop();cache.tried=false;}if(cache.enabled&&!cache.tried&&which>=0){cache.tried=true;if(!Load(which))++pvzh1_early_lawn_diagnostic[3];}if(cache.enabled&&cache.map&&which>=0&&b->mSodPosition==0&&!panorama.pages&&!LoadPan(which))++pvzh1_camera_lawn_diagnostic[3];}
bool H1EarlyLawnDraw(Board* b,Sexy::Graphics* g){
 int which=Case(b);if(panorama.pages&&b&&b->mSodPosition>0)DropPan();if(cache.enabled&&g&&which>=0&&DrawPan(b,g,which))return true;
 if(!cache.enabled||!cache.map||which<0||which!=cache.which||!g||b->mX!=130||b->mY||g->mTransX!=130||g->mTransY||g->mScaleX!=1||g->mScaleY!=1||g->mColorizeImages||g->mDrawMode!=Sexy::Graphics::DRAWMODE_NORMAL||g->mClipRect!=Sexy::Rect(0,0,1060,600))return false;
 auto* screen=Sexy::H1GetPhysicalScreen(dynamic_cast<Sexy::GLImage*>(g->mDestImage));if(!screen||!screen->Active()||!screen->mGLInterface->mTransformStack.empty())return false;
 auto* target=screen->Target();if(target!=h1DirtyCanvas.owner||!target->mBits)return false;
 const int progress=std::clamp(b->mSodPosition,0,1000),width=which==0?Sexy::IMAGE_SOD1ROW->GetWidth():which==1?Sexy::IMAGE_SOD3ROW->GetWidth():773;
 const int reveal=std::clamp(Boundary(130+(which==0?239:which==1?235:232)-BOARD_OFFSET+(progress*width+500)/1000),0,480);
 const uint64_t key[3]={0x48314c41574eull,(uintptr_t)b,uint64_t(unsigned(which))<<32|unsigned(reveal)};
 const bool foundation=h1DirtyCanvas.Root(target,key,sizeof(key)),limited=foundation&&h1DirtyCanvas.limited;
 if(!foundation)h1DirtyCanvas.MarkAll();if(h1AdditiveNormalDraw)h1AdditiveNormalDraw();auto* dest=target->mBits.get();
 for(unsigned row=0;row<34;++row){unsigned mask=limited?h1DirtyCanvas.RestoreRow(row*8):H1DirtyCanvas::All;
  for(unsigned col=0;col<30;++col){if(!(mask&(1u<<col)))continue;const unsigned cell=row*30+col,x=col*16,y=row*8;
   const auto* base=Tile(which*2,cell);const auto* full=Tile(which*2+1,cell);int n=std::clamp(reveal-int(x),0,16);
   if(n==0||n==16){const auto* source=n?full:base;
    for(unsigned j=0;j<8;++j)H1CopyPixels(dest+(y+j)*480+x,source+j*16,16);
   }else for(unsigned j=0;j<8;++j){auto* out=dest+(y+j)*480+x;H1CopyPixels(out,full+j*16,n);H1CopyPixels(out+n,base+j*16+n,16-n);}
  }
 }
 target->BitsChanged();Sexy::H1ScreenNoteDraw(screen);++pvzh1_early_lawn_diagnostic[1];++pvzh1_early_lawn_diagnostic[limited?6:5];pvzh1_early_lawn_diagnostic[7]=reveal;return true;
}
extern "C" void pvzh1_early_lawn_shutdown(){Drop();cache.tried=false;}
#ifdef PVZ_HOST_RUNNER
void H1EarlyLawnTestEnable(bool v){cache.enabled=v;}
#endif
