/* Compare bounded unscaled CJK draws with the original full-atlas pixels. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/Cutscene.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "Lawn/Widget/TitleScreen.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/PausedBoardCache.h"
#include "graphics/GLInterface.h"
#include "graphics/SoftwareRaster.h"
#include "Resources.h"
#include "widget/WidgetManager.h"
#include "h1_resources.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <zlib.h>
extern "C" volatile uint32_t pvzh1_font_storage_diagnostic[8];
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" size_t pvzh1_cache_memory_pressure(size_t);
namespace {void Check(bool v){if(!v)throw std::runtime_error("Bounded CJK glyph regression");}}
extern "C" void pvzh1_reduced_font_glyph_probe(){
 using namespace Sexy;
 unsigned comparisons=0;
 for(const char* name:{"h1zh09","h1zh12","h1zh17","briannetod12","briannetod16","continuumbold14","houseofterror20"}){
  std::string path=std::string("data/")+name;
  std::unique_ptr<GLImage> source(gLawnApp->GetImage(path,false));Check(bool(source));
  H1Image input{};Check(h1_image_open(&input,path.c_str(),true,0xffffff));
  MemoryImage oracle(gLawnApp);oracle.Create(input.info.width,input.info.height);
  Check(h1_image_load_all(&input,oracle.GetBits(),size_t(input.info.width)*input.info.height));
  oracle.SetImageMode(true,true);h1_image_close(&input);
  for(bool sparse:{true,false}){
   H1ImageCacheFontTilesTestEnable(sparse);
   for(int mode:{Graphics::DRAWMODE_NORMAL,Graphics::DRAWMODE_ADDITIVE})
    for(int alpha:{255,129})for(int y:{0,530,1060}){
     if(y>=source->mHeight)continue;
     Rect crop(47,y,37,std::min(40,source->mHeight-y));Color color(90,190,255,alpha);
     MemoryImage a(gLawnApp),b(gLawnApp);a.Create(100,70);b.Create(100,70);
     a.SetImageMode(false,false);b.SetImageMode(false,false);
     for(int i=0;i<100*70;++i)a.GetBits()[i]=b.GetBits()[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
     a.Blt(&oracle,7,11,crop,color,mode,false);b.Blt(source.get(),7,11,crop,color,mode,false);
     Check(std::equal(a.GetBits(),a.GetBits()+7000,b.GetBits()));
     Check(!source->mBits&&!source->mColorIndices&&!source->mColorTable);++comparisons;
    }
  }
 }
 unsigned cropComparisons=0;
 {
  std::unique_ptr<GLImage> source(gLawnApp->GetImage("data/h1zh15o",false));Check(bool(source));
  for(int mode:{Graphics::DRAWMODE_NORMAL,Graphics::DRAWMODE_ADDITIVE})for(int alpha:{255,129})for(int row:{0,530,1060}){
   std::vector<uint32_t> expected;
   for(bool crop:{false,true}){
    H1ImageCacheFontTilesTestEnable(false);H1ImageCacheAlphaPagesTestEnable(false);H1ImageCacheFontTilesTestEnable(crop);
    MemoryImage target(gLawnApp);target.Create(480,272);target.SetImageMode(false,false);auto* bits=target.GetBits();
    for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
    SexyTransform2D matrix;matrix.Scale(.4528302f,.453333333f);matrix.Translate(13.5f,11.25f-float(row)*.453333333f);
    H1DrawTransformed(&target,source.get(),Rect(47,row,37,40),Rect(5,7,410,250),Color(90,190,255,alpha),mode,matrix,true);
    if(!crop)expected.assign(bits,bits+480*272);else{
     Check(expected==std::vector<uint32_t>(bits,bits+480*272));
     H1ImageCacheStorageStats stats{};Check(H1ImageCacheGetStorageStats(source.get(),&stats)&&stats.bytes<163840&&!source->mBits);
    }
   }++cropComparisons;
  }
 }

 H1ImageCacheAlphaPagesTestEnable(true);
 H1ImageCacheFontTilesTestEnable(true);
 Check(!pvzh1_font_storage_diagnostic[2]&&pvzh1_font_storage_diagnostic[0]==comparisons);
 PlayerInfo profile;profile.mName="FONT_OPTIONS";profile.mLevel=4;profile.mId=1;
 auto* saved=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 if(gLawnApp->mTitleScreen){gLawnApp->mWidgetManager->RemoveWidget(gLawnApp->mTitleScreen.get());gLawnApp->mTitleScreen.reset();}
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 const auto savePath=GetSavedGameName(GAMEMODE_ADVENTURE,profile.mId);Check(!std::filesystem::exists(savePath));
 H1ImageCacheStorageStats ui{};Check(H1ImageCacheGetStorageStats(dynamic_cast<MemoryImage*>(IMAGE_OPTIONS_MENUBACK),&ui)&&ui.bytes&&ui.storageWidth==190&&ui.storageHeight==224);
 const auto reservedBytes=ui.bytes;
 pvzh1_cache_complete_draw_batch();
 for(int i=0;i<512&&pvzh1_cache_memory_pressure(4096);++i){}
 Check(H1ImageCacheGetStorageStats(dynamic_cast<MemoryImage*>(IMAGE_OPTIONS_MENUBACK),&ui)&&ui.bytes==reservedBytes&&!ui.argbLoads);
 gLawnApp->mGameScene=SCENE_PLAYING;gLawnApp->mBoard->mCutScene->mSeedChoosing=false;
 gLawnApp->mBoard->mCutScene->mUpsellHideBoard=false;
 gLawnApp->mBoard->mX=H1BoardOffset;gLawnApp->mBoard->mY=0;
 gLawnApp->mWidgetManager->MarkAllDirty();gLawnApp->DrawDirtyStuff();
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* canvas=H1ScreenTarget(screen);
 std::vector<uint32_t> lawn(canvas->GetBits(),canvas->GetBits()+480*272);
 gLawnApp->DoNewOptions(false);
 Check(!std::filesystem::exists(savePath));
 Check(pvzh1_paused_board_diagnostic[0]==480*272*4);
 std::fill_n(canvas->GetBits(),480*272,0xff123456);
 Graphics boardGraphics(screen);boardGraphics.mTransX=H1BoardOffset;
 Check(H1PausedBoardReplay(gLawnApp->mBoard,&boardGraphics));
 Check(std::equal(lawn.begin(),lawn.end(),canvas->GetBits()));
 for(int i=0;i<3;++i){gLawnApp->mWidgetManager->MarkAllDirty();gLawnApp->DrawDirtyStuff();}
 Check(pvzh1_paused_board_diagnostic[1]>=4&&pvzh1_paused_board_diagnostic[0]==480*272*4);
 Check(!pvzh1_font_storage_diagnostic[2]);
 gLawnApp->DoConfirmBackToMain();gLawnApp->ButtonDepress(2000+DIALOG_CONFIRM_BACK_TO_MAIN);
 Check(!gLawnApp->mBoard&&std::filesystem::exists(savePath));
 std::ifstream file(savePath,std::ios::binary);std::vector<unsigned char> savedBytes((std::istreambuf_iterator<char>(file)),{});
 auto u32=[&](unsigned at){return uint32_t(savedBytes[at])|uint32_t(savedBytes[at+1])<<8|uint32_t(savedBytes[at+2])<<16|uint32_t(savedBytes[at+3])<<24;};
 Check(savedBytes.size()>24&&!std::memcmp(savedBytes.data(),"PVZP_SAVE4",10)&&u32(12)==1&&u32(16)==savedBytes.size()-24&&u32(20)==crc32(0,savedBytes.data()+24,savedBytes.size()-24));
 bool blocked=false;
 {std::unique_ptr<GLImage> font(gLawnApp->GetImage("data/h1zh15o",false));
  try{font->GetBits();}catch(const std::logic_error&){blocked=true;}
  H1ImageCacheStorageStats stats{};Check(blocked&&!font->mBits&&H1ImageCacheGetStorageStats(font.get(),&stats)&&!stats.argbLoads);
 }
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"reduced-font-verification.json");
 out<<"{\"passed\":true,\"pixel_comparisons\":"<<comparisons<<",\"triangle_tile_comparisons\":"<<cropComparisons<<",\"CJK_native_path\":\"offline glyph tiles only\",\"renderer_full_atlas_requests\":0,\"forbidden_14MB_RGBA_request_rejected\":true,\"largest_glyph_buffer\":"<<pvzh1_font_storage_diagnostic[5]<<",\"level4_options_drawn\":true,\"UI_survives_pressure\":true,\"primed_paused_canvas_pixel_exact\":true,\"opening_options_has_no_save_IO\":true,\"return_to_menu_writes_valid_original_V4\":true}\n";
 gLawnApp->KillDialog(DIALOG_NEWOPTIONS);gLawnApp->KillSeedChooserScreen();gLawnApp->KillBoard();gLawnApp->mPlayerInfo=saved;
}
