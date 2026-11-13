/* Eager versus deferred observed matrices, clocks, effects and RNG. */
#define pvzh1_active_track_probe unused_active_track_probe
#include "active_track_engine_probe.cpp"
#undef pvzh1_active_track_probe
#include "Lawn/System/SaveGame.h"
extern "C" volatile uint32_t pvzh1_deferred_overlay_diagnostic[8];
namespace {
std::vector<uint64_t> QueryAndSave(bool deferred){
 H1DeferredOverlayTestEnable(deferred);H1ActorActiveTracksTestEnable(true);
 PlayerInfo profile;profile.mName="DEFERRED_QUERY_SAVE";profile.mId=1;profile.mLevel=6;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 SRand(80319);gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);Check(H1DeferredOverlayPrepare(),"query table allocation");
 auto* board=gLawnApp->mBoard;SRand(80319);auto* plant=board->AddPlant(2,1,SEED_PEASHOOTER);
 auto* body=gLawnApp->ReanimationGet(plant->mBodyReanimID);auto* head=gLawnApp->ReanimationGet(plant->mHeadReanimID);
 const int face=head->FindTrackIndex("anim_face");Check(face>=0,"query fixture face track");
 std::vector<uint64_t> values;SRand(80319);
 for(int tick=0;tick<80;++tick){
  if(tick==15)body->StartBlend(12);
  if(tick==20)plant->DoBlink();if(tick==50)plant->EndBlink();
  body->Update();
  // Mutate every frame-sampling input after the child matrix was produced.
  // Delayed observation must use the clock at Update, not these new inputs.
  if(tick==18||tick==37){
   body->mAnimTime=.1375f;body->mFrameStart=0;body->mFrameCount=3;
   body->mLoopType=tick==18?REANIM_LOOP_FULL_LAST_FRAME:REANIM_LOOP;
  }
  body->SetPosition(float(tick%7)-3,float(tick%5)-2);
  body->OverrideScale(tick&1?1.125f:1.f,tick&2?.875f:1.f);
  if(tick==32)body->StartBlend(6);
  if(tick%11==0)head->SetPosition(float(tick-25),float(100-tick));
  if(tick%13==0)head->OverrideScale(.875f,1.125f);
  gLawnApp->mEffectSystem->ProcessDeleteQueue();
  if(tick==32||tick%7==3){
   Sexy::SexyTransform2D matrix;head->GetTrackMatrix(face,matrix);
   for(unsigned i=0;i<9;++i)values.push_back(std::bit_cast<uint32_t>(matrix.m[i/3][i%3]));
   values.push_back(Snapshot());
  }
  values.push_back(unsigned(Rand()));
 }
 const auto filename=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/(deferred?"deferred.v4":"eager.v4");
 Check(!deferred||pvzh1_deferred_overlay_diagnostic[5]>0,"save fixture has no pending matrix");
 Check(LawnSaveGame(board,filename.string()),"portable matrix save failed");
 Check(!pvzh1_deferred_overlay_diagnostic[5],"portable save did not materialize matrices");
 values.push_back(Snapshot());
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;return values;
}
}
extern "C" void pvzh1_deferred_overlay_probe(){
 unsigned comparisons=0;
 for(unsigned batch:{1u,9u}){
  observationBatch=batch;
  H1DeferredOverlayTestEnable(false);const auto original=Run(true);
  H1DeferredOverlayTestEnable(true);const auto optimized=Run(true);
  Check(original.size()==optimized.size(),"deferred trace length differs");
  for(unsigned i=0;i<original.size();++i){
   if(original[i]!=optimized[i]){std::fprintf(stderr,"DEFERRED MISMATCH batch=%u observation=%u field=%u original=%llx actual=%llx\n",batch,i/2,i%2,(unsigned long long)original[i],(unsigned long long)optimized[i]);Check(false,"deferred matrix/state/random stream differs");}
   ++comparisons;
  }
 }
 const auto eagerQueries=QueryAndSave(false),deferredQueries=QueryAndSave(true);
 Check(eagerQueries==deferredQueries,"matrix queries/partial setters/portable save differ");
 comparisons+=eagerQueries.size();
 Check(pvzh1_deferred_overlay_diagnostic[2]>100,"unobserved matrices were not omitted");
 observationBatch=1;H1DeferredOverlayTestEnable(true);
 pvzh1_actor_pose_shutdown();
 Check(!pvzh1_deferred_overlay_diagnostic[5]&&!pvzh1_deferred_overlay_diagnostic[7],"pending matrices or table survived shutdown");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"deferred-overlay-verification.json");
 out<<"{\"passed\":true,\"all_seed_types\":"<<int(NUM_SEED_TYPES)<<",\"observed_state_and_RNG_comparisons\":"<<comparisons<<",\"observation_intervals\":[1,9],\"queued\":"<<pvzh1_deferred_overlay_diagnostic[0]<<",\"resolved\":"<<pvzh1_deferred_overlay_diagnostic[1]<<",\"unobserved_calculations_omitted\":"<<pvzh1_deferred_overlay_diagnostic[2]<<",\"shutdown_bytes\":0}\n";
}
