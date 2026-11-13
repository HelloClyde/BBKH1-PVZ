/* Compare specialized H1D2 loading with the original map decoder. */
#include "PvzpLib/Reanimator.h"
#include <cstring>
#include <stdexcept>
#include <filesystem>
#include <fstream>
extern void H1DefinitionFastTransformTestEnable(bool);
namespace {void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
extern "C" void pvzh1_numeric_transform_decode_probe(){
 unsigned definitions=0,tracks=0,frames=0,strings=0;
 for(unsigned type=0;type<gReanimationParamArraySize;++type){
  const auto* path=gReanimationParamArray[type].mReanimFileName;
  ReanimatorDefinition original{},fast{};
  H1DefinitionFastTransformTestEnable(false);
  Check(ReanimationLoadDefinition(path,&original),"Original map load failed");
  H1DefinitionFastTransformTestEnable(true);
  Check(ReanimationLoadDefinition(path,&fast),"Bulk transform load failed");
  Check(original.mFPS==fast.mFPS&&original.mTracks.count==fast.mTracks.count,"Definition header changed");
  for(int t=0;t<original.mTracks.count;++t){
   const auto&a=original.mTracks.tracks[t];const auto&b=fast.mTracks.tracks[t];
   Check(!std::strcmp(a.mName,b.mName)&&a.mTransforms.count==b.mTransforms.count,"Track header changed");++tracks;
   for(int f=0;f<a.mTransforms.count;++f){
    const auto&x=a.mTransforms.mTransforms[f];const auto&y=b.mTransforms.mTransforms[f];
    Check(!std::memcmp(&x.mTransX,&y.mTransX,32),"Float bits changed");
    Check(x.mImage==y.mImage&&x.mFont==y.mFont&&!std::strcmp(x.mText,y.mText),"Resource or string changed");
    strings+=*x.mText!=0;++frames;
   }
  }
  ReanimationFreeDefinition(&original);ReanimationFreeDefinition(&fast);++definitions;
 }
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"numeric-transform-decode-verification.json");
 out<<"{\"passed\":true,\"definitions\":"<<definitions<<",\"tracks\":"<<tracks<<",\"frames_compared\":"<<frames<<",\"nonempty_text_frames\":"<<strings<<",\"float_bits_and_resource_handles_identical\":true,\"both_loaders_freed\":true}\n";
}
