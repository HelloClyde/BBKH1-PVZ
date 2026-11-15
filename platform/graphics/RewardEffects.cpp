/* The asset contains finished shader contributions, not source textures.
 * Only a clock, LCD translation and bounded pixel blending run on H1.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "RewardEffects.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "PvzpLib/PvzpParticle.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "Common.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <new>
#include <stdexcept>
extern "C" {volatile uint32_t pvzh1_reward_effect_diagnostic[12];}
namespace {
struct Record {uint32_t offset,bytes;int16_t bounds[4];};
struct Cache {int file=-1;Record* records=nullptr;uint8_t* input=nullptr;uint32_t* palette=nullptr;uint8_t* frame[2]{};uint32_t local[2][256]{};unsigned localCount[2]{},frameStart[2]{},passes[2]{};unsigned capacity[2]{},current[2]{~0u,~0u},frameBytes[2]{};unsigned inputBytes=0,count=0,colors=0;bool tried=false;} cache;
struct Live {PvzpParticleSystem* system=nullptr;unsigned age=0,kind=0;} live[8];
bool enabled=true,starburstSpawn=false;
unsigned U16(const uint8_t* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
unsigned U32(const uint8_t* p){return U16(p)|(U16(p+2)<<16);}
struct Reader {unsigned at;const uint8_t* plain;uint8_t Byte(){return plain[at++];}
 unsigned Short(){unsigned n=Byte();return n|(Byte()<<8);}unsigned Word(){unsigned n=Short();return n|(Short()<<16);}
 unsigned Index(){unsigned n=Byte();return n<128?n:(n&127)|(Byte()<<7);}};
void Drop(){if(cache.file>=0)close(cache.file);delete[] cache.records;delete[] cache.input;delete[] cache.palette;for(auto* p:cache.frame)delete[] p;cache={};pvzh1_reward_effect_diagnostic[0]=0;}
bool Frame(unsigned index){
 unsigned kind=index>=181;if(cache.current[kind]==index)return true;
 const auto& record=cache.records[index];
 if(cache.current[kind]<cache.count&&cache.records[cache.current[kind]].offset==record.offset){cache.current[kind]=index;return true;}
 if(lseek(cache.file,record.offset,SEEK_SET)!=off_t(record.offset)||read(cache.file,cache.input,record.bytes)!=ssize_t(record.bytes))
  throw std::runtime_error("Unable to read offline reward frame");
 ++pvzh1_reward_effect_diagnostic[11];
 Reader input{0,cache.input};unsigned expected=input.Word(),end=record.bytes,written=0;auto* out=cache.frame[kind];if(!out||expected>cache.capacity[kind])return false;
 auto length=[&](unsigned n){if(n==15){unsigned c;do{if(input.at>=end)return ~0u;c=input.Byte();if(c>expected||n>expected-c)return ~0u;n+=c;}while(c==255);}return n;};
 while(input.at<end){unsigned token=input.Byte(),literal=length(token>>4);if(literal>expected-written||literal>end-input.at)return false;while(literal--)out[written++]=input.Byte();
  if(input.at==end)break;if(input.at+2>end)return false;unsigned offset=input.Short();if(!offset||offset>written)return false;unsigned match=length(token&15);if(match>expected-written||match+4>expected-written)return false;match+=4;
  while(match--){out[written]=out[written-offset];++written;}
 }
 if(written!=expected||written<10)return false;Reader header{0,out};unsigned passes=header.Word(),colors=header.Word();if(passes>5||!colors||colors>256||8+colors*2>written)return false;
 for(unsigned i=0;i<colors;++i){unsigned id=header.Short();if(id>=cache.colors||(!i&&id))return false;cache.local[kind][i]=cache.palette[id];}
 cache.localCount[kind]=colors;cache.frameStart[kind]=header.at;cache.passes[kind]=passes;cache.current[kind]=index;cache.frameBytes[kind]=written;++pvzh1_reward_effect_diagnostic[9];return true;
}
bool Load(const char* alternate=nullptr){
 const auto path=Sexy::GetResourcePath("reward-effects.h1re");int flags=O_RDONLY;
#ifdef O_BINARY
 flags|=O_BINARY;
#endif
 cache.file=open(alternate?alternate:path.c_str(),flags);if(cache.file<0)return false;
 // This is an offline-indexed trusted pack. No whole-file inflation, CRC,
 // decoded bank or per-frame allocation is needed on the device.
 // Use read directly: H1 newlib built with -Os refills a one-byte
 // unbuffered stdio buffer and would issue one SD read per byte.
 auto exact=[&](void* p,unsigned n){return read(cache.file,p,n)==ssize_t(n);};
 auto seek=[&](unsigned at){return lseek(cache.file,at,SEEK_SET)==off_t(at);};
 uint8_t header[64],entries[212*16],bounds[212*8];
 bool ok=exact(header,64)&&U32(header)==0x45523148&&U32(header+4)==5&&U32(header+20)==212;
 if(!ok){Drop();return false;}
 cache.count=212;cache.colors=U32(header+16);
 cache.records=new(std::nothrow) Record[cache.count];cache.palette=new(std::nothrow) uint32_t[cache.colors];
 if(!cache.records||!cache.palette){Drop();return false;}
 ok=exact(cache.palette,cache.colors*4);
 ok=ok&&seek(U32(header+28))&&exact(entries,sizeof(entries));
 ok=ok&&seek(U32(header+60))&&exact(bounds,sizeof(bounds));
 if(!ok){Drop();return false;}
 for(unsigned i=0;i<212;++i){auto& r=cache.records[i];const auto* e=entries+i*16;r.offset=U32(e+8);r.bytes=U32(e+12);
  for(unsigned j=0;j<4;++j)r.bounds[j]=int16_t(U16(bounds+i*8+j*2));
  cache.capacity[i>=181]=std::max(cache.capacity[i>=181],U32(e));cache.inputBytes=std::max(cache.inputBytes,r.bytes);
 }
 cache.input=new(std::nothrow) uint8_t[cache.inputBytes];
 for(unsigned k=0;k<2;++k)cache.frame[k]=new(std::nothrow) uint8_t[cache.capacity[k]];
 if(!cache.input||!cache.frame[0]||!cache.frame[1]||!Frame(0)||!Frame(181)){Drop();return false;}
 pvzh1_reward_effect_diagnostic[0]=sizeof(Record)*cache.count+cache.colors*4+cache.inputBytes+cache.capacity[0]+cache.capacity[1];
 cache.tried=true;
 pvzh1_reward_effect_diagnostic[10]=cache.capacity[0]+cache.capacity[1];++pvzh1_reward_effect_diagnostic[1];return true;
}
Live* Find(PvzpParticleSystem* s){for(auto& l:live)if(l.system==s)return &l;return nullptr;}
__attribute__((always_inline)) inline uint32_t Blend(uint32_t d,uint32_t s,unsigned mode){
 if(mode){
  // Saturate packed R/B lanes together. No per-pixel function prologue or
  // three separate channel extraction/clamp sequences on the H1 MIPS CPU.
  unsigned rb=(d&0xff00ffu)+(s&0xff00ffu),g=(d&0xff00u)+(s&0xff00u);
  unsigned carry=rb&0x1000100u;rb=(rb| (carry-(carry>>8)))&0xff00ffu;
  carry=g&0x10000u;g=(g|(carry-(carry>>8)))&0xff00u;
  return (d&0xff000000u)|rb|g;
 }
 if(s>>24==255)return s;unsigned a=s>>24;return 0xff000000u|((s&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));
}
bool Draw(unsigned kind,unsigned age,Sexy::Graphics* g,int cx,int cy){
 using namespace Sexy;auto* screen=H1GetPhysicalScreen(dynamic_cast<GLImage*>(g->mDestImage));if(!cache.records||!screen||!screen->Active()||!screen->mGLInterface->mTransformStack.empty()||g->mScaleX!=1||g->mScaleY!=1||g->mColorizeImages||g->mDrawMode!=Graphics::DRAWMODE_NORMAL)return false;
 auto* target=screen->Target();if(target!=h1DirtyCanvas.owner||target->mWidth!=480||target->mHeight!=272)return false;
 unsigned frame=std::min(kind?30u:180u,(age*3+5)/10),index=(kind?181:0)+frame;const auto* bounds=cache.records[index].bounds;int minx=bounds[0],maxx=bounds[1],miny=bounds[2],maxy=bounds[3];if(cx<minx||cx>maxx||cy<miny||cy>maxy)return false;
 if(!Frame(index))return false;Reader data{cache.frameStart[kind],cache.frame[kind]};
 unsigned passes=cache.passes[kind];const int dx=cx-480,dy=cy-272;auto clip=screen->MapRect(g->mClipRect).Intersection(Rect(0,0,480,272));
 H1CanvasKnownDraw damage(target,clip.mX,clip.mY,clip.mWidth,clip.mHeight);auto* pixels=target->mBits.get();
 for(unsigned pass=0;pass<passes;++pass){unsigned mode=data.Word(),length=data.Word(),base=data.at;
  Reader lengths{base,data.plain};unsigned rowAt=base+1088;int top=std::max(0,clip.mY-dy),bottom=std::min(544,clip.mY+clip.mHeight-dy);
  for(int sy=0;sy<bottom;++sy){unsigned bytes=lengths.Short();Reader row{rowAt,data.plain};rowAt+=bytes;if(sy<top)continue;int y=sy+dy;unsigned tokens=row.Short();int x=dx;
   for(unsigned t=0;t<tokens;++t){unsigned run=row.Short(),n=run&32767;int first=std::max(x,clip.mX),last=std::min(x+int(n),clip.mX+clip.mWidth);
    if(run&32768){unsigned color=row.Byte();if(color&&first<last){uint32_t s=cache.local[kind][color];auto* p=pixels+y*480+first;unsigned count=last-first;
      if((mode&&s==0xffffffu)||(!mode&&s>>24==255)){s|=0xff000000u;while(count>=8){p[0]=p[1]=p[2]=p[3]=p[4]=p[5]=p[6]=p[7]=s;p+=8;count-=8;}while(count--)*p++=s;}
      else while(count--){*p=Blend(*p,s,mode);++p;}}}
    else for(unsigned j=0;j<n;++j){unsigned color=row.Byte();int at=x+int(j);if(color&&at>=first&&at<last){auto& d=pixels[y*480+at];d=Blend(d,cache.local[kind][color],mode);}}
    x+=n;
   }
  }data.at=base+length;
 }
 target->BitsChanged();screen->NoteDraw();++pvzh1_reward_effect_diagnostic[4];pvzh1_reward_effect_diagnostic[5]=kind;pvzh1_reward_effect_diagnostic[6]=age;return true;
}
void Fallback(PvzpParticleSystem* s){auto* l=Find(s);if(!l)return;unsigned age=l->age;*l={};++pvzh1_reward_effect_diagnostic[7];while(age--&&!s->mDead)s->Update();}
}
void H1RewardEffectsPrepare(){if(!enabled||cache.tried)return;cache.tried=true;if(!Load()){cache.tried=true;++pvzh1_reward_effect_diagnostic[2];throw std::runtime_error("Unable to prepare offline reward frames");}}
bool H1RewardEffectsBegin(PvzpParticleSystem* s){
 if(!enabled||!cache.records||!gLawnApp||!gLawnApp->mBoard||s->mDead)return false;
 unsigned kind=s->mEffectType==PARTICLE_STARBURST?1:0;
 if(kind?!starburstSpawn:(s->mEffectType!=PARTICLE_SEED_PACKET_PICKUP||gLawnApp->mBoard->mBoardFadeOutCounter<0))return false;
 for(auto& l:live)if(!l.system||l.system==s){l={s,0,kind};++pvzh1_reward_effect_diagnostic[3];return true;}return false;
}
bool H1RewardEffectsUpdate(PvzpParticleSystem* s){auto* l=Find(s);if(!l)return false;if(!s->mDontUpdate){if(++l->age>(l->kind?100u:600u))s->ParticleSystemDie();else ++pvzh1_reward_effect_diagnostic[8];}return true;}
void H1RewardEffectsForget(PvzpParticleSystem* s){if(auto* l=Find(s))*l={};}
bool H1RewardEffectsDraw(PvzpParticleSystem* s,Sexy::Graphics* g){
 auto* l=Find(s);if(!l)return false;if(s->mDead)return true;
 auto* node=s->mEmitterList.mHead;if(!node){Fallback(s);return false;}auto* emitter=s->mParticleHolder->mEmitters.DataArrayGet(unsigned(node->mValue));
 for(auto* n=node;n;n=n->mNext){auto* e=s->mParticleHolder->mEmitters.DataArrayGet(unsigned(n->mValue));if(e->mImageOverride||e->mFrameOverride!=-1||e->mExtraAdditiveDrawOverride||e->mColorOverride!=Sexy::Color::White){Fallback(s);return false;}}
 int x=int(std::lround((emitter->mSystemCenter.x+g->mTransX)*480.f/1060.f)),y=int(std::lround((emitter->mSystemCenter.y+g->mTransY)*272.f/600.f));
 if(Draw(l->kind,l->age,g,x,y))return true;Fallback(s);return false;
}
PvzpParticleSystem* H1RewardStarburst(LawnApp* app,float x,float y,int order){bool old=starburstSpawn;starburstSpawn=true;auto* effect=app->AddPvzpParticle(x,y,order,PARTICLE_STARBURST);starburstSpawn=old;return effect;}
extern "C" void pvzh1_reward_effect_shutdown(){for(auto& l:live)l={};Drop();}
#ifdef PVZ_HOST_RUNNER
void H1RewardEffectsTestEnable(bool value){enabled=value;}
bool H1RewardEffectsTestDraw(unsigned kind,unsigned age,Sexy::Graphics* g,int x,int y){return Draw(kind,age,g,x,y);}
bool H1RewardEffectsTestLoad(const char* path){Drop();return Load(path);}
#endif
