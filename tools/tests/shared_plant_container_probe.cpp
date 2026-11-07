/* Exercise the new descriptor, component streams and checked packet payloads. */
#include "LawnApp.h"
#include "Common.h"
#include "actors/ActorPose.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_plant_pose_diagnostic[12];
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" void pvzh1_shared_plant_container_probe(){
 const auto resources=Sexy::GetResourceFolder();unsigned rejected=0;
 for(unsigned fault=0;fault<14;++fault){
  Sexy::SetResourceFolder((std::filesystem::current_path()/"build/shared-plant-fault-fixtures-v1"/std::to_string(fault)).string());
  H1ActorPoseTestEnable(true);const auto before=pvzh1_plant_pose_diagnostic[1];H1PlantPosePrepare(REANIM_PEASHOOTER);
  const bool okay=fault==0?pvzh1_actor_pose_diagnostic[0]>0:!pvzh1_actor_pose_diagnostic[0]&&pvzh1_plant_pose_diagnostic[1]==before+1;
  if(!okay){std::fprintf(stderr,"H1PS fixture %u was handled incorrectly\n",fault);throw std::runtime_error("H1PS component guard failed");}
  H1PlantPosePrepare(REANIM_PEASHOOTER);if(pvzh1_plant_pose_diagnostic[1]!=before+bool(fault))throw std::runtime_error("H1PS fault retried while drawing");
  pvzh1_actor_pose_shutdown();if(pvzh1_actor_pose_diagnostic[0])throw std::runtime_error("H1PS fault cleanup leaked");rejected+=bool(fault);
 }
 Sexy::SetResourceFolder(resources);std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"shared-plant-container-verification.json");
 out<<"{\"passed\":true,\"valid_descriptor_banks\":1,\"malformed_component_cases\":"<<rejected<<",\"failed_load_latched\":true,\"shutdown_bytes\":0}\n";
}
