/* Bake original complete decorative roll at 40 Hz, preserving ordered alpha.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpParticle.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "Common.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
namespace{struct Op{uint32_t destination,literal;};std::vector<Op> ops;uint32_t* surface;
 void Capture(uint32_t* p,uint32_t literal){if(p<surface||p>=surface+480*272)throw std::runtime_error("roll capture out of range");ops.push_back({uint32_t(p-surface),literal});}
 void U32(std::ofstream& f,uint32_t n){f.write(reinterpret_cast<const char*>(&n),4);}}
extern "C" void pvzh1_sod_roll_export(){
 using namespace Sexy;H1ActorPoseTestEnable(false);PlayerInfo p;p.mName="SOD_COMPILER";p.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);SRand(20261105);
 auto* roll=gLawnApp->AddReanimation(0,0,0,REANIM_SODROLL);auto* dirt=gLawnApp->AddPvzpParticle(35,348,1,PARTICLE_SOD_ROLL);
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();surface=target->GetBits();h1DirtyCanvas.enabled=false;pvzh1_host_offline_pixel_capture=Capture;
 std::ofstream raw(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"sod-roll.raw",std::ios::binary);unsigned age=0,comparisons=0;uint64_t operations=0;unsigned last=0;
 for(unsigned frame=0;frame<=90;++frame){unsigned tick=(frame*5+1)/2;while(age<tick){if(!roll->mDead)roll->Update();if(!dirt->mDead)dirt->Update();++age;}
  auto draw=[&](){Graphics g(screen);g.Translate(130,0);roll->Draw(&g);dirt->Draw(&g);};
  h1DirtyCanvas.RequireClear();ops.clear();pvzh1_text_capture_active=1;draw();pvzh1_text_capture_active=0;
  for(unsigned bg=0;bg<3;++bg){for(unsigned i=0;i<480*272;++i)surface[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);std::vector<uint32_t> expected(surface,surface+480*272);draw();
   for(auto op:ops){auto& out=expected[op.destination];unsigned a=op.literal>>24;if(a==255)out=op.literal;else out=0xff000000u|((op.literal&0xffffffu)+((((out&0xff00ffu)*a)>>8)&0xff00ffu)+((((out&0xff00u)*a)>>8)&0xff00u));}
   if(expected!=std::vector<uint32_t>(surface,surface+480*272))throw std::runtime_error("roll alpha capture differs from original effect");++comparisons;
  }
  constexpr uint64_t key=0x534f44524f4c4c31ull;for(uint32_t value:{uint32_t(key),uint32_t(key>>32),frame,2u,0u,0u,1u,0xffffffffu,0u,uint32_t(ops.size())})U32(raw,value);raw.write(reinterpret_cast<const char*>(ops.data()),ops.size()*sizeof(Op));operations+=ops.size();last=ops.size();
 }
 raw.close();pvzh1_host_offline_pixel_capture=nullptr;H1ActorPoseTestEnable(true);h1DirtyCanvas.enabled=true;
 if(last)throw std::runtime_error("roll trail has not finished in baked duration");
 std::ofstream verified(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"sod-roll-export-verification.json");verified<<"{\"passed\":true,\"sampled_frames\":91,\"whole_frame_comparisons\":"<<comparisons<<",\"ordered_pixel_operations\":"<<operations<<",\"simulation_hz\":100,\"render_samples_hz\":40,\"duration_ms\":2250,\"final_frame_operations\":"<<last<<",\"decorative_seed\":20261105}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
