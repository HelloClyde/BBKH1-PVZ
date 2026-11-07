/* Test-only linker observation of the real software raster calls. */
#include "dense_render_probe.cpp"
#include "graphics/SWTri.h"
#include "graphics/MemoryImage.h"
#include <map>
#include <iomanip>
namespace {
struct DrawStatistics {std::map<std::string,unsigned> calls;bool enabled=false;
 ~DrawStatistics(){std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"draw-source-observation.json");out<<'{';bool first=true;for(const auto& [name,count]:calls){if(!first)out<<',';first=false;out<<std::quoted(name)<<':'<<count;}out<<"}\n";}
} statistics;
}
extern "C" void pvzh1_dense_draw_observe_begin(){statistics.calls.clear();statistics.enabled=true;}
using namespace Sexy;
#define SW_ARGS SWHelper::XYZStruct* vertices,int count,MemoryImage* image,const Color& color,int mode,const Rect& clip,void* surface,int pitch,int format,bool blend,bool vertexColor
void RealShape(SW_ARGS) asm("__real__ZN4Sexy8SWHelper11SWDrawShapeEPNS0_9XYZStructEiPNS_11MemoryImageERKNS_5ColorEiRKNS_5TRectIiEEPviibb");
void ObserveShape(SW_ARGS) asm("__wrap__ZN4Sexy8SWHelper11SWDrawShapeEPNS0_9XYZStructEiPNS_11MemoryImageERKNS_5ColorEiRKNS_5TRectIiEEPviibb");
void ObserveShape(SW_ARGS){
 if(statistics.enabled){std::string name=image?image->mFilePath:"<solid>";if(image&&name.empty())name="<generated "+std::to_string(image->mWidth)+"x"+std::to_string(image->mHeight)+">";++statistics.calls[name];}
 RealShape(vertices,count,image,color,mode,clip,surface,pitch,format,blend,vertexColor);
}
