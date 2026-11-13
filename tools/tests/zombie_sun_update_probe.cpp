/* Whole animation/RNG equivalence for sparse zombie/sun track updates and
 * frame-time reuse; original blend, shake and attachment mutations run. */
#define pvzh1_active_track_probe unused_zombie_active_probe
#include "active_track_engine_probe.cpp"
#undef pvzh1_active_track_probe
#include "Lawn/Zombie.h"
#include "Lawn/Coin.h"
namespace {bool referenceFrameTime=false;}
extern "C" bool __real__Z16H1ActorFrameTimeP11ReanimationR19ReanimatorFrameTimeb(Reanimation*,ReanimatorFrameTime&,bool);
extern "C" bool __wrap__Z16H1ActorFrameTimeP11ReanimationR19ReanimatorFrameTimeb(Reanimation* r,ReanimatorFrameTime& t,bool store){return !referenceFrameTime&&__real__Z16H1ActorFrameTimeP11ReanimationR19ReanimatorFrameTimeb(r,t,store);}
namespace {
std::vector<uint64_t> Trial(bool optimized){
 H1ActorActiveTracksTestEnable(optimized);referenceFrameTime=!optimized;
 PlayerInfo profile;profile.mName="ZOMBIE_SUN_UPDATE";profile.mLevel=8;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 SRand(440199);gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;
 for(auto type:{ZOMBIE_NORMAL,ZOMBIE_TRAFFIC_CONE,ZOMBIE_PAIL,ZOMBIE_FLAG})Zombie::PreloadZombieResources(type);
 SRand(571231);std::vector<Reanimation*> bodies;
 for(auto type:{ZOMBIE_NORMAL,ZOMBIE_TRAFFIC_CONE,ZOMBIE_PAIL,ZOMBIE_FLAG}){auto* z=b->AddZombieInRow(type,2,0);bodies.push_back(gLawnApp->ReanimationGet(z->mBodyReanimID));}
 for(unsigned i=0;i<22;++i)b->AddCoin(200+i*3,200,COIN_SUN,COIN_MOTION_FROM_SKY);
 std::vector<Reanimation*> roots=bodies;
 // Zombie/Coin own their animation updates; these actors are deliberately
 // marked attachments so EffectSystem does not update them a second time.
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)if(r->mReanimationType==REANIM_SUN)roots.push_back(r);
 AttachmentID added=ATTACHMENTID_NULL;std::vector<uint64_t> states;SRand(403219);
 for(unsigned tick=0;tick<700;++tick){
  for(auto* r:bodies){
   if(tick==30||tick==210)r->PlayReanim("anim_eat",REANIM_LOOP,20,36);
   if(tick==90||tick==290)r->PlayReanim("anim_walk2",REANIM_LOOP,20,12);
   if(tick==130)r->SetShakeOverride("anim_head1",.25f);
   if(tick==160)r->SetShakeOverride("anim_head1",-0.f);
   r->mEnableExtraAdditiveDraw=tick%30<12;r->mExtraAdditiveColor=Sexy::Color(130,130,130,255);
  }
  if(tick==60){auto* r=bodies[0];const int track=r->FindTrackIndex("anim_head1");Check(track>=0,"zombie attachment fixture track");auto& slot=r->mTrackInstances[track].mAttachmentID;AttachReanim(slot,gLawnApp->AddReanimation(0,0,0,REANIM_SUN),0,0);added=slot;}
  if(tick==100){AttachmentDie(added);added=ATTACHMENTID_NULL;}
  for(auto* r:roots)if(!r->mDead){r->PropogateColorToAttachments();r->Update();}
  gLawnApp->mEffectSystem->ProcessDeleteQueue();
  // Observe the real clocks repeatedly, including a legal clock change after
  // the first query. The reference wrapper disables only the memo helper.
  for(auto* r:bodies){ReanimatorFrameTime a,c;r->GetFrameTime(&a);r->GetFrameTime(&c);Check(a.mFraction==c.mFraction&&a.mAnimFrameBeforeInt==c.mAnimFrameBeforeInt&&a.mAnimFrameAfterInt==c.mAnimFrameAfterInt,"repeated frame query differs");
   if(tick%71==0){r->mAnimTime=.3125f;r->GetFrameTime(&c);}
   uint64_t h=std::bit_cast<uint32_t>(c.mFraction);h=h*65537+unsigned(c.mAnimFrameBeforeInt);h=h*65537+unsigned(c.mAnimFrameAfterInt);states.push_back(h);
  }
  states.push_back(Snapshot());states.push_back(unsigned(Rand()));
 }
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;return states;
}
}
extern "C" void pvzh1_zombie_sun_update_probe(){
 const auto original=Trial(false),optimized=Trial(true);Check(original.size()==optimized.size(),"zombie/sun trace length differs");
 for(unsigned i=0;i<original.size();++i)if(original[i]!=optimized[i]){std::fprintf(stderr,"ZOMBIE SUN MISMATCH observation=%u source=%llx actual=%llx\n",i,(unsigned long long)original[i],(unsigned long long)optimized[i]);Check(false,"zombie/sun state or RNG differs");}
 Check(pvzh1_active_track_diagnostic[3]>10000,"inactive zombie/sun tracks were not omitted");referenceFrameTime=false;H1ActorActiveTracksTestEnable(true);pvzh1_actor_pose_shutdown();
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"zombie-sun-update-verification.json");out<<"{\"passed\":true,\"simulation_steps_per_path\":700,\"state_frame_time_rng_observations\":"<<optimized.size()<<",\"inactive_tracks_omitted\":"<<pvzh1_active_track_diagnostic[3]<<",\"blend_shake_new_and_retired_attachments_preserved\":true,\"shutdown_bytes\":0}\n";
}
