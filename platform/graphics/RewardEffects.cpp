#include "TrustedResources.h"
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
#include <cstring>
#include <new>
#include <zlib.h>
extern "C" {volatile uint32_t pvzh1_reward_effect_diagnostic[12];}
namespace {
struct Cache {uint8_t** pages=nullptr;uint32_t* palette=nullptr;uint8_t* frame[2]{};uint32_t local[2][256]{};unsigned localCount[2]{},frameStart[2]{},passes[2]{};unsigned capacity[2]{},current[2]{~0u,~0u},frameBytes[2]{};unsigned bytes=0,count=0,colors=0,table=0,bounds=0;bool tried=false;} cache;
struct Live {PvzpParticleSystem* system=nullptr;unsigned age=0,kind=0;} live[8];
bool enabled=true,starburstSpawn=false;
unsigned U16(const uint8_t* p){return unsigned(p[0])|(unsigned(p[1])<<8);}
unsigned U32(const uint8_t* p){return U16(p)|(U16(p+2)<<16);}
struct Reader {unsigned at;const uint8_t* plain=nullptr;uint8_t Byte(){unsigned p=at++;return plain?plain[p]:cache.pages[p>>12][p&4095];}
 unsigned Short(){unsigned n=Byte();return n|(Byte()<<8);}unsigned Word(){unsigned n=Short();return n|(Short()<<16);}
 unsigned Index(){unsigned n=Byte();return n<128?n:(n&127)|(Byte()<<7);}};
void Drop(){if(cache.pages){for(unsigned i=0;i<(cache.bytes+4095)/4096;++i)delete[] cache.pages[i];delete[] cache.pages;}delete[] cache.palette;for(auto* p:cache.frame)delete[] p;cache={};pvzh1_reward_effect_diagnostic[0]=0;}
bool Frame(unsigned index){
 unsigned kind=index>=181;if(cache.current[kind]==index)return true;
 Reader entry{cache.table+index*16};if(entry.Word()!=kind)return false;entry.Word();unsigned start=entry.Word(),bytes=entry.Word();if(start>cache.bytes||bytes<5||bytes>cache.bytes-start)return false;
 Reader input{start};unsigned expected=input.Word(),end=start+bytes,written=0;auto* out=cache.frame[kind];if(!out||expected>cache.capacity[kind])return false;
 auto length=[&](unsigned n){if(n==15){unsigned c;do{if(input.at>=end)return ~0u;c=input.Byte();if(c>expected||n>expected-c)return ~0u;n+=c;}while(c==255);}return n;};
 while(input.at<end){unsigned token=input.Byte(),literal=length(token>>4);if(literal>expected-written||literal>end-input.at)return false;while(literal--)out[written++]=input.Byte();
  if(input.at==end)break;if(input.at+2>end)return false;unsigned offset=input.Short();if(!offset||offset>written)return false;unsigned match=length(token&15);if(match>expected-written||match+4>expected-written)return false;match+=4;
  while(match--){out[written]=out[written-offset];++written;}
 }
 if(written!=expected||written<10)return false;Reader header{0,out};unsigned passes=header.Word(),colors=header.Word();if(passes>5||!colors||colors>256||8+colors*2>written)return false;
 for(unsigned i=0;i<colors;++i){unsigned id=header.Short();if(id>=cache.colors||(!i&&id))return false;cache.local[kind][i]=cache.palette[id];}
 cache.localCount[kind]=colors;cache.frameStart[kind]=header.at;cache.passes[kind]=passes;cache.current[kind]=index;cache.frameBytes[kind]=written;++pvzh1_reward_effect_diagnostic[9];return true;
}
bool InitializeMetadata(){
 Reader r{0};unsigned h[16];for(auto& n:h)n=r.Word();
 cache.count=h[5];cache.colors=h[4];cache.table=h[7];cache.bounds=h[15];cache.palette=new(std::nothrow) uint32_t[h[4]];if(!cache.palette)return false;
 r.at=64;for(unsigned i=0;i<h[4];++i)cache.palette[i]=r.Word();
 for(unsigned i=0;i<cache.count;++i){Reader e{cache.table+i*16};unsigned kind=e.Word();e.Word();unsigned start=e.Word();e.Word();Reader n{start};cache.capacity[kind]=std::max(cache.capacity[kind],n.Word());}
 for(unsigned k=0;k<2;++k){cache.frame[k]=new(std::nothrow) uint8_t[cache.capacity[k]];if(!cache.frame[k])return false;}
 return true;
}

bool Load(const char* alternate=nullptr){
 auto path=Sexy::GetResourcePath("reward-effects.h1re");FILE* f=fopen(alternate?alternate:path.c_str(),"rb");if(!f)return false;
 unsigned char header[16];bool ok=fread(header,1,16,f)==16&&U32(header)==0x5a523148&&U32(header+4)==1;
 unsigned bytes=ok?U32(header+8):0,expected=ok?U32(header+12):0;
 if(bytes<64||bytes>12*1024*1024)ok=false;
 if(ok){cache.bytes=bytes;cache.pages=new(std::nothrow) uint8_t*[(bytes+4095)/4096]{};if(!cache.pages)ok=false;}
 z_stream stream{};bool initialized=ok&&H1ResourceInflateInit(&stream)==Z_OK;ok=ok&&initialized;unsigned char input[4096];int state=Z_OK;
 for(unsigned page=0;ok&&page<(bytes+4095)/4096;++page){unsigned n=std::min(4096u,bytes-page*4096);cache.pages[page]=new(std::nothrow) uint8_t[n];if(!cache.pages[page]){ok=false;break;}
  stream.next_out=cache.pages[page];stream.avail_out=n;
  while(ok&&stream.avail_out){if(!stream.avail_in){stream.avail_in=fread(input,1,sizeof(input),f);stream.next_in=input;if(!stream.avail_in){ok=false;break;}}
   state=inflate(&stream,Z_NO_FLUSH);if(state!=Z_OK&&state!=Z_STREAM_END)ok=false;if(state==Z_STREAM_END&&stream.avail_out)ok=false;}
 }
 if(ok&&state!=Z_STREAM_END){unsigned char extra;stream.next_out=&extra;stream.avail_out=1;state=inflate(&stream,Z_FINISH);}
 if(ok&&(state!=Z_STREAM_END||stream.total_out!=bytes||stream.avail_in||fgetc(f)!=EOF))ok=false;
 if(initialized)inflateEnd(&stream);fclose(f);if(ok)ok=InitializeMetadata();if(!ok){Drop();return false;}
 pvzh1_reward_effect_diagnostic[0]=cache.bytes+cache.colors*4+((cache.bytes+4095)/4096)*sizeof(void*)+cache.capacity[0]+cache.capacity[1];pvzh1_reward_effect_diagnostic[10]=cache.capacity[0]+cache.capacity[1];++pvzh1_reward_effect_diagnostic[1];return true;
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
 using namespace Sexy;auto* screen=H1GetPhysicalScreen(dynamic_cast<GLImage*>(g->mDestImage));if(!cache.pages||!screen||!screen->Active()||!screen->mGLInterface->mTransformStack.empty()||g->mScaleX!=1||g->mScaleY!=1||g->mColorizeImages||g->mDrawMode!=Graphics::DRAWMODE_NORMAL)return false;
 auto* target=screen->Target();if(target!=h1DirtyCanvas.owner||target->mWidth!=480||target->mHeight!=272)return false;
 unsigned frame=std::min(kind?30u:180u,(age*3+5)/10),index=(kind?181:0)+frame;Reader bounds{cache.bounds+index*8};int minx=bounds.Short(),maxx=bounds.Short(),miny=bounds.Short(),maxy=bounds.Short();if(cx<minx||cx>maxx||cy<miny||cy>maxy)return false;
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
void H1RewardEffectsPrepare(){if(!enabled||cache.tried)return;cache.tried=true;if(!Load()){cache.tried=true;++pvzh1_reward_effect_diagnostic[2];}}
bool H1RewardEffectsBegin(PvzpParticleSystem* s){
 if(!enabled||!cache.pages||!gLawnApp||!gLawnApp->mBoard||s->mDead)return false;
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
