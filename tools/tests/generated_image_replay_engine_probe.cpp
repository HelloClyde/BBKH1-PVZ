/* Exact replay of already generated small ARGB pictures; LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "graphics/MemoryImage.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/GLInterface.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/TextReplay.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
using namespace Sexy;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
namespace {unsigned exported=0;void CountExport(uint32_t*,uint32_t){++exported;}void Check(bool v,const char*s){if(!v)throw std::runtime_error(s);}}
extern "C" void pvzh1_generated_image_probe(){
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();auto*bits=target->GetBits();h1DirtyCanvas.enabled=false;
 unsigned cases=0,hits=0,mutations=0;const auto initial=pvzh1_text_replay_diagnostic[6];
 {
 MemoryImage image(gLawnApp);image.Create(120,120);image.SetImageMode(true,true);
 auto paint=[&](unsigned phase){auto*p=image.GetBits();for(unsigned i=0;i<120*120;++i)p[i]=((i%17?i%256:0)<<24)|((i*0x17895bu+phase*0x315793u)&0xffffffu);image.BitsChanged();};paint(0);
 auto fill=[&](unsigned bg){h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};
 for(float scale:{.45f,.73f})for(bool linear:{false,true})for(bool tinted:{false,true})for(bool cropped:{false,true})for(bool rotated:{false,true}){
  SexyTransform2D matrix;matrix.Scale(scale,scale);if(rotated)matrix.RotateDeg(17);matrix.Translate(100.25f,65.5f);
  Rect clip=cropped?Rect(75,68,37,51):Rect(0,0,480,272);Color color=tinted?Color(85,190,210,137):Color::White;
  for(unsigned bg=0;bg<3;++bg){H1TextReplayTestEnable(false);fill(bg);unsigned before=target->mBitsChangedCount;H1DrawTransformed(target,&image,Rect(0,0,120,120),clip,color,0,matrix,linear);unsigned bumps=target->mBitsChangedCount-before;std::vector<uint32_t> expected(bits,bits+480*272);
   H1TextReplayTestEnable(true);for(unsigned repeat=0;repeat<3;++repeat){fill(bg);before=target->mBitsChangedCount;unsigned oldHits=pvzh1_text_replay_diagnostic[0];H1DrawTransformed(target,&image,Rect(0,0,120,120),clip,color,0,matrix,linear);
    Check(expected==std::vector<uint32_t>(bits,bits+480*272),"generated replay changed sampled/tinted/clipped pixels");Check(target->mBitsChangedCount-before==bumps,"generated replay changed mutation count");hits+=pvzh1_text_replay_diagnostic[0]-oldHits;++cases;
   }
  }
 }
 SexyTransform2D matrix;matrix.Scale(.45f,.45f);matrix.Translate(100,70);
 for(unsigned phase=1;phase<=3;++phase){paint(phase);H1TextReplayTestEnable(false);fill(phase);H1DrawTransformed(target,&image,Rect(0,0,120,120),Rect(0,0,480,272),Color::White,0,matrix,true);std::vector<uint32_t> expected(bits,bits+480*272);
  H1TextReplayTestEnable(true);for(unsigned repeat=0;repeat<4;++repeat){fill(phase);H1DrawTransformed(target,&image,Rect(0,0,120,120),Rect(0,0,480,272),Color::White,0,matrix,true);Check(expected==std::vector<uint32_t>(bits,bits+480*272),"generated mutation reused old pixels");++mutations;}
 }
 // A warmed replay must never bypass an external offline pixel observer.
 fill(7);auto observer=pvzh1_host_offline_pixel_capture;pvzh1_host_offline_pixel_capture=CountExport;pvzh1_text_capture_active=1;
 unsigned previousHits=pvzh1_text_replay_diagnostic[0];H1DrawTransformed(target,&image,Rect(0,0,120,120),Rect(0,0,480,272),Color::White,0,matrix,true);
 pvzh1_text_capture_active=0;pvzh1_host_offline_pixel_capture=observer;
 Check(exported>0&&pvzh1_text_replay_diagnostic[0]==previousHits,"warmed image bypassed external offline capture");
 Check(hits>=128,"stable generated images did not replay");Check(pvzh1_text_replay_diagnostic[6]>initial,"generated image recorded no entries");
 }
 Check(pvzh1_text_replay_diagnostic[6]==initial,"source destruction retained generated entries");h1DirtyCanvas.enabled=true;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"generated-image-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<cases<<",\"warm_hits\":"<<hits<<",\"mutation_cases\":"<<mutations<<",\"source_destruction_releases_entries\":true,\"external_offline_capture_preserved\":true,\"linear_nearest_tint_rotation_clip\":true}\n";
}
