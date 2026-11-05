/* Original loading art, baked losslessly at LCD size. LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Startup.h"
#include "Common.h"
#include "SDL.h"
#include "widget/WidgetManager.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/PixelCopy.h"
#include <algorithm>
#include <cstdio>
#include <new>
#include <zlib.h>
extern "C" int pvzh1_widget_draw_active();
extern "C" {volatile uint32_t pvzh1_level_loading_diagnostic[12];}
namespace {
struct Cache {unsigned h[16]{};unsigned char* data=nullptr;uint32_t* patch=nullptr;z_stream z{};bool initialized=false,ready=false,tried=false,painted=false;Sexy::MemoryImage* canvas=nullptr;unsigned serial=0;} cache;
unsigned depth=0,stage=0;bool drawing=false;uint32_t lastTick=0;
unsigned Word(const unsigned char* p){return unsigned(p[0])|unsigned(p[1])<<8|unsigned(p[2])<<16|unsigned(p[3])<<24;}
void Drop(){if(cache.initialized)inflateEnd(&cache.z);delete[] cache.data;delete[] cache.patch;cache={};pvzh1_level_loading_diagnostic[10]=0;}
void ReleaseBackground(){
 if(cache.initialized){inflateEnd(&cache.z);cache.initialized=false;}delete[] cache.data;cache.data=nullptr;
 pvzh1_level_loading_diagnostic[10]=cache.h[8]*cache.h[9]*4;
}
bool Decode(unsigned offset,unsigned size,void* output,unsigned bytes){
 if(inflateReset(&cache.z)!=Z_OK)return false;cache.z.next_in=cache.data+offset;cache.z.avail_in=size;cache.z.next_out=(Bytef*)output;cache.z.avail_out=bytes;
 return inflate(&cache.z,Z_FINISH)==Z_STREAM_END&&cache.z.total_out==bytes&&!cache.z.avail_in;
}
bool Load(){
 FILE* f=fopen(Sexy::GetResourcePath("level-loading.h1ll").c_str(),"rb");if(!f)return false;
 unsigned char header[64];bool ok=fread(header,1,64,f)==64;for(unsigned i=0;ok&&i<16;++i)cache.h[i]=Word(header+i*4);auto* h=cache.h;
 if(!ok||h[0]!=0x4c4c3148||h[1]!=1||h[2]!=480||h[3]!=272||h[4]!=522240||!h[5]||h[5]>1024*1024||h[6]>=480||h[7]>=272||!h[8]||h[8]>480-h[6]||!h[9]||h[9]>272-h[7]||!h[10]||h[10]>65536||h[13]!=64+h[5]+h[10])ok=false;
 if(ok){cache.data=new(std::nothrow) unsigned char[h[5]+h[10]];cache.patch=new(std::nothrow) uint32_t[h[8]*h[9]];ok=cache.data&&cache.patch;}
 if(ok)ok=fread(cache.data,1,h[5]+h[10],f)==h[5]+h[10]&&fgetc(f)==EOF;fclose(f);if(!ok)return false;
 cache.initialized=inflateInit(&cache.z)==Z_OK;if(!cache.initialized)return false;
 // Validate and warm the inflater's window once while the menu is loading.
 // Repainting never allocates a logical 1060x600 image or a second canvas.
 cache.z.next_in=cache.data;cache.z.avail_in=h[5];unsigned char scratch[4096];unsigned crc=crc32(0,nullptr,0);int result=Z_OK;
 while(result==Z_OK){cache.z.next_out=scratch;cache.z.avail_out=sizeof(scratch);result=inflate(&cache.z,Z_NO_FLUSH);crc=crc32(crc,scratch,sizeof(scratch)-cache.z.avail_out);if(cache.z.total_out>h[4])return false;}
 if(result!=Z_STREAM_END||cache.z.avail_in||cache.z.total_out!=h[4]||crc!=h[11])return false;
 if(!Decode(h[5],h[10],cache.patch,h[8]*h[9]*4)||crc32(0,(const Bytef*)cache.patch,h[8]*h[9]*4)!=h[12])return false;
 cache.ready=true;pvzh1_level_loading_diagnostic[10]=h[5]+h[10]+h[8]*h[9]*4+32768+sizeof(z_stream);return true;
}
void Draw(bool force){
 if(!depth||drawing||!cache.ready||!gLawnApp||gLawnApp->mShutdown||gLawnApp->mIsDrawing||pvzh1_widget_draw_active()||!gLawnApp->mGLInterface)return;
 const unsigned now=SDL_GetTicks();if(!force&&now-lastTick<1000)return;drawing=true;
 auto* device=gLawnApp->mGLInterface.get();auto* screen=Sexy::H1GetPhysicalScreen(device->GetScreenImage());auto* target=screen?screen->Target():nullptr;
 if(target&&target==h1DirtyCanvas.owner&&target->mWidth==480&&target->mHeight==272){
  auto* pixels=target->mBits.get();bool ok=false;
  if(!cache.painted){h1DirtyCanvas.RequireClear();ok=Decode(0,cache.h[5],pixels,522240);}
  else ok=cache.canvas==target&&cache.serial==target->mBitsChangedCount&&h1DirtyCanvas.pending;
  // Synchronous loading does not draw game widgets. Reuse the loader already
  // in the existing back buffer and reveal only newly completed grass. Drop
  // compressed pixels and the inflater BEFORE the board's heap allocations.
  if(ok){
   h1DirtyCanvas.pending=false;h1DirtyCanvas.keyValid=h1DirtyCanvas.rooted=h1DirtyCanvas.limited=false;h1DirtyCanvas.MarkAll();
   const auto* h=cache.h;const unsigned width=h[8]*stage/100;
   for(unsigned y=0;y<h[9];++y)H1CopyPixels(pixels+(h[7]+y)*480+h[6],cache.patch+y*h[8],width);
   target->BitsChanged();pvzh1_level_loading_diagnostic[5]=stage;device->Redraw(nullptr);lastTick=SDL_GetTicks();
   cache.painted=true;cache.canvas=target;cache.serial=target->mBitsChangedCount;if(cache.data)ReleaseBackground();
   auto& d=pvzh1_level_loading_diagnostic;++d[4];if(!d[6])d[6]=lastTick+1;d[7]=lastTick+1;
  }else ++pvzh1_level_loading_diagnostic[11];
 }
 drawing=false;
}
}
extern "C" void pvzh1_level_loading_prepare(){if(cache.tried)return;cache.tried=true;if(!Load()){Drop();cache.tried=true;++pvzh1_level_loading_diagnostic[11];}}
extern "C" void pvzh1_level_loading_begin(){if(depth++)return;pvzh1_level_loading_prepare();stage=0;auto& d=pvzh1_level_loading_diagnostic;d[0]=1;d[1]=1;++d[2];d[6]=0;d[8]=SDL_GetTicks()+1;Draw(true);}
extern "C" void pvzh1_level_loading_step(unsigned value){if(!depth)return;stage=std::max(stage,std::min(value,99u));Draw(false);}
extern "C" int pvzh1_level_loading_progress(){if(!depth)return 0;Draw(false);return 1;}
extern "C" void pvzh1_level_loading_end(){
 if(!depth)return;if(depth==1){stage=100;Draw(true);}if(--depth)return;Drop();auto& d=pvzh1_level_loading_diagnostic;d[1]=0;++d[3];d[9]=SDL_GetTicks()+1;
 if(gLawnApp&&gLawnApp->mWidgetManager)gLawnApp->mWidgetManager->MarkAllDirty();
}
