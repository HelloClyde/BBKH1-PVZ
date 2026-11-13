/* Compare real attachment states and RNG with color memoization disabled. */
#define pvzh1_active_track_probe unused_color_active_probe
#include "active_track_engine_probe.cpp"
#undef pvzh1_active_track_probe
extern "C" volatile uint32_t pvzh1_color_memo_diagnostic[4];
extern "C" void pvzh1_attachment_color_probe(){
 H1ActorColorMemoTestEnable(false);const auto source=Run(true);
 H1ActorColorMemoTestEnable(true);const auto memoized=Run(true);
 Check(source==memoized,"memoized colors changed animation state or RNG");
 Check(pvzh1_color_memo_diagnostic[0]>100,"unchanged colors were never skipped");
 // Add a blink underneath a head while its ancestor remains highlighted.
 // New descendants must receive the unchanged ancestor color immediately.
 PlayerInfo profile;profile.mName="NEW_BLINK_COLOR";profile.mLevel=6;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);unsigned newChildren=0;
 for(auto seed:{SEED_PEASHOOTER,SEED_SUNFLOWER}){
  auto* plant=gLawnApp->mBoard->AddPlant(2,1,seed);auto* body=gLawnApp->ReanimationGet(plant->mBodyReanimID);
  body->mExtraAdditiveColor=Sexy::Color(255,255,255,196);body->mEnableExtraAdditiveDraw=true;
  for(unsigned trial=0;trial<5;++trial){body->PropogateColorToAttachments();body->PropogateColorToAttachments();plant->DoBlink();body->PropogateColorToAttachments();
   auto* blink=gLawnApp->ReanimationGet(plant->mBlinkReanimID);Check(blink->mEnableExtraAdditiveDraw&&blink->mExtraAdditiveColor==body->mExtraAdditiveColor,"new blink missed persistent parent color");++newChildren;plant->EndBlink();}
 }
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;pvzh1_actor_pose_shutdown();
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"attachment-color-verification.json");
 out<<"{\"passed\":true,\"state_and_rng_snapshots\":"<<memoized.size()<<",\"new_blinks_under_persistent_highlight\":"<<newChildren<<",\"unchanged_color_skips\":"<<pvzh1_color_memo_diagnostic[0]<<",\"shutdown_bytes\":0}\n";
}
