/* Host-only attribution of the exact fixed dense-board input. */
#include "triangle_source_inventory_probe.cpp"
#include <chrono>
namespace {
struct ReplayCensus {uint64_t segments=0,empty=0,emptyPlain=0,visible=0,runs=0,sharedEmpty=0;};
std::map<int,ReplayCensus> census;
struct Timed {uint64_t calls=0,ns=0;std::vector<std::chrono::steady_clock::time_point> stack;};
std::map<std::pair<int,unsigned>,Timed> times;
}
extern "C" void pvzh1_host_replay_phase(int type,unsigned phase,bool enter){
 if(!enabled)return;
 auto& v=times[{type,phase}];
 if(enter)v.stack.push_back(std::chrono::steady_clock::now());
 else{Check(!v.stack.empty(),"Unbalanced replay phase");v.ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-v.stack.back()).count();v.stack.pop_back();++v.calls;}
}
extern "C" void pvzh1_host_replay_segment(int type,unsigned mode,unsigned runs,unsigned payload,bool attached,bool visible){
 if(!enabled)return;
 auto& v=census[type];++v.segments;v.empty+=runs==0;v.emptyPlain+=runs==0&&!attached;v.visible+=visible;v.runs+=runs;v.sharedEmpty+=runs==0&&bool(payload&0x80000000u);
}
extern "C" void pvzh1_replay_census_probe(){
 pvzh1_triangle_source_inventory_probe();
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"replay-census.json");
 out<<"{\"scope\":\"Host structural and time attribution; not a physical H1 timing\",\"segments\":[";bool first=true;
 for(auto&[type,v]:census){if(!first)out<<',';first=false;out<<"{\"type\":"<<type<<",\"count\":"<<v.segments<<",\"empty\":"<<v.empty<<",\"empty_no_attachment\":"<<v.emptyPlain<<",\"visible\":"<<v.visible<<",\"runs\":"<<v.runs<<",\"shared_empty\":"<<v.sharedEmpty<<"}";}
 out<<"],\"phases\":[";first=true;
 for(auto&[key,v]:times){Check(v.stack.empty(),"Unfinished replay phase");if(!first)out<<',';first=false;out<<"{\"type\":"<<key.first<<",\"phase\":"<<key.second<<",\"calls\":"<<v.calls<<",\"ns\":"<<v.ns<<"}";}
 out<<"]}\n";
}
