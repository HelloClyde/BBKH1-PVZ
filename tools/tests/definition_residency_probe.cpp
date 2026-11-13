/* Count real loaded definition storage and exact transform duplication. */
#include "triangle_source_inventory_probe.cpp"
#include <set>
#include <cstring>
extern "C" void pvzh1_definition_residency_probe(){
 pvzh1_triangle_source_inventory_probe();
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"definition-residency.json");
 out<<"{\"scope\":\"Loaded definitions for the fixed dense scene; MIPS32 byte model\",\"definitions\":[";bool first=true;
 uint64_t rawTotal=0,indexTotal=0;
 for(unsigned type=0;type<gReanimatorDefCount;++type){auto&d=gReanimatorDefArray[type];if(!d.mTracks.tracks)continue;
  uint64_t frames=0;std::set<std::string> transforms;
  for(int t=0;t<d.mTracks.count;++t){auto&a=d.mTracks.tracks[t].mTransforms;frames+=a.count;
   for(int f=0;f<a.count;++f){const auto&v=a.mTransforms[f];std::string key((const char*)&v,32);
    for(auto p:{uintptr_t(v.mImage),uintptr_t(v.mFont)})key.append((const char*)&p,sizeof p);
    key+=v.mText?v.mText:"";transforms.insert(std::move(key));}
  }
  uint64_t raw=frames*44,indexed=transforms.size()*44+frames*(transforms.size()<=65535?2:4)+d.mTracks.count*12;rawTotal+=raw;indexTotal+=indexed;
  if(!first)out<<',';first=false;out<<"{\"type\":"<<type<<",\"tracks\":"<<d.mTracks.count<<",\"frames\":"<<frames<<",\"unique_transform_values\":"<<transforms.size()<<",\"raw_bytes\":"<<raw<<",\"hypothetical_indexed_bytes\":"<<indexed<<"}";
 }
 out<<"],\"raw_transform_bytes\":"<<rawTotal<<",\"hypothetical_indexed_bytes\":"<<indexTotal<<"}\n";
}
