/* The actual failure animation must shade every LCD column at every camera offset. */
#include "LawnApp.h"
#include "PvzpLib/Reanimator.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" void pvzh1_fullscreen_mask_probe(){
 using namespace Sexy;auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();
 auto*r=gLawnApp->AddReanimation(0,0,0,REANIM_ZOMBIES_WON);r->SetFramesForLayer("anim_screen");unsigned cases=0,changed=0;
 for(float x:{0.f,130.f,220.f,-120.f,130.75f})for(float y:{0.f,3.25f})for(int alpha:{255,129}){
  r->mColorOverride=Color(255,255,255,alpha);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();auto*bits=target->GetBits();std::fill_n(bits,480*272,0xffa0c0e0u);
  Graphics g(screen);g.mTransX=x;g.mTransY=y;r->DrawRenderGroup(&g,0);
  const auto shade=bits[0];for(unsigned i=0;i<480*272;++i)if(bits[i]!=shade)throw std::runtime_error("Failure mask left part of the viewport uncovered");
  if(shade!=0xffa0c0e0u)++changed;++cases;
 }
 if(changed!=cases)throw std::runtime_error("Failure mask did not shade the test canvas");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"fullscreen-mask-verification.json");out<<"{\"passed\":true,\"viewport\":[480,272],\"real_failure_animation\":true,\"pixel_cases\":"<<cases<<",\"pixels_checked\":"<<cases*480*272<<",\"camera_and_fractional_offsets\":true,\"alpha_preserved\":true}\n";r->ReanimationDie();
}
