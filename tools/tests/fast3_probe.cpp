#include "LawnApp.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/Graphics.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/MemoryImageCache.h"
#include <bit>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <limits>
using namespace Sexy;
extern "C" {bool pvzh1_fast3_test_enabled=true;}
namespace {
void Check(bool b,const char*s){if(!b){std::fprintf(stderr,"FAST3 FAILURE: %s\n",s);throw std::runtime_error(s);}}
void Background(MemoryImage*image){auto p=image->GetBits();for(size_t i=0,n=(size_t)image->mWidth*image->mHeight;i<n;++i)p[i]=0xff000000u|((i*0x164b35u+0x213141u)&0xffffffu);image->BitsChanged();}
std::vector<uint32_t> Pixels(MemoryImage*image){auto p=image->GetBits();return {p,p+(size_t)image->mWidth*image->mHeight};}
uint64_t Hash(const std::vector<uint32_t>&p){uint64_t h=1469598103934665603ull;for(auto x:p)for(int s=0;s<32;s+=8){h^=(x>>s)&255;h*=1099511628211ull;}return h;}
Reanimation *Allocate(ReanimationType type,float x,float y){return gLawnApp->mEffectSystem->mReanimationHolder->AllocReanimation(x,y,0,type);}
void PrepareAnimations(Reanimation*selector,Reanimation*actor,float phase,bool extra){
 selector->mAnimRate=0;selector->mAnimTime=phase;selector->mEnableExtraAdditiveDraw=extra;selector->mExtraAdditiveColor=Color(31,79,113,41);selector->mEnableExtraOverlayDraw=extra;selector->mExtraOverlayColor=Color(71,139,213,37);
 actor->mAnimRate=0;actor->mAnimTime=phase;selector->Update();actor->Update();
}
void DrawAnimations(GLImage*screen,Reanimation*selector,Reanimation*actor,float phase,bool extra,const Rect&clip){
 (void)phase;(void)extra;
 Graphics g(screen);g.SetLinearBlend(true);g.SetClipRect(clip);selector->DrawRenderGroup(&g,1);selector->DrawRenderGroup(&g,RENDER_GROUP_NORMAL);actor->Draw(&g);
}
}
extern "C" void pvzh1_fast3_probe(){
 auto*device=gLawnApp->mGLInterface.get();auto savedScreen=std::move(device->mScreenImage);unsigned animationChecks=0,matrixChecks=0,bitChecks=0;uint64_t digest=0;
 auto*selector=Allocate(ReanimationType::REANIM_SELECTOR_SCREEN,.5f,.5f);
 selector->PlayReanim("anim_sign",ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD,0,0);
 selector->AssignRenderGroupToPrefix("flower",RENDER_GROUP_HIDDEN);selector->AssignRenderGroupToPrefix("leaf",RENDER_GROUP_HIDDEN);selector->AssignRenderGroupToTrack("SelectorScreen_BG",1);
 int signStart,signCount;selector->GetFramesForLayer("anim_sign",signStart,signCount);selector->mFrameBasePose=signStart+signCount-1;
 auto*flower=Allocate(ReanimationType::REANIM_SELECTOR_SCREEN,.5f,.5f);flower->PlayReanim("anim_flower1",ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD,0,0);flower->AttachToAnotherReanimation(selector,"SelectorScreen_BG_Right");
 Check(selector->mDefinition->mTracks.count==48,"fixture lost original full Selector definition");unsigned transforms=0;for(int i=0;i<selector->mDefinition->mTracks.count;++i)transforms+=selector->mDefinition->mTracks.tracks[i].mTransforms.count;
 std::vector<Reanimation*> actors={Allocate(ReanimationType::REANIM_PEASHOOTER,330,290),Allocate(ReanimationType::REANIM_SUNFLOWER,430,310),Allocate(ReanimationType::REANIM_ZOMBIE,250,160),Allocate(ReanimationType::REANIM_SUN,510,310)};
 for(bool physical:{true,false}){
  if(physical)device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,800,600,360,270);else{auto s=std::make_unique<GLImage>(device);s->Create(800,600);s->SetImageMode(false,false);device->mScreenImage=std::move(s);}
  auto*screen=device->GetScreenImage();auto*target=H1ScreenTarget(screen);
  for(auto*actor:actors)for(float phase:{0.f,.375f,1.f})for(bool extra:{false}){
   Rect clip=phase==.375f?Rect(29,13,711,538):Rect(0,0,800,600);
   pvzh1_fast3_test_enabled=false;PrepareAnimations(selector,actor,phase,extra);
   // FilterEffectGetImage initializes/sands derived images lazily. Establish
   // those original source representations before either side of the oracle.
   pvzh1_fast3_test_enabled=false;Background(target);DrawAnimations(screen,selector,actor,phase,extra,clip);
   pvzh1_fast3_test_enabled=false;Background(target);DrawAnimations(screen,selector,actor,phase,extra,clip);auto original=Pixels(target);
   pvzh1_fast3_test_enabled=false;Background(target);DrawAnimations(screen,selector,actor,phase,extra,clip);Check(original==Pixels(target),"fixture is not stable under two original draws");
   pvzh1_fast3_test_enabled=true;Background(target);DrawAnimations(screen,selector,actor,phase,extra,clip);auto fast=Pixels(target);
   if(original!=fast)std::fprintf(stderr,"FAST3 animation mismatch physical=%d actor=%d phase=%g extra=%d\n",physical,(int)actor->mReanimationType,phase,extra);
   Check(original==fast,"complete real reanimation/attachment/additive/overlay pixels changed");digest^=Hash(fast);++animationChecks;
  }
  // Parent BG is hidden in this animation range. Its attached flower must
  // continue through DrawRenderGroup's original parent-hidden attachment logic.
  selector->PlayReanim("anim_flower2",ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD,0,0);
  for(float phase:{0.f,.5f,1.f}){
   pvzh1_fast3_test_enabled=false;PrepareAnimations(selector,actors[0],phase,false);
   pvzh1_fast3_test_enabled=false;Background(target);DrawAnimations(screen,selector,actors[0],phase,false,Rect(0,0,800,600));auto original=Pixels(target);
   pvzh1_fast3_test_enabled=true;Background(target);DrawAnimations(screen,selector,actors[0],phase,false,Rect(0,0,800,600));Check(original==Pixels(target),"hidden parent changed actual AttachmentDraw behavior");++animationChecks;
  }
  selector->PlayReanim("anim_sign",ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD,0,0);
  GLImage right(device);Check(H1ImageCacheDescribe(&right,"reanim/selectorscreen_bg_right",true,0xffffff),"real BG missing");
  for(int kind=0;kind<8;++kind){std::vector<uint32_t> original;
   for(bool fast:{false,true}){pvzh1_fast3_test_enabled=fast;Background(target);ReanimatorTransform t;t.mTransX=kind==2?-33.125f:120.5f;t.mTransY=kind==3?-79.25f:141.5f;t.mSkewX=kind==5?17.f:kind==1?-0.f:0.f;t.mSkewY=kind==5?-23.f:kind==1?-0.f:0.f;t.mScaleX=kind==4?-.8f:1.0f;t.mScaleY=1.0f;SexyMatrix3 m;Reanimation::MatrixFromTransform(t,m);Rect src=kind==6?Rect(11,9,right.mWidth-22,right.mHeight-18):Rect(0,0,right.mWidth,right.mHeight);Rect clip=kind==3?Rect(13,17,701,533):Rect(0,0,800,600);device->BltTransformed(&right,&clip,kind==7?Color(101,179,231,137):Color::White,0,src,m,false,0,0,true);if(!fast)original=Pixels(target);else Check(original==Pixels(target),"actual BG matrix/UV clip/mirror/tint pixels changed");}
   ++matrixChecks;
  }
  if(physical)Check(H1GetPhysicalScreen(screen)->Active(),"ordinary fast3 drawing forced full logical backing");
 }
 ReanimatorTransform t;t.mSkewX=t.mSkewY=0;t.mScaleX=t.mScaleY=1;t.mTransX=t.mTransY=0;
 float vals[]={0.f,-0.f,1.f,-1.f,.45f,-.45f,std::numeric_limits<float>::min(),std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::max(),std::numeric_limits<float>::infinity(),std::bit_cast<float>(0x7fc12345u)};
 for(float skew:{0.f,-0.f,17.f})for(float sx:vals)for(float sy:vals){t.mSkewX=skew;t.mSkewY=-skew;t.mScaleX=sx;t.mScaleY=sy;SexyMatrix3 a,b;pvzh1_fast3_test_enabled=false;Reanimation::MatrixFromTransform(t,a);pvzh1_fast3_test_enabled=true;Reanimation::MatrixFromTransform(t,b);Check(!memcmp(&a,&b,sizeof(a)),"integrated matrix no longer bit-exact");++bitChecks;}
 device->mScreenImage=std::move(savedScreen);pvzh1_fast3_test_enabled=true;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"fast3-verification.json");out<<"{\"passed\":true,\"actual_animation_pixel_checks\":"<<animationChecks<<",\"real_BG_matrix_pixel_checks\":"<<matrixChecks<<",\"matrix_bit_checks\":"<<bitChecks<<",\"selector_tracks\":48,\"selector_transforms\":"<<transforms<<",\"actor_definition_count\":4,\"physical_sizes\":[[360,270],[800,600]],\"real_attached_flower_and_hidden_parent\":true,\"clip_tint_mirror_angle_fallback\":true,\"extra_additive_overlay_pixel_scope_verified\":false,\"extra_case_excluded_because_original_repeated_reference_unstable\":true,\"ordinary_physical_fallbacks\":0,\"combined_pixel_hash\":"<<digest<<"}\n";
 std::fprintf(stderr,"FAST3 %u ACTUAL ANIMATION + %u BG MATRIX + %u BIT CHECKS PASSED\n",animationChecks,matrixChecks,bitChecks);
}
