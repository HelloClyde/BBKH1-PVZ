/* Compare the production plant packet reader with the original renderer.
 * LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/Graphics.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include "graphics/MemoryImage.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <zlib.h>
extern "C" volatile uint32_t pvzh1_plant_pose_diagnostic[12];
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" volatile uint32_t pvzh1_actor_page_diagnostic[8];
extern void H1ActorPosePageTestSlots(unsigned);
extern void H1ActorPoseLegacyTestEnable(bool);
extern "C" size_t pvzh1_actor_pose_release_pressure(size_t);
namespace {
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
std::pair<const char*,std::vector<Sexy::Image*>> Damage(ReanimationType type){
 using namespace Sexy;
 switch(type){
  case REANIM_WALLNUT:return {"anim_face",{nullptr,IMAGE_REANIM_WALLNUT_CRACKED1,IMAGE_REANIM_WALLNUT_CRACKED2}};
  case REANIM_TALLNUT:return {"anim_idle",{nullptr,IMAGE_REANIM_TALLNUT_CRACKED1,IMAGE_REANIM_TALLNUT_CRACKED2}};
  case REANIM_GARLIC:return {"anim_face",{nullptr,IMAGE_REANIM_GARLIC_BODY2,IMAGE_REANIM_GARLIC_BODY3}};
  case REANIM_PUMPKIN:return {"Pumpkin_front",{nullptr,IMAGE_REANIM_PUMPKIN_DAMAGE1,IMAGE_REANIM_PUMPKIN_DAMAGE3}};
  default:return {nullptr,{nullptr}};
 }
}
}
extern "C" void pvzh1_plant_pose_engine_probe(){
 PlayerInfo profile;profile.mName="PLANT_READER_TEST";profile.mId=1;profile.mLevel=6;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;H1ActorPoseTestEnable(false);gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);H1ActorPoseTestEnable(true);auto* board=gLawnApp->mBoard;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;
 Sexy::H1RenderPlanTestEnable(false);Sexy::H1TextReplayTestEnable(false);H1ActorPosePageTestSlots(2);
 std::set<Reanimation*> seen;for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)seen.insert(r);
 std::set<int> types;unsigned comparisons=0,replays=0,visibility=0;
 for(int seed=0;seed<int(NUM_SEED_TYPES);++seed){
  H1ActorPoseTestEnable(true);Plant::PreloadPlantResources(SeedType(seed));board->AddPlant(2,1,SeedType(seed));
  for(auto* child:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)if(!child->mDead&&!seen.contains(child)&&H1ActorPose::PlantType(child->mReanimationType)){
   child->mOverlayMatrix.m02=120*1060.f/480;child->mOverlayMatrix.m12=136*600.f/272;H1PlantPosePrepare(child->mReanimationType,true);
  }
  for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
   if(r->mDead||seen.contains(r))continue;seen.insert(r);if(!H1ActorPose::PlantType(r->mReanimationType))continue;
   if(!types.insert(int(r->mReanimationType)).second)continue;H1PlantPosePrepare(r->mReanimationType,true);
   for(int t=0;t<r->mDefinition->mTracks.count;++t)for(int f=0;f<r->mDefinition->mTracks.tracks[t].mTransforms.count;++f){auto* image=r->mDefinition->mTracks.tracks[t].mTransforms.mTransforms[f].mImage;if(uintptr_t(image)>1000)if(auto* memory=dynamic_cast<Sexy::MemoryImage*>(image))memory->GetBits();}
   for(auto* image:Damage(r->mReanimationType).second)if(auto* memory=dynamic_cast<Sexy::MemoryImage*>(image))memory->GetBits();
   std::vector<AttachmentID> attachments;for(int i=0;i<r->mDefinition->mTracks.count;++i){attachments.push_back(r->mTrackInstances[i].mAttachmentID);r->mTrackInstances[i].mAttachmentID=ATTACHMENTID_NULL;}
   r->mOverlayMatrix.m02=120*1060.f/480;r->mOverlayMatrix.m12=136*600.f/272;
   const auto damage=Damage(r->mReanimationType);const int frames=r->mDefinition->mTracks.tracks[0].mTransforms.count;
   for(auto* image:damage.second){if(damage.first)r->SetImageOverride(damage.first,image);
    for(auto filter:{FILTER_EFFECT_NONE,FILTER_EFFECT_WASHED_OUT,FILTER_EFFECT_LESS_WASHED_OUT,FILTER_EFFECT_WHITE}){
     r->mFilterEffect=filter;
     for(int i=0;i<r->mDefinition->mTracks.count;++i){r->mTrackInstances[i].mBlendCounter=0;r->mTrackInstances[i].mShakeX=r->mTrackInstances[i].mShakeY=0;}
     for(unsigned sample=0;sample<7;++sample){const unsigned half=sample*(frames*2-2)/6;
      r->mFrameStart=half/2;r->mFrameCount=half/2+1==unsigned(frames)?1:2;r->mAnimTime=(half&1)*.5f;
      for(unsigned selection=0;selection<2;++selection){
       for(int i=0;i<r->mDefinition->mTracks.count;++i)r->mTrackInstances[i].mRenderGroup=selection?(i%3==0?1:0):0;
       for(unsigned bg=0;bg<3;++bg){std::vector<uint32_t> expected;
        for(bool replay:{false,true}){H1ActorPoseTestEnable(replay);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();
         for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);
         H1ActorPoseLegacyTestEnable(false);if(replay&&sample==3&&selection==1&&bg==0)pvzh1_actor_pose_release_pressure(232632);
         Sexy::Graphics g(screen);const auto before=pvzh1_actor_pose_diagnostic[5];r->DrawRenderGroup(&g,0);
         if(!replay)expected.assign(pixels,pixels+480*272);
         else {if(expected!=std::vector<uint32_t>(pixels,pixels+480*272)){
           unsigned at=0;while(at<480*272&&expected[at]==pixels[at])++at;
           std::fprintf(stderr,"PLANT READER MISMATCH seed=%d type=%d filter=%d half=%u selection=%u pixel=%u expected=%08x actual=%08x replay=%u\n",seed,int(r->mReanimationType),int(filter),half,selection,at,expected[at],pixels[at],pvzh1_actor_pose_diagnostic[5]-before);
           ReanimatorFrameTime time;r->GetFrameTime(&time);uint64_t key=0;H1ActorPose::Signature(r,0,key,false,false,true);std::fprintf(stderr,"  key=%llx time=%d/%d/%f groups:",(unsigned long long)key,time.mAnimFrameBeforeInt,time.mAnimFrameAfterInt,time.mFraction);
           for(int i=0;i<r->mDefinition->mTracks.count;++i)std::fprintf(stderr," %d:%d/%d",i,r->mTrackInstances[i].mRenderGroup,int(r->mTrackInstances[i].mAttachmentID));std::fprintf(stderr,"\n");
           auto folder=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));std::ofstream(folder/"expected.bin",std::ios::binary).write((char*)expected.data(),480*272*4);std::ofstream(folder/"actual.bin",std::ios::binary).write((char*)pixels,480*272*4);
           Check(false,"Plant packet differs from original drawing");}
          Check(pvzh1_actor_pose_diagnostic[5]>before,"Plant packet path was bypassed");replays+=pvzh1_actor_pose_diagnostic[5]-before;++comparisons;
         }
        }
       }if(selection)++visibility;
      }
     }
    }
   }
   for(int i=0;i<r->mDefinition->mTracks.count;++i)r->mTrackInstances[i].mAttachmentID=attachments[i];
  }
 }
 Check(types.size()==50,"Plant type coverage incomplete");Check(!pvzh1_plant_pose_diagnostic[1],"Plant preload rejected a bank");Check(!pvzh1_actor_page_diagnostic[4],"Plant decoder error");
 const unsigned loaded=pvzh1_plant_pose_diagnostic[0],generic=pvzh1_plant_pose_diagnostic[5];
 bool keep[NUM_REANIMS]{},filters[NUM_REANIMS]{};H1PlantPoseDiscardUnneeded(keep,filters);pvzh1_actor_pose_shutdown();Check(!pvzh1_actor_pose_diagnostic[0],"Plant cleanup leaked");
 // A compact real bank exercises the new outer container/slice guards.
 const std::string resources=Sexy::GetResourceFolder();std::ifstream input(Sexy::GetResourcePath("plant-poses.h1pp"),std::ios::binary);
 std::vector<char> original((std::istreambuf_iterator<char>(input)),{});
 auto word=[](const std::vector<char>& b,unsigned at){uint32_t n;std::memcpy(&n,b.data()+at,4);return n;};
 auto put=[](std::vector<char>& b,unsigned at,uint32_t n){std::memcpy(b.data()+at,&n,4);};
 const unsigned tableBytes=word(original,12),index=unsigned(REANIM_WALLNUT)*4*12,start=word(original,32+index),size=word(original,36+index);
 std::vector<char> compact(original.begin(),original.begin()+32+tableBytes);std::fill(compact.begin()+32,compact.end(),0);put(compact,32+index,32+tableBytes);put(compact,36+index,size);put(compact,40+index,word(original,40+index));compact.insert(compact.end(),original.begin()+start,original.begin()+start+size);
 put(compact,16,compact.size());put(compact,20,crc32(0,(Bytef*)compact.data()+32,tableBytes));
 const auto temp=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"bad-plant-container";std::filesystem::create_directory(temp);Sexy::SetResourceFolder(temp.string());H1ActorPoseTestEnable(true);unsigned rejected=0;
 auto attempt=[&](bool valid){const auto before=pvzh1_plant_pose_diagnostic[1];H1PlantPosePrepare(REANIM_WALLNUT);Check(valid?pvzh1_actor_pose_diagnostic[0]>0:!pvzh1_actor_pose_diagnostic[0]&&pvzh1_plant_pose_diagnostic[1]==before+1,"Plant outer container guard failed");H1PlantPosePrepare(REANIM_WALLNUT);Check(pvzh1_plant_pose_diagnostic[1]==before+!valid,"Plant missing bank retried every draw");pvzh1_actor_pose_shutdown();if(!valid)++rejected;};
 std::filesystem::remove(temp/"plant-poses.h1pp");attempt(false);
 for(unsigned fault=0;fault<6;++fault){auto bytes=compact;
  if(fault==1)bytes.resize(16);
  if(fault==2)bytes[20]^=1;
  if(fault==3){put(bytes,32+index,0xffffffffu);put(bytes,20,crc32(0,(Bytef*)bytes.data()+32,tableBytes));}
  // The physical EOF contradicts the declared size. A correctly declared
  // shorter lazy stream need not inspect unused geometry during preload.
  if(fault==4)bytes.resize(bytes.size()-size/2);
  if(fault==5)bytes.back()^=1;
  // Resource checksums are intentionally offline-only. Changing only the
  // outer CRC or the zlib Adler trailer must not reject trusted game data.
  {std::ofstream out(temp/"plant-poses.h1pp",std::ios::binary);out.write(bytes.data(),bytes.size());}attempt(fault==0||fault==2||fault==5);
 }Sexy::SetResourceFolder(resources);
 h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream report(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"plant-reader-verification.json");
 report<<"{\"passed\":true,\"types\":"<<types.size()<<",\"banks_loaded\":"<<loaded<<",\"whole_frame_comparisons\":"<<comparisons<<",\"own_actor_pixels_only\":true,\"replays\":"<<replays<<",\"generic_replays\":"<<generic<<",\"visibility_states\":"<<visibility<<",\"decoded_slots_per_bank\":2,\"pressure_releases\":"<<pvzh1_plant_pose_diagnostic[10]<<",\"missing_or_malformed_containers\":"<<rejected<<",\"shutdown_bytes\":0}\n";
}
