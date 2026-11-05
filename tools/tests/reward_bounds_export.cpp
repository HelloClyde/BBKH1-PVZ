#include "LawnApp.h"
#include "PvzpLib/PvzpCommon.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
extern "C" void pvzh1_reward_bounds_export(){
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"reward-bounds.json");out<<"[";
 for(unsigned frame=0;frame<181;++frame){int age=std::max(0,int((frame*10+1)/3)-3);
  float loX=PvzpAnimateCurveFloat(0,350,age,0.f,240.f,PvzpCurves::CURVE_EASE_OUT);
  float hiX=PvzpAnimateCurveFloat(0,350,age,480.f,240.f,PvzpCurves::CURVE_EASE_OUT);
  float loY=PvzpAnimateCurveFloat(0,350,age,0.f,200.f*272/600,PvzpCurves::CURVE_EASE_OUT);
  float hiY=PvzpAnimateCurveFloat(0,350,age,272.f,200.f*272/600,PvzpCurves::CURVE_EASE_OUT);
  if(frame)out<<",";out<<"["<<std::max(0,int(std::floor(loX))-2)<<","<<std::min(480,int(std::ceil(hiX))+2)<<","<<std::max(0,int(std::floor(loY))-2)<<","<<std::min(272,int(std::ceil(hiY))+2)<<"]";
 }out<<"]\n";
}
