/* Target-size baked first-player opening. LGPL-3.0-or-later. */
#include "IntroMovie.h"
#include "IntroFrames.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CutScene.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/MemoryImage.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/DirtyCanvas.h"
#include "Common.h"
#include "h1_host.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include "zlib.h"
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" {volatile uint32_t pvzh1_intro_movie_diagnostic[16];
// Fill calls/ms/bytes, decode calls/unpack ms/reconstruct ms/copied bytes,
// maximum unpack ms. All timers use the existing calibrated native clock.
volatile uint32_t pvzh1_intro_cost_diagnostic[16];
volatile uint32_t pvzh1_intro_memory_diagnostic[8];}
namespace {
constexpr unsigned Pixels=480*272,Frames=348,CacheCapacity=9*1024*1024,ReadBufferBytes=65536,PageBytes=16384,CachePages=CacheCapacity/PageBytes;
struct Packet {unsigned offset,size;int shift;};
Board* owner=nullptr;Packet* packets=nullptr;uint16_t *before=nullptr,*current=nullptr,*delta=nullptr;
FILE* input=nullptr;uint8_t *cache[CachePages]{},*readBuffer=nullptr;unsigned allocatedPages=0;unsigned nextLoaded=0,loadingBytes=0,occupied=0,writeAt=0,bufferBytes=0,lowWater=0;
int frame=-1,shownFrame=-1;
bool backingValid=false;
#ifdef PVZ_HOST_RUNNER
bool enabled=true;
#endif
uint32_t Word(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
[[noreturn]] void Fail(const char* why){++pvzh1_intro_movie_diagnostic[7];H1IntroMovieRelease();throw std::runtime_error(why);}
void Resident(){unsigned bytes=packets?Frames*sizeof(Packet):0;
 bytes+=allocatedPages*PageBytes;if(before)bytes+=bufferBytes;if(current)bytes+=bufferBytes;if(delta)bytes+=bufferBytes;if(readBuffer)bytes+=ReadBufferBytes;
 pvzh1_intro_movie_diagnostic[1]=bytes;pvzh1_intro_movie_diagnostic[9]=std::max(bytes,unsigned(pvzh1_intro_movie_diagnostic[9]));pvzh1_intro_movie_diagnostic[15]=occupied;}
void DropCache(){for(auto& page:cache){std::free(page);page=nullptr;}allocatedPages=0;}
void CloseInput(){if(input)std::fclose(input);input=nullptr;std::free(readBuffer);readBuffer=nullptr;}
void Fill(unsigned budget){const auto started=pvzp_ticks_ms();unsigned read=0;while(budget&&nextLoaded<Frames&&occupied<CacheCapacity){
 auto& p=packets[nextLoaded];
 if(!loadingBytes){uint8_t item[8];if(!input||std::fread(item,1,8,input)!=8)Fail("Unable to read first-player frame header");p.shift=int32_t(Word(item));p.size=Word(item+4);p.offset=writeAt;}
 const unsigned count=std::min({budget,p.size-loadingBytes,CacheCapacity-occupied});
 const unsigned first=std::min(count,PageBytes-writeAt%PageBytes);
 if(std::fread(cache[writeAt/PageBytes]+writeAt%PageBytes,1,first,input)!=first)Fail("Unable to read first-player frame");
 writeAt=(writeAt+first)%CacheCapacity;occupied+=first;loadingBytes+=first;budget-=first;read+=first;
 if(loadingBytes==p.size){++nextLoaded;loadingBytes=0;}
 }if(nextLoaded==Frames)CloseInput();Resident();++pvzh1_intro_cost_diagnostic[0];pvzh1_intro_cost_diagnostic[1]+=pvzp_ticks_ms()-started;pvzh1_intro_cost_diagnostic[2]+=read;}
void Decode(uint32_t* out=nullptr){const unsigned next=unsigned(frame+1);if(next>=Frames)Fail("Incomplete first-player frames");
 if(next>=nextLoaded)Fill(bufferBytes);if(next>=nextLoaded||!allocatedPages)Fail("First-player frame buffer unavailable");auto& p=packets[next];
 const uint8_t* source=cache[p.offset/PageBytes]+p.offset%PageBytes;
 if(p.size>PageBytes-p.offset%PageBytes){pvzh1_intro_cost_diagnostic[6]+=p.size;auto* temp=reinterpret_cast<uint8_t*>(current);unsigned at=p.offset,left=p.size;
  while(left){const unsigned count=std::min(left,PageBytes-at%PageBytes);std::memcpy(temp,cache[at/PageBytes]+at%PageBytes,count);temp+=count;left-=count;at=(at+count)%CacheCapacity;}source=reinterpret_cast<uint8_t*>(current);}
 const auto unpackStarted=pvzp_ticks_ms();if(!H1IntroUnpack(source,p.size,delta))Fail("Unable to expand first-player frame");
 const auto unpackMs=pvzp_ticks_ms()-unpackStarted;pvzh1_intro_cost_diagnostic[4]+=unpackMs;pvzh1_intro_cost_diagnostic[7]=std::max(unsigned(pvzh1_intro_cost_diagnostic[7]),unpackMs);
 const auto reconstructStarted=pvzp_ticks_ms();
 if(out)H1IntroReconstructExpand(p.shift,delta,before,current,out);else H1IntroReconstruct(p.shift,delta,before,current);
 ++pvzh1_intro_cost_diagnostic[3];pvzh1_intro_cost_diagnostic[5]+=pvzp_ticks_ms()-reconstructStarted;
 std::swap(before,current);occupied-=p.size;++frame;++pvzh1_intro_movie_diagnostic[2];pvzh1_intro_movie_diagnostic[3]=frame;
 if(frame==int(Frames-1)){DropCache();}Resident();
}
}
void H1IntroMovieRelease(Board* board){if(board&&board!=owner)return;
 CloseInput();DropCache();std::free(packets);std::free(before);std::free(current);std::free(delta);
 packets=nullptr;before=current=delta=nullptr;owner=nullptr;frame=shownFrame=-1;backingValid=false;nextLoaded=loadingBytes=occupied=writeAt=bufferBytes=lowWater=0;
 pvzh1_intro_movie_diagnostic[0]=0;pvzh1_intro_movie_diagnostic[1]=0;++pvzh1_intro_movie_diagnostic[6];}
bool H1IntroMovieRequired(){
#ifdef PVZ_HOST_RUNNER
 return enabled;
#else
 return true;
#endif
}
bool H1IntroMovieActive(Board* board){return board&&board==owner&&packets&&before;}
const uint16_t* H1IntroMovieNativePixels(uint32_t* generation,uint32_t* index){
 if(!owner||frame<0||!pvzh1_intro_movie_diagnostic[12]||owner->mPaused||!owner->mApp->mDialogList.empty()||owner->mApp->mGameScene!=GameScenes::SCENE_LEVEL_INTRO)return nullptr;
 if(generation)*generation=pvzh1_intro_movie_diagnostic[5];if(index)*index=frame;return before;
}
void H1IntroMoviePresented(){if(owner&&frame!=shownFrame){++pvzh1_intro_movie_diagnostic[11];shownFrame=frame;}}
void H1IntroMovieCompleted(Board* board){if(H1IntroMovieActive(board)){
 pvzh1_intro_movie_diagnostic[13]=pvzp_ticks_ms()+1;pvzh1_intro_movie_diagnostic[14]=board->mCutScene->mCutsceneTime;}}
bool H1IntroMoviePrepare(Board* board){
#ifdef PVZ_HOST_RUNNER
 if(!enabled)return false;
#endif
 if(!board||board->mApp->mGameMode!=GAMEMODE_INTRO){H1IntroMovieRelease();return false;}
 if(H1IntroMovieActive(board))return true;H1IntroMovieRelease();
 // The preceding menu/loading draw has returned. Its reconstructible source
 // pixels are no longer borrowed; retain the screen and music, not old UI or
 // previous-level textures beside the movie's read-ahead window.
 pvzh1_cache_complete_draw_batch();
 const auto previousImages=Sexy::H1ImageCacheGetStats();
 pvzh1_intro_memory_diagnostic[0]=PageBytes;pvzh1_intro_memory_diagnostic[1]=CacheCapacity;
 pvzh1_intro_memory_diagnostic[2]=static_cast<uint32_t>(previousImages.residentBytes);
 Sexy::H1ImageCacheReleaseUI();Sexy::H1ImageCacheSetBudget(0);Sexy::H1ImageCacheEndFrame();
 pvzh1_intro_memory_diagnostic[3]=static_cast<uint32_t>(Sexy::H1ImageCacheGetStats().residentBytes);
 Sexy::H1ImageCacheSetBudget(previousImages.budgetBytes);
 input=std::fopen(Sexy::GetResourcePath("first-intro.h1if").c_str(),"rb");
 if(!input)Fail("Missing first-intro.h1if; install the matching resource pack");
 readBuffer=static_cast<uint8_t*>(std::malloc(ReadBufferBytes));
 if(!readBuffer||std::setvbuf(input,reinterpret_cast<char*>(readBuffer),_IOFBF,ReadBufferBytes))Fail("Not enough memory for opening file buffer");
 uint8_t header[64]{};bool okay=std::fread(header,1,64,input)==64;
 okay=okay&&Word(header)==0x46493148&&Word(header+4)==2&&Word(header+8)==480&&Word(header+12)==272&&Word(header+16)==Frames&&Word(header+20)==40&&Word(header+24)==13890;
 const unsigned bytes=Word(header+32),maxPacket=Word(header+28);okay=okay&&bytes>Frames*8&&bytes<=24*1024*1024&&maxPacket<=Pixels*2+2048;
 bufferBytes=std::max(Pixels*2,maxPacket);lowWater=3*maxPacket;
 if(okay){packets=static_cast<Packet*>(std::calloc(Frames,sizeof(Packet)));before=static_cast<uint16_t*>(std::calloc(1,bufferBytes));current=static_cast<uint16_t*>(std::malloc(bufferBytes));delta=static_cast<uint16_t*>(std::malloc(bufferBytes));okay=packets&&before&&current&&delta;}
 if(!okay)Fail("Not enough memory for opening frame buffers");
 // Packet metadata is read together with its payload during prefetch.
 // The installer already verified the complete movie.
 for(auto& page:cache){page=static_cast<uint8_t*>(std::malloc(PageBytes));if(!page){pvzh1_intro_memory_diagnostic[5]=allocatedPages;++pvzh1_intro_memory_diagnostic[6];Fail("Not enough memory for opening read-ahead page");}++allocatedPages;}
 pvzh1_intro_memory_diagnostic[4]=allocatedPages;
 owner=board;frame=-1;nextLoaded=loadingBytes=occupied=writeAt=0;
 pvzh1_intro_movie_diagnostic[12]=pvzh1_intro_movie_diagnostic[13]=pvzh1_intro_movie_diagnostic[14]=0;Fill(CacheCapacity);Decode();
 pvzh1_intro_movie_diagnostic[0]=1;++pvzh1_intro_movie_diagnostic[5];return true;
}
bool H1IntroMovieDraw(Board* board,Sexy::Graphics* g){if(!H1IntroMovieActive(board))return false;
 auto* screen=dynamic_cast<Sexy::GLImage*>(g->mDestImage);auto* physical=screen?Sexy::H1GetPhysicalScreen(screen):nullptr;
 if(!physical||!physical->Active())Fail("First-player frames require the physical screen");
 const int time=std::max(0,board->mCutScene->mCutsceneTime),target=std::min(int(Frames-1),time/40);
 if(!pvzh1_intro_movie_diagnostic[12])pvzh1_intro_movie_diagnostic[12]=pvzp_ticks_ms()+1;
 auto* destination=physical->Target();
 const uint64_t key[2]={0x494e54524f46524dull,uintptr_t(board)};
 const bool root=h1DirtyCanvas.Root(destination,key,sizeof(key));
 bool direct=false;
#ifndef PVZ_HOST_RUNNER
 direct=H1IntroMovieNativePixels(nullptr,nullptr)!=nullptr;
#endif
 if(direct){while(frame<target)Decode();backingValid=false;}
 else if(!backingValid||frame<target||!root||!h1DirtyCanvas.limited){H1CanvasProtectedPixels protect(destination);auto* out=destination->GetBits();
  if(frame<target){while(frame<target)Decode(frame+1==target?out:nullptr);}else H1IntroExpand(before,out);
  destination->BitsChanged();h1DirtyCanvas.MarkAll();backingValid=true;++pvzh1_intro_movie_diagnostic[10];}
 // Keep card I/O outside the moving interval while the prefetched window
 // suffices. Refill only near its end, in bounded batches, servicing audio
 // through the ordinary filesystem adapter.
 if(occupied<lowWater)Fill(bufferBytes);physical->NoteDraw();++pvzh1_intro_movie_diagnostic[4];pvzh1_intro_movie_diagnostic[8]=time;
#ifdef PVZ_HOST_RUNNER
 H1IntroMoviePresented();
#endif
 return true;
}
#ifdef PVZ_HOST_RUNNER
void H1IntroMovieTestEnable(bool value){enabled=value;if(!value)H1IntroMovieRelease();}
#endif
