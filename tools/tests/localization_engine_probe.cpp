/* Real engine glyph coverage, migrated saved labels and localized almanac UI. */
#include "LawnApp.h"
#include "Lawn/MessageWidget.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/Widget/AlmanacDialog.h"
#include "Resources.h"
#include "graphics/ImageFont.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "localization/LegacyMessageMap.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdlib>
#include <cstring>
using namespace Sexy;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void PPM(const std::filesystem::path& name,H1PhysicalScreen* screen){
 std::ofstream f(name,std::ios::binary);f<<"P6\n480 272\n255\n";
 auto* bits=screen->Target()->GetBits();for(unsigned i=0;i<480*272;++i){unsigned char rgb[]={(unsigned char)(bits[i]>>16),(unsigned char)(bits[i]>>8),(unsigned char)bits[i]};f.write((char*)rgb,3);}
}
extern "C" void pvzh1_localization_probe(){
 const std::filesystem::path output=std::getenv("PVZ_HOST_OUTPUT");
 std::ifstream in(std::getenv("PVZ_ZH_CHARACTERS"),std::ios::binary);std::string glyphs((std::istreambuf_iterator<char>(in)),{});
 Require(!glyphs.empty(),"no glyph set");unsigned checks=0,faces=0;
 uint64_t digest=14695981039346656037ull;
 auto hash=[&](int value){for(unsigned i=0;i<4;++i){digest^=(uint32_t(value)>>(i*8))&255;digest*=1099511628211ull;}};
 const char* names[]={"briannetod12","briannetod16","briannetod32","briannetod32black","continuumbold14","continuumbold14outback","dwarventodcraft12","dwarventodcraft15","dwarventodcraft18","dwarventodcraft18brightgreeninset","dwarventodcraft18greeninset","dwarventodcraft18yellow","dwarventodcraft24","dwarventodcraft36brightgreeninset","dwarventodcraft36greeninset","houseofterror16","houseofterror20","houseofterror28","pico129","pix118bold"};
 for(auto* name:names){
  ImageFont font(gLawnApp,std::string("data/")+name+".txt");Require(font.mFontData->mInitialized,"font descriptor failed");
  size_t offset=0;char32_t c=0;
  while(UTF8DecodeNext(glyphs,offset,c)){
   Require(font.CharWidth(c)>0,"CJK glyph has zero advance");bool found=false;
   for(auto& layer:font.mFontData->mFontLayerList){auto& d=*layer.GetCharData(c);if(d.mImageRect.mWidth>0&&d.mImageRect.mHeight>0){found=true;hash(c);hash(d.mWidth);hash(d.mOffset.mX);hash(d.mOffset.mY);hash(d.mImageRect.mX);hash(d.mImageRect.mY);hash(d.mImageRect.mWidth);hash(d.mImageRect.mHeight);}}
   Require(found,"CJK glyph was not parsed into a real rectangle");++checks;
  }hash(font.GetHeight());hash(font.GetAscent());++faces;
 }
 unsigned migrated=0;
 for(auto style:{MessageStyle::MESSAGE_STYLE_HINT_LONG,MessageStyle::MESSAGE_STYLE_HUGE_WAVE}){
  MessageWidget message(gLawnApp);const char* key=style==MessageStyle::MESSAGE_STYLE_HUGE_WAVE?"ADVICE_HUGE_WAVE":"ADVICE_CLICK_ON_SUN";
  const H1LegacyMessageEntry* entry=nullptr;for(const auto& e:H1LegacyMessages)if(!std::strcmp(e.key,key))entry=&e;
  Require(entry!=nullptr,"missing migration key");message.SetLabel(entry->english,style);
  int duration=message.mDuration,display=message.mDisplayTime,slide=message.mSlideOffTime;
  message.H1LocalizeRestoredText();Require(std::string_view(message.mLabel)==gLawnApp->GetString(key,entry->english),"saved label stayed English");
  Require(duration==message.mDuration&&display==message.mDisplayTime&&slide==message.mSlideOffTime,"migration changed message timing");
  if(style==MessageStyle::MESSAGE_STYLE_HUGE_WAVE){size_t offset=0;char32_t c=0;unsigned count=0;while(UTF8DecodeNext(message.mLabel,offset,c))++count;Require(count==(unsigned)message.mTextReanimCount,"animated text retained English byte indices");}
  message.H1LocalizeRestoredText();Require(duration==message.mDuration,"migration is not idempotent");++migrated;
 }
 auto* device=gLawnApp->mGLInterface.get();auto saved=std::move(device->mScreenImage);
 device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,H1LogicalWidth,H1LogicalHeight,H1ScreenWidth,H1ScreenHeight);
 auto* screen=H1GetPhysicalScreen(device->GetScreenImage());
 PlayerInfo player{};player.mLevel=50;auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&player;
 auto* dialog=gLawnApp->DoAlmanacDialog(SeedType::SEED_PEASHOOTER);
 Graphics g(screen);g.SetColor(Color(38,69,39));g.FillRect(0,0,H1LogicalWidth,H1LogicalHeight);g.Translate(dialog->mX,dialog->mY);dialog->Draw(&g);
 PPM(output/"host-almanac-peashooter.ppm",screen);
 gLawnApp->KillDialog(Dialogs::DIALOG_ALMANAC);gLawnApp->mPlayerInfo=previous;device->mScreenImage=std::move(saved);
 std::ofstream report(output/"localization-engine-verification.json");report<<"{\"passed\":true,\"font_faces\":"<<faces<<",\"actual_engine_glyph_checks\":"<<checks<<",\"glyph_metrics_hash\":\""<<std::hex<<digest<<std::dec<<"\",\"saved_label_styles\":"<<migrated<<",\"animated_UTF8_indices_rebuilt\":true,\"message_timing_preserved\":true,\"almanac_scope\":\"host visual fixture using original dialog and localized resources\"}\n";
}
