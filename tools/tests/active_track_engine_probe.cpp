/* Original full track traversal versus the production active-track traversal. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Attachment.h"
#include "actors/ActorPose.h"
#include "Common.h"
#include <bit>
#include <vector>
#include <map>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern void H1ActorActiveTracksTestEnable(bool);
extern "C" volatile uint32_t pvzh1_active_track_diagnostic[4];
namespace {
std::vector<uint32_t>* debugWords=nullptr;std::ostream* debugLabels=nullptr;
void Check(bool b,const char* why){if(!b){std::fprintf(stderr,"ACTIVE TRACKS: %s\n",why);throw std::runtime_error(why);}}
uint64_t Snapshot(){
 uint64_t hash=14695981039346656037ull;auto word=[&](uint32_t v){if(debugWords)debugWords->push_back(v);for(unsigned j=0;j<4;++j){hash^=(v>>(j*8))&255;hash*=1099511628211ull;}};auto number=[&](float v){word(std::bit_cast<uint32_t>(v));};
 std::map<unsigned,unsigned> ids;unsigned ordinal=0;
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)ids[unsigned(gLawnApp->ReanimationGetID(r))]=++ordinal;
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
  if(debugLabels)*debugLabels<<"actor type="<<int(r->mReanimationType)<<" word="<<debugWords->size()<<"\n";
  word(r->mReanimationType);word(r->mDead);word(r->mIsAttachment);number(r->mAnimTime);number(r->mLastFrameTime);number(r->mAnimRate);word(r->mLoopCount);word(r->mLoopType);word(r->mFrameStart);word(r->mFrameCount);word(r->mFrameBasePose);
  for(unsigned i=0;i<9;++i)number(r->mOverlayMatrix.m[i/3][i%3]);word(r->mColorOverride.ToInt());word(r->mExtraAdditiveColor.ToInt());word(r->mExtraOverlayColor.ToInt());word(r->mEnableExtraAdditiveDraw);word(r->mEnableExtraOverlayDraw);
  word(r->mDefinition->mTracks.count);
  for(int i=0;i<r->mDefinition->mTracks.count;++i){if(debugLabels)*debugLabels<<" track="<<i<<" word="<<debugWords->size()<<"\n";const auto& t=r->mTrackInstances[i];word(t.mBlendCounter);word(t.mBlendTime);number(t.mShakeOverride);number(t.mShakeX);number(t.mShakeY);word(t.mRenderGroup);word(t.mTrackColor.ToInt());word(t.mIgnoreClipRect);word(t.mIgnoreColorOverride);word(t.mIgnoreExtraAdditiveColor);word(t.mTruncateDisappearingFrames);
   const auto& b=t.mBlendTransform;for(float v:{b.mTransX,b.mTransY,b.mSkewX,b.mSkewY,b.mScaleX,b.mScaleY,b.mAlpha,b.mFrame})number(v);
   auto* attachment=gLawnApp->mEffectSystem->mAttachmentHolder->mAttachments.DataArrayTryToGet(unsigned(t.mAttachmentID));word(attachment?attachment->mNumEffects:0);
   if(attachment)for(int e=0;e<attachment->mNumEffects;++e){const auto& effect=attachment->mEffectArray[e];word(effect.mEffectType);word(effect.mDontDrawIfParentHidden);word(effect.mDontPropogateColor);if(effect.mEffectType==EFFECT_REANIM)word(ids[effect.mEffectID]);for(unsigned k=0;k<9;++k)number(effect.mOffset.m[k/3][k%3]);}
  }
 }return hash;
}
void Dump(const char* tag,bool active){std::vector<uint32_t> values;const auto base=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/(std::string(active?"optimized-":"original-")+tag);std::ofstream labels(base.string()+".txt");debugWords=&values;debugLabels=&labels;Snapshot();debugWords=nullptr;debugLabels=nullptr;std::ofstream data(base.string()+".bin",std::ios::binary);data.write(reinterpret_cast<const char*>(values.data()),values.size()*4);}
std::vector<uint64_t> Run(bool active){
 H1ActorActiveTracksTestEnable(active);PlayerInfo profile;profile.mName="ACTIVE_TRACK_ORACLE";profile.mId=1;profile.mLevel=6;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
 // Finish cold definition loads before fixing the inputs to the update trial.
 for(int seed=0;seed<int(NUM_SEED_TYPES);++seed)Plant::PreloadPlantResources(SeedType(seed));
 std::vector<Plant*> plants;for(int seed=0;seed<int(NUM_SEED_TYPES);++seed){SRand(140326+seed);plants.push_back(board->AddPlant(4,2,SeedType(seed)));}
 SRand(80319);
 gLawnApp->AddReanimation(100,100,0,REANIM_SUN);std::vector<uint64_t> states;
 Dump("initial",active);
 for(unsigned tick=0;tick<150;++tick){
  for(unsigned i=0;i<plants.size();++i)if(auto* r=gLawnApp->ReanimationTryToGet(plants[i]->mBodyReanimID)){
   if(tick==10||tick==110)r->StartBlend(tick==10?7:12);
   if(tick==30&&i<10)r->SetShakeOverride(r->mDefinition->mTracks.tracks[0].mName,.25f);
   if(tick==60&&i<10)r->SetShakeOverride(r->mDefinition->mTracks.tracks[0].mName,-0.f);
   if(tick==50&&(plants[i]->mSeedType==SEED_PEASHOOTER||plants[i]->mSeedType==SEED_SUNFLOWER||plants[i]->mSeedType==SEED_WALLNUT))plants[i]->DoBlink();
   if(tick==90)plants[i]->EndBlink();
   if(tick==70)r->mColorOverride=Sexy::Color(172,91,203,137);
   if(tick==80)r->mColorOverride=Sexy::Color::White;
   r->mEnableExtraAdditiveDraw=tick>=40&&tick<50;r->mExtraAdditiveColor=Sexy::Color(93,93,93,196);
   r->mEnableExtraOverlayDraw=tick>=100&&tick<109;r->mExtraOverlayColor=Sexy::Color(62,191,223,113);
   r->PropogateColorToAttachments();r->Update();
  }
  gLawnApp->mEffectSystem->ProcessDeleteQueue();if(tick==0)Dump("tick0",active);states.push_back(Snapshot());states.push_back(unsigned(Rand()));
 }
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;return states;
}
}
extern "C" void pvzh1_active_track_probe(){
 const auto original=Run(false),optimized=Run(true);Check(original.size()==optimized.size(),"trace length differs");
 for(unsigned i=0;i<original.size();++i)if(original[i]!=optimized[i]){std::fprintf(stderr,"ACTIVE TRACK MISMATCH tick=%u field=%u original=%llx actual=%llx\n",i/2,i%2,(unsigned long long)original[i],(unsigned long long)optimized[i]);Check(false,"live animation state or random stream differs");}
 Check(pvzh1_active_track_diagnostic[3]>10000,"inactive tracks were not omitted");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"active-tracks-verification.json");out<<"{\"passed\":true,\"all_seed_types\":"<<int(NUM_SEED_TYPES)<<",\"simulation_steps_per_path\":150,\"animation_state_and_random_snapshots\":"<<optimized.size()<<",\"original_clock_blend_shake_blink_color_and_attachment_updates_preserved\":true,\"inactive_track_visits_omitted\":"<<pvzh1_active_track_diagnostic[3]<<"}\n";
}
