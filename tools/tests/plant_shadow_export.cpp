/* Bake the ordinary day/night shadow on actual lawn grid positions. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/PvzpCommon.h"
#include "Resources.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include <bit>
#include <vector>
#include <map>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
namespace {
struct Op {uint32_t at,value;bool operator==(const Op&)const=default;};
uint32_t* surface;std::vector<Op> operations;
void Capture(uint32_t* p,uint32_t v){operations.push_back({uint32_t(p-surface),v});}
void Check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void Word(std::ofstream& out,uint32_t n){out.write((char*)&n,4);}
}
extern "C" void pvzh1_plant_shadow_export(){
 using namespace Sexy;H1ActorPoseTestEnable(false);H1RenderPlanTestEnable(false);H1TextReplayTestEnable(false);h1DirtyCanvas.enabled=false;
 PlayerInfo profile;profile.mName="SHADOW_COMPILER";auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();surface=target->GetBits();
 std::map<std::pair<uint64_t,unsigned>,std::vector<Op>> poses;unsigned comparisons=0;
 auto fill=[&](unsigned bg){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)surface[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};
 for(int level:{1,11,21,31,41}){
  profile.mLevel=level;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
  for(int row=0;row<6;++row)for(int col=0;col<9;++col)for(unsigned night=0;night<2;++night){
   auto* image=night?IMAGE_PLANTSHADOW2:IMAGE_PLANTSHADOW;Check(image,"missing shadow source");
   const float tx=130.f+board->GridToPixelX(col,row),ty=board->GridToPixelY(col,row);
   const uint64_t key=(uint64_t(std::bit_cast<uint32_t>(tx))<<32)|std::bit_cast<uint32_t>(ty);
   operations.clear();pvzh1_host_offline_pixel_capture=Capture;pvzh1_text_capture_active=1;
   Graphics g(screen);g.Translate(tx,ty);PvzpDrawImageCelCenterScaledF(&g,image,-3,51,0,1,1);
   pvzh1_text_capture_active=0;pvzh1_host_offline_pixel_capture=nullptr;
   if(operations.empty())continue;
   auto [found,inserted]=poses.emplace(std::make_pair(key,night),operations);Check(inserted||found->second==operations,"shadow source changed between levels");
   if(!inserted)continue;
   for(unsigned bg=0;bg<3;++bg){
    fill(bg);PvzpDrawImageCelCenterScaledF(&g,image,-3,51,0,1,1);std::vector<uint32_t> expected(surface,surface+480*272);fill(bg);
    for(auto op:operations){auto& dest=surface[op.at];const uint32_t v=op.value,a=v>>24,p=dest;
     dest=a==255?v:0xff000000u|((v&0xffffffu)+((((p&0xff00ffu)*a)>>8)&0xff00ffu)+((((p&0xff00u)*a)>>8)&0xff00u));}
    Check(expected==std::vector<uint32_t>(surface,surface+480*272),"shadow capture differs from original whole frame");++comparisons;
   }
  }
 }
 const auto folder=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));std::ofstream raw(folder/"plant-shadows.raw",std::ios::binary);unsigned total=0;
 for(const auto& [id,ops]:poses){for(uint32_t v:{uint32_t(id.first),uint32_t(id.first>>32),id.second,0u,0u,0u,0x40000001u,0xffffffffu,1u,uint32_t(ops.size())})Word(raw,v);for(auto op:ops){Word(raw,op.at);Word(raw,op.value);}total+=ops.size();}
 std::ofstream proof(folder/"plant-shadow-export-verification.json");proof<<"{\"passed\":true,\"grid_shadow_poses\":"<<poses.size()<<",\"whole_frame_comparisons\":"<<comparisons<<",\"pixel_operations\":"<<total<<",\"stages\":5,\"day_and_night\":true}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;h1DirtyCanvas.enabled=true;
}
