/* Universal glyph operations must match the original on changing backgrounds. */
#include "LawnApp.h"
#include "Resources.h"
#include "graphics/ImageFont.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/TextReplay.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>
using namespace Sexy;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
extern "C" void pvzh1_text_replay_probe(){
 auto* device=gLawnApp->mGLInterface.get();auto saved=std::move(device->mScreenImage);
 device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,H1LogicalWidth,H1LogicalHeight,H1ScreenWidth,H1ScreenHeight);
 auto* screen=H1GetPhysicalScreen(device->GetScreenImage());auto* target=screen->Target();
 ImageFont* fonts[]={dynamic_cast<ImageFont*>(FONT_HOUSEOFTERROR28),dynamic_cast<ImageFont*>(FONT_HOUSEOFTERROR16),dynamic_cast<ImageFont*>(FONT_DWARVENTODCRAFT18GREENINSET)};
 for(auto* font:fonts){Check(font!=nullptr,"missing real font");H1TextReplayForget(font);}
 unsigned cases=0;const int alphas[]={0,1,8,9,128,192,239,255};
 for(unsigned frame=0;frame<240;++frame){
  auto* font=fonts[(frame/8)%3];const Color color(253,245,173,alphas[frame%8]);
  const std::string text=(frame%2)?"Click on the falling sun to collect it!":"Sun 50, AAVV 0123456789";
  std::vector<uint32_t> expected;uint32_t targetDelta=0,logicalDelta=0;
  for(bool fast:{false,true}){
   H1TextReplayTestEnable(fast);auto* pixels=target->GetBits();
   for(unsigned i=0;i<H1ScreenWidth*H1ScreenHeight;++i)pixels[i]=((i%5?255u:0u)<<24)|((i*0x13759bu+frame*0x71933u)&0xffffffu);target->BitsChanged();
   Graphics g(screen);g.mTransX=(frame/24)%2?.375f:0.f;g.mTransY=(frame/48)%2?-.25f:0.f;
   if(frame%3==0)g.SetClipRect(41,71,521,389);
   const Color old=g.GetColor();const bool oldColorize=g.GetColorizeImages();
   const uint32_t beforeTarget=target->mBitsChangedCount,beforeLogical=screen->mBitsChangedCount;
   font->DrawStringEx(&g,63,333,text,color,nullptr,nullptr);
   const int measured=font->StringWidth(text);
   H1TextReplayTestEnable(false);const int originalWidth=font->StringWidth(text);H1TextReplayTestEnable(fast);
   Check(measured==originalWidth,"cached font metrics changed string width");
   Check(g.GetColor()==old&&g.GetColorizeImages()==oldColorize&&g.GetDrawMode()==0,"font Graphics state changed");
   pixels=target->GetBits();std::vector<uint32_t> actual(pixels,pixels+H1ScreenWidth*H1ScreenHeight);
   if(!fast){expected=std::move(actual);targetDelta=target->mBitsChangedCount-beforeTarget;logicalDelta=screen->mBitsChangedCount-beforeLogical;}
   else {
    if(actual!=expected){fprintf(stderr,"TEXT mismatch frame=%u alpha=%d hits=%u stores=%u\n",frame,alphas[frame%8],pvzh1_text_replay_diagnostic[0],pvzh1_text_replay_diagnostic[2]);throw std::runtime_error("glyph replay differs on changed background");}
    Check(target->mBitsChangedCount-beforeTarget==targetDelta&&screen->mBitsChangedCount-beforeLogical==logicalDelta,"font mutation notification count changed");++cases;
   }
  }
 }
 Check(pvzh1_text_replay_diagnostic[0]>30,"no warm text replay occurred");
 const unsigned hits=pvzh1_text_replay_diagnostic[0],stores=pvzh1_text_replay_diagnostic[2];
 // Font generation invalidation and an optional partial-allocation failure.
 for(auto* font:fonts)H1TextReplayForget(font);
 Check(pvzh1_text_replay_diagnostic[4]==0&&pvzh1_text_replay_diagnostic[6]==0,"font release leaked captured pages");
 auto* font=fonts[0];Graphics g(screen);H1TextReplayTestEnable(true);H1TextReplayTestFailure(0);
 const unsigned failures=pvzh1_text_replay_diagnostic[7];
 for(unsigned i=0;i<3;++i)font->DrawStringEx(&g,71,321,"optional cache failure",Color::White,nullptr,nullptr);
 Check(pvzh1_text_replay_diagnostic[7]==failures+1,"optional allocation failure retried every frame");
 Check(pvzh1_text_replay_diagnostic[4]==0,"failed text capture leaked pages");
 H1TextReplayTestFailure(-1);
 const unsigned rejected=pvzh1_text_replay_diagnostic[3];
 {H1TextReplayCapture capture(font,&g,71,321,"overflow",Color::White);
  for(unsigned i=0;i<8193;++i)pvzh1_text_capture_pixel(target->mBits.get()+(i%1000),0xff37ab93u);
  capture.Commit();}
 Check(pvzh1_text_replay_diagnostic[3]==rejected+1&&pvzh1_text_replay_diagnostic[4]==0,"overflow retained a partial text recording");
 for(auto* item:fonts)H1TextReplayForget(item);
 device->mScreenImage=std::move(saved);H1TextReplayTestEnable(true);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"text-replay-verification.json");
 out<<"{\"passed\":true,\"whole_frame_cases\":"<<cases<<",\"warm_hits\":"<<hits<<",\"captures\":"<<stores<<",\"changing_background\":true,\"alpha_thresholds\":true,\"fractional_translation\":true,\"clip\":true,\"mutation_counts\":true,\"allocation_failure_latched\":true,\"overflow_rejected\":true,\"released_bytes\":0}\n";
}
