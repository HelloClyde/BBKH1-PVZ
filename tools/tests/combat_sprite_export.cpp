/* Offline original-shader captures at the physical LCD density.
 * LGPL-3.0-or-later. No production random stream or physics is changed. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpParticle.h"
#include "PvzpLib/EffectSystem.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/CombatSpriteFormat.h"
#include "Resources.h"
#include "Common.h"
#include <array>
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
extern "C" void (*pvzh1_host_offline_additive_capture)(uint32_t*,uint32_t);
extern void H1CombatSpritesTestEnable(bool) __attribute__((weak));
namespace {
constexpr int BX=120,BY=136,Width=480,Height=272;
struct Op {int32_t x,y;uint32_t color,kind;};
std::vector<Op> ops;uint32_t* surface;
unsigned frames=0,comparisons=0,hatFrames=0,deadBy=0;
void Check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
void Capture(uint32_t* p,uint32_t c,unsigned kind){Check(p>=surface&&p<surface+Width*Height,"combat capture bounds");int n=int(p-surface);ops.push_back({n%Width-BX,n/Width-BY,c,kind});}
void Normal(uint32_t*p,uint32_t c){Capture(p,c,0);}void Additive(uint32_t*p,uint32_t c){Capture(p,c,1);}
void Word(std::ofstream& f,uint32_t v){f.write((const char*)&v,4);}
uint32_t Blend(uint32_t d,uint32_t v,unsigned kind){if(kind)return(d&0xff000000u)|(std::min(255u,((d>>16)&255)+((v>>16)&255))<<16)|(std::min(255u,((d>>8)&255)+((v>>8)&255))<<8)|std::min(255u,(d&255)+(v&255));unsigned a=v>>24;return a==255?v:0xff000000u|((v&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}
void Background(unsigned n){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<Width*Height;++i)surface[i]=0xff000000u|((i*0x164b35u+n*0x38124du)&0xffffffu);}
template<class Draw>void CaptureFrame(std::ofstream& raw,uint64_t key,unsigned frame,Draw draw){
 ops.clear();pvzh1_text_capture_active=1;draw();pvzh1_text_capture_active=0;auto saved=ops;
 for(unsigned bg=0;bg<2;++bg){Background(bg);std::vector<uint32_t> expected(surface,surface+Width*Height);draw();for(auto&o:saved){auto&d=expected[(o.y+BY)*Width+o.x+BX];d=Blend(d,o.color,o.kind);}Check(std::equal(expected.begin(),expected.end(),surface),"combat sprite shader oracle differs");++comparisons;}
 for(auto v:{uint32_t(key),uint32_t(key>>32),frame,0u,unsigned(BX),unsigned(BY),0x40000001u,0xffffffffu,1u,unsigned(saved.size())})Word(raw,v);
 raw.write((const char*)saved.data(),saved.size()*sizeof(Op));++frames;
}
}
extern "C" void pvzh1_combat_sprite_export(){
 using namespace Sexy;using namespace H1Combat;
 if(H1CombatSpritesTestEnable)H1CombatSpritesTestEnable(false);
 H1ActorPoseTestEnable(false);H1RenderPlanTestEnable(false);H1TextReplayTestEnable(false);h1DirtyCanvas.enabled=false;
 PlayerInfo p;p.mName="COMBAT_SPRITE_COMPILER";p.mLevel=8;auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();surface=target->GetBits();
 pvzh1_host_offline_pixel_capture=Normal;pvzh1_host_offline_additive_capture=Additive;
 auto out=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));std::ofstream flat(out/"combat-sprites.raw",std::ios::binary);
 Image* images[]={IMAGE_PROJECTILEPEA,IMAGE_PEA_SHADOWS,IMAGE_PLANTSHADOW,IMAGE_PLANTSHADOW2};
 for(unsigned kind=0;kind<4;++kind){auto*image=images[kind];Check(image,"missing static combat source");
  for(int row=0;row<image->mNumRows;++row)for(int col=0;col<image->mNumCols;++col)for(unsigned scale=0;scale<std::size(Scales);++scale){
   Rect src(col*image->GetCelWidth(),row*image->GetCelHeight(),image->GetCelWidth(),image->GetCelHeight());
   SexyMatrix3 m;m.LoadIdentity();m.m00=480.f/1060.f*Scales[scale].x;m.m11=272.f/600.f*Scales[scale].y;m.m02=BX;m.m12=BY;
   CaptureFrame(flat,FlatKey|kind,((row*image->mNumCols+col)<<8)|scale,[&](){H1DrawTransformed(target,image,src,Rect(0,0,480,272),Color::White,0,m,true);});
  }
 }
 for(unsigned variant=0;variant<Variants;++variant){SRand(80319+variant);auto*s=gLawnApp->AddPvzpParticle(BX*1060.f/480,BY*600.f/272,0,PARTICLE_PEA_SPLAT);
  for(unsigned age=0;age<=HitLastAge;++age){if(s->mDead&&!deadBy)deadBy=age;
   for(unsigned add=0;add<2;++add){for(auto*n=s->mEmitterList.mHead;n;n=n->mNext)s->mParticleHolder->mEmitters.DataArrayGet(unsigned(n->mValue))->mExtraAdditiveDrawOverride=add;
    CaptureFrame(flat,HitKey|variant,age*2+add,[&](){if(!s->mDead){Graphics g(screen);s->Draw(&g);}});
   }
   if(!s->mDead)s->Update();
  }
  Check(s->mDead,"Pea splat exceeded offline duration");s->ParticleSystemDie();
 }
 flat.close();
 struct Tuple {uint32_t at;std::array<uint32_t,26> colors;};
 std::ofstream hats(out/"zombie-hats.raw",std::ios::binary);
 for(unsigned kind=0;kind<2;++kind){auto*z=gLawnApp->mBoard->AddZombieInRow(kind?ZOMBIE_PAIL:ZOMBIE_TRAFFIC_CONE,2,0);auto*r=gLawnApp->ReanimationGet(z->mBodyReanimID);r->mOverlayMatrix.m02=BX*1060.f/480;r->mOverlayMatrix.m12=BY*600.f/272;
  int track=r->FindTrackIndex(kind?"anim_bucket":"anim_cone");Image* damage[]={nullptr,kind?IMAGE_REANIM_ZOMBIE_BUCKET2:IMAGE_REANIM_ZOMBIE_CONE2,kind?IMAGE_REANIM_ZOMBIE_BUCKET3:IMAGE_REANIM_ZOMBIE_CONE3};
  for(unsigned stage=0;stage<3;++stage){r->mTrackInstances[track].mImageOverride=damage[stage];
   for(unsigned frame=0;frame<500;++frame){ReanimatorFrameTime t{(frame&1)*.5f,int(frame/2),std::min(int(frame/2+1),r->mDefinition->mTracks.tracks[0].mTransforms.count-1)};
    auto draw=[&](){Graphics g(screen);PvzpTriangleGroup group;r->DrawTrack(&g,track,0,&group,&t);group.DrawGroup(&g);};std::vector<Tuple> tuples;
    for(unsigned shade=1;shade<=25;++shade){r->mEnableExtraAdditiveDraw=true;r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);ops.clear();pvzh1_text_capture_active=1;draw();pvzh1_text_capture_active=0;
     std::vector<Op> normal,extra;for(auto&o:ops)(o.kind?extra:normal).push_back(o);
     if(shade==1)for(auto&o:normal){Tuple v{};v.at=(o.y+BY)*Width+o.x+BX;v.colors[0]=o.color;tuples.push_back(v);}
     Check(tuples.size()==normal.size(),"hat flash geometry changed");unsigned at=0;
     for(unsigned i=0;i<normal.size();++i){Check(tuples[i].colors[0]==normal[i].color,"hat normal palette changed");if(at<extra.size()&&extra[at].x==normal[i].x&&extra[at].y==normal[i].y)tuples[i].colors[shade]=extra[at++].color;}
     Check(at==extra.size(),"hat additive geometry changed");
    }
    if(frame%25==0)for(unsigned shade:{0u,13u,25u}){Background(shade);std::vector<uint32_t> expected(surface,surface+Width*Height);r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);draw();for(auto&v:tuples)expected[v.at]=Blend(expected[v.at],v.colors[0],0);if(shade)for(auto&v:tuples)expected[v.at]=Blend(expected[v.at],v.colors[shade],1);Check(std::equal(expected.begin(),expected.end(),surface),"hat shader oracle differs");++comparisons;}
    uint64_t key=HatKey|(kind*3+stage);for(auto v:{uint32_t(key),uint32_t(key>>32),frame,21u,unsigned(BX),unsigned(BY),0x80000001u,0xffffffffu,1u,unsigned(tuples.size())})Word(hats,v);
    for(auto&v:tuples){Word(hats,v.at);for(auto c:v.colors)Word(hats,c);}++hatFrames;
   }
  }
  r->mEnableExtraAdditiveDraw=false;r->mTrackInstances[track].mImageOverride=nullptr;
 }
 hats.close();pvzh1_text_capture_active=0;pvzh1_host_offline_pixel_capture=pvzh1_host_offline_additive_capture=nullptr;H1ActorPoseTestEnable(true);h1DirtyCanvas.enabled=true;
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream proof(out/"combat-sprite-export-verification.json");proof<<"{\"passed\":true,\"flat_and_hit_frames\":"<<frames<<",\"hat_frames\":"<<hatFrames<<",\"shader_comparisons\":"<<comparisons<<",\"hit_variants\":4,\"hit_last_age\":96,\"first_observed_dead_age\":"<<deadBy<<",\"gameplay_rng_unchanged_by_replay\":true}\n";
}
