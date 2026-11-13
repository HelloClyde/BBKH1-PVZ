/* Independent original-shader operations versus direct LCD sprite playback. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/PvzpParticle.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/CombatSprites.h"
#include "graphics/CombatSpriteFormat.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include "Resources.h"
#include "Common.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
extern "C" volatile uint32_t pvzh1_combat_sprite_diagnostic[12];
namespace {
struct Op{int x,y;uint32_t color;};std::vector<Op> ops;uint32_t*bits;
void Check(bool b,const char*s){if(!b){fprintf(stderr,"COMBAT: %s\n",s);throw std::runtime_error(s);}}
void Capture(uint32_t*p,uint32_t color){int n=int(p-bits);Check(n>=0&&n<480*272,"flat reference capture bounds");ops.push_back({n%480,n/480,color});}
uint32_t Background(unsigned i,unsigned bg){return 0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);}
void Fill(unsigned bg){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)bits[i]=Background(i,bg);}
uint32_t Blend(uint32_t d,uint32_t v){unsigned a=v>>24;return a==255?v:0xff000000u|((v&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}
}
extern "C" void pvzh1_combat_sprites_engine_probe(){
 using namespace Sexy;using namespace H1Combat;
 PlayerInfo p;p.mName="COMBAT_SPRITE_ORACLE";p.mLevel=8;auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 Check(H1CombatSpritesPrepare(),"required combat banks not prepared");
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();bits=target->GetBits();h1DirtyCanvas.enabled=false;
 H1RenderPlanTestEnable(false);H1TextReplayTestEnable(false);
 Image*images[]={IMAGE_PROJECTILEPEA,IMAGE_PEA_SHADOWS,IMAGE_PLANTSHADOW,IMAGE_PLANTSHADOW2};unsigned flat=0,hits=0;
 for(auto*image:images)for(int row=0;row<image->mNumRows;++row)for(int col=0;col<image->mNumCols;++col)for(auto scale:Scales){
  Rect src(col*image->GetCelWidth(),row*image->GetCelHeight(),image->GetCelWidth(),image->GetCelHeight());SexyMatrix3 ref;ref.LoadIdentity();ref.m00=480.f/1060.f*scale.x;ref.m11=272.f/600.f*scale.y;ref.m02=120;ref.m12=136;
  H1CombatSpritesTestEnable(false);ops.clear();pvzh1_host_offline_pixel_capture=Capture;pvzh1_text_capture_active=1;H1DrawTransformed(target,image,src,Rect(0,0,480,272),Color::White,0,ref,true);pvzh1_text_capture_active=0;pvzh1_host_offline_pixel_capture=nullptr;
  for(auto position:{std::pair<float,float>{120,136},{120.25f,136.75f},{-4.75f,18.25f},{475.25f,267.75f}})for(bool center:{false,true})for(auto clip:{Rect(0,0,480,272),Rect(117,134,12,9),Rect(1,1,478,270)})for(unsigned bg=0;bg<2;++bg){
   Fill(bg);std::vector<uint32_t> expected(bits,bits+480*272);int dx=int(std::lround(position.first))-120,dy=int(std::lround(position.second))-136;
   for(auto&o:ops){int x=o.x+dx,y=o.y+dy;if(x>=0&&x<480&&y>=0&&y<272&&x>=clip.mX&&x<clip.mX+clip.mWidth&&y>=clip.mY&&y<clip.mY+clip.mHeight)expected[y*480+x]=Blend(expected[y*480+x],o.color);}
   SexyMatrix3 m=ref;m.m02=position.first+(center?src.mWidth*.5f*m.m00:0);m.m12=position.second+(center?src.mHeight*.5f*m.m11:0);
   H1CombatSpritesTestEnable(true);auto before=pvzh1_combat_sprite_diagnostic[1];H1DrawTransformed(target,image,src,clip,Color::White,0,m,true,0,0,center);
   Check(pvzh1_combat_sprite_diagnostic[1]==before+1,"flat sprite did not bypass triangles");
   if(!std::equal(expected.begin(),expected.end(),bits)){fprintf(stderr,"FLAT_DIFF source=%s scale=%f,%f pos=%f,%f center=%d clip=%d,%d,%d,%d\n",image->mFilePath.c_str(),scale.x,scale.y,position.first,position.second,center,clip.mX,clip.mY,clip.mWidth,clip.mHeight);throw std::runtime_error("Direct sprite differs from original-shader LCD bitmap/crop");}++flat;
  }
 }
 for(unsigned variant=0;variant<Variants;++variant){SRand(80319+variant);H1CombatSpritesTestEnable(true);auto*s=gLawnApp->AddPvzpParticle(120*1060.f/480,136*600.f/272,0,PARTICLE_PEA_SPLAT);H1CombatSpritesTestVariant(s,variant);
  for(unsigned age=0;age<=HitLastAge;++age){for(bool add:{false,true}){
   for(auto*n=s->mEmitterList.mHead;n;n=n->mNext)s->mParticleHolder->mEmitters.DataArrayGet(unsigned(n->mValue))->mExtraAdditiveDrawOverride=add;
   for(unsigned bg=0;bg<2;++bg){Fill(bg);H1CombatSpritesTestEnable(false);Graphics g(screen);if(!s->mDead)s->Draw(&g);std::vector<uint32_t> expected(bits,bits+480*272);
    Fill(bg);H1CombatSpritesTestEnable(true);Check(H1CombatHitDraw(s,&g),"pea splat was not replayed");
    if(!std::equal(expected.begin(),expected.end(),bits)){fprintf(stderr,"HIT_DIFF variant=%u age=%u add=%u bg=%u\n",variant,age,unsigned(add),bg);throw std::runtime_error("Pea splat pixels or playback age differ");}++hits;
   }
  }if(!s->mDead)s->Update();}
  Check(s->mDead,"baked hit outlived original effect");s->ParticleSystemDie();
 }
 h1DirtyCanvas.enabled=true;gLawnApp->ShowGameSelector();Check(!pvzh1_combat_sprite_diagnostic[0],"return menu retained combat banks");gLawnApp->KillGameSelector();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"combat-sprites-engine-verification.json");out<<"{\"passed\":true,\"direct_bitmap_full_frame_oracles\":"<<flat<<",\"particle_full_frame_oracles\":"<<hits<<",\"raster_translation_snaps_only_display_coordinates\":true,\"clips_crop_without_resampling\":true,\"particle_variants\":4,\"normal_and_extra_additive\":true,\"menu_combat_bytes\":0}\n";
}
