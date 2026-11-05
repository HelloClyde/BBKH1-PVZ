/* Public matrix-text drawing, including tint, clipping and changing backgrounds. */
#include "LawnApp.h"
#include "Resources.h"
#include "PvzpLib/PvzpCommon.h"
#include "graphics/Graphics.h"
#include "graphics/ImageFont.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/TextReplay.h"
#include "graphics/RenderPlanCache.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_text_replay_diagnostic[8];
namespace {void Check(bool v,const char*why){if(!v)throw std::runtime_error(why);}}
extern "C" void pvzh1_matrix_text_probe(){
 using namespace Sexy;auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;H1RenderPlanTestEnable(false);
 auto fill=[&](unsigned bg){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};unsigned compared=0,hits=0;
 for(auto* font:{FONT_CONTINUUMBOLD14,FONT_BRIANNETOD16})for(const char* text:{"100","阳光"})for(float scale:{.5f,1.f})for(unsigned tint=0;tint<2;++tint)for(unsigned clipped=0;clipped<2;++clipped){
  SexyTransform2D matrix;matrix.Scale(scale,scale);matrix.Translate(180.25f,180.5f);Color color=tint?Color(80,120,200,160):Color::Black;
  for(unsigned bg=0;bg<4;++bg){Graphics g(screen);g.mScaleX=g.mScaleY=scale;g.Translate(25,8);if(clipped)g.ClipRect(175,150,26,40);
   H1TextReplayTestEnable(false);fill(bg);auto before=target->mBitsChangedCount;PvzpDrawStringMatrix(&g,font,matrix,text,color);auto bumps=target->mBitsChangedCount-before;std::vector<uint32_t> expected(pixels,pixels+480*272);Graphics originalState(g);
   H1TextReplayTestEnable(true);fill(bg);before=target->mBitsChangedCount;auto prior=pvzh1_text_replay_diagnostic[0];PvzpDrawStringMatrix(&g,font,matrix,text,color);if(pvzh1_text_replay_diagnostic[0]>prior)++hits;
   Check(expected==std::vector<uint32_t>(pixels,pixels+480*272),"matrix text changed pixel blending");Check(target->mBitsChangedCount-before==bumps,"matrix text changed target mutation count");Check(g.mColor==originalState.mColor&&g.mColorizeImages==originalState.mColorizeImages&&g.mScaleX==originalState.mScaleX&&g.mScaleY==originalState.mScaleY&&g.mLinearBlend==originalState.mLinearBlend,"matrix text changed Graphics state");++compared;
  }
 }
 Check(hits>=32,"stable matrix strings never replayed");h1DirtyCanvas.enabled=true;H1RenderPlanTestEnable(true);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"matrix-text-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<compared<<",\"replayed_cases\":"<<hits<<",\"Graphics_state_and_mutation_counts_match\":true,\"backgrounds\":4,\"clip_and_scale_variants\":true}\n";
}
