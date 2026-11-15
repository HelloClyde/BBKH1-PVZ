/* Compare the indexed reader with the accepted v4 decoder and shader. */
#include "graphics/RewardEffects.h"
#define pvzh1_reward_effect_diagnostic pvzh1_legacy_reward_effect_diagnostic
#define H1RewardEffectsPrepare H1LegacyRewardEffectsPrepare
#define H1RewardEffectsBegin H1LegacyRewardEffectsBegin
#define H1RewardEffectsUpdate H1LegacyRewardEffectsUpdate
#define H1RewardEffectsForget H1LegacyRewardEffectsForget
#define H1RewardEffectsDraw H1LegacyRewardEffectsDraw
#define H1RewardStarburst H1LegacyRewardStarburst
#define pvzh1_reward_effect_shutdown pvzh1_legacy_reward_effect_shutdown
#define H1RewardEffectsTestEnable H1LegacyRewardEffectsTestEnable
#define H1RewardEffectsTestDraw H1LegacyRewardEffectsTestDraw
#define H1RewardEffectsTestLoad H1LegacyRewardEffectsTestLoad
#include "reward_effect_v4_oracle.inc"
#undef pvzh1_reward_effect_diagnostic
#undef H1RewardEffectsPrepare
#undef H1RewardEffectsBegin
#undef H1RewardEffectsUpdate
#undef H1RewardEffectsForget
#undef H1RewardEffectsDraw
#undef H1RewardStarburst
#undef pvzh1_reward_effect_shutdown
#undef H1RewardEffectsTestEnable
#undef H1RewardEffectsTestDraw
#undef H1RewardEffectsTestLoad
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/ReanimationLawn.h"
#include "Lawn/Widget/AwardScreen.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <iterator>
extern "C" volatile uint32_t pvzh1_reward_effect_diagnostic[12],pvzh1_actor_pose_diagnostic[16],pvzh1_early_lawn_diagnostic[8],pvzh1_award_retirement_diagnostic[6];
namespace {
void CheckStream(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
uint32_t At(const std::vector<uint8_t>& v,unsigned at){return v[at]|uint32_t(v[at+1])<<8|uint32_t(v[at+2])<<16|uint32_t(v[at+3])<<24;}
void Put(std::vector<uint8_t>&v,unsigned at,uint32_t x){for(unsigned i=0;i<4;++i)v[at+i]=x>>(i*8);}
}
extern "C" void pvzh1_reward_stream_probe(){
 using namespace Sexy;auto out=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));
 std::ifstream input(std::filesystem::path(getenv("PVZ_RESOURCE_DIR"))/"reward-effects.h1re",std::ios::binary);
 std::vector<uint8_t> indexed((std::istreambuf_iterator<char>(input)),{}),old=indexed;CheckStream(At(indexed,4)==5,"Not indexed reward asset");
 const auto table=At(indexed,28),bounds=At(indexed,60);Put(old,4,4);
 for(unsigned i=0;i<212;++i){auto encoded=At(indexed,table+i*16+4);Put(old,table+i*16,encoded>>16);Put(old,table+i*16+4,encoded&65535);}
 Put(old,40,crc32(0,old.data()+64,old.size()-64));uLongf packedBytes=compressBound(old.size());std::vector<uint8_t> packed(packedBytes+16);
 Put(packed,0,0x5a523148);Put(packed,4,1);Put(packed,8,old.size());Put(packed,12,crc32(0,old.data(),old.size()));CheckStream(compress2(packed.data()+16,&packedBytes,old.data(),old.size(),9)==Z_OK,"Oracle packing failed");
 auto legacy=out/"reward-v4.h1re";{std::ofstream f(legacy,std::ios::binary);f.write((char*)packed.data(),packedBytes+16);}
 CheckStream(H1LegacyRewardEffectsTestLoad(legacy.string().c_str()),"Legacy reader failed");H1RewardEffectsPrepare();
 const auto resident=pvzh1_reward_effect_diagnostic[0];CheckStream(resident>0&&resident<65536,"Reward whole bank still resident");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();unsigned comparisons=0;
 for(unsigned i=0;i<212;++i){unsigned word=At(indexed,table+i*16+4),kind=word>>16,age=word&65535;int box[4];for(unsigned j=0;j<4;++j)box[j]=int16_t(indexed[bounds+i*8+j*2]|unsigned(indexed[bounds+i*8+j*2+1])<<8);
  for(unsigned placement=0;placement<6;++placement){int x=placement==1||placement==4?box[0]:placement==2||placement==5?box[1]:(box[0]+box[1])/2;int y=placement==1||placement==5?box[2]:placement==2||placement==4?box[3]:(box[2]+box[3])/2;
   std::vector<uint32_t> expected;
   for(bool fresh:{false,true}){h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned n=0;n<480*272;++n)pixels[n]=0xff000000u|((n*0x164b35u+placement*0x38124du)&0xffffffu);Graphics g(screen);if(placement==3)g.SetClipRect(61,33,550,401);
    CheckStream(fresh?H1RewardEffectsTestDraw(kind,age,&g,x,y):H1LegacyRewardEffectsTestDraw(kind,age,&g,x,y),"Reward drawing rejected");
    if(!fresh)expected.assign(pixels,pixels+480*272);else CheckStream(std::equal(expected.begin(),expected.end(),pixels),"Indexed replay changed pixels");
   }++comparisons;
  }
 }
 pvzh1_legacy_reward_effect_shutdown();pvzh1_reward_effect_shutdown();
 gLawnApp->KillGameSelector();PlayerInfo profile;profile.mName="RewardStream";profile.mId=1;profile.mLevel=2;auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);gLawnApp->StartPlaying();auto* board=gLawnApp->mBoard;board->mBoardFadeOutCounter=600;
 const auto reads=pvzh1_reward_effect_diagnostic[11];auto* effect=gLawnApp->AddPvzpParticle(530,300,0,PARTICLE_SEED_PACKET_PICKUP);CheckStream(effect,"No real reward particle");
 {h1DirtyCanvas.Begin(target);Graphics g(screen);g.Translate(board->mX,board->mY);effect->Draw(&g);}CheckStream(pvzh1_reward_effect_diagnostic[11]==reads,"Pickup first frame performed IO");
 for(unsigned i=0;i<121;++i)effect->Update();{Graphics g(screen);g.Translate(board->mX,board->mY);effect->Draw(&g);}CheckStream(pvzh1_reward_effect_diagnostic[6]==121,"Reward clock changed");
 effect->ParticleSystemDie();profile.mLevel=3;gLawnApp->KillBoard();gLawnApp->ShowAwardScreen(AWARD_FORLEVEL,false);
 CheckStream(pvzh1_actor_pose_diagnostic[0]==0&&pvzh1_early_lawn_diagnostic[0]==0,"Combat storage remains in award page");CheckStream(pvzh1_reward_effect_diagnostic[0]==0,"Reward storage remains in award page");
 {h1DirtyCanvas.Begin(target);Graphics g(screen);g.Translate(gLawnApp->mAwardScreen->mX,gLawnApp->mAwardScreen->mY);gLawnApp->mAwardScreen->Draw(&g);}
 gLawnApp->KillAwardScreen();gLawnApp->mPlayerInfo=previous;
 std::ofstream f(out/"reward-stream-verification.json");f<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"frames\":212,\"resident_bytes\":"<<resident<<",\"pickup_first_frame_io\":0,\"particle_clock_preserved\":true,\"award_combat_and_lawn_bytes\":0,\"award_preview_drawn\":true}\n";
}
