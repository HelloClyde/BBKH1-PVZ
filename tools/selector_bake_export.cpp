/* Offline-only original-engine raster export. Never linked into the H1 app. */
#include "LawnApp.h"
#include "graphics/ScreenLayout.h"
#include "Lawn/Widget/GameSelector.h"
#include "Lawn/Widget/GameButton.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/Graphics.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/RenderPlanCache.h"
#include "Resources.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Sexy;
namespace {
struct Pixel {uint32_t destination,premultiplied,inverse,opaque;};
static_assert(sizeof(Pixel)==16);
uint32_t *target=nullptr;
std::vector<Pixel> pixels;
bool capturing=false;
unsigned checks=0;
uint64_t Hash(const uint32_t *p,size_t n){uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;++i)for(unsigned s=0;s<32;s+=8){h^=(p[i]>>s)&255;h*=1099511628211ull;}return h;}
void Check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void Background(unsigned seed){for(unsigned i=0;i<H1ScreenWidth*H1ScreenHeight;++i)target[i]=0xff000000u|((i*0x164b35u+seed*0x213141u)&0xffffffu);}
void Replay(){for(const auto& p:pixels){uint32_t &d=target[p.destination];if(p.opaque)d=p.premultiplied;else{uint32_t rb=(((d&0xff00ffu)*p.inverse)>>8)&0xff00ffu,g=(((d&0xff00u)*p.inverse)>>8)&0xff00u;d=0xff000000u|(p.premultiplied+(rb|g));}}}
std::filesystem::path directory;
std::ofstream manifest;
bool first=true;
void Export(const std::string& name,const std::function<void()>& draw,const std::string& extra=""){
 pixels.clear();Background(1);capturing=true;draw();capturing=false;
 for(unsigned seed:{1u,17u,231u}){Background(seed);draw();std::vector<uint32_t> expected(target,target+H1ScreenWidth*H1ScreenHeight);Background(seed);Replay();Check(std::memcmp(expected.data(),target,expected.size()*4)==0,"literal fragment replay differs from original engine");++checks;}
 auto path=directory/(name+".ops");std::ofstream out(path,std::ios::binary);out.write((const char*)pixels.data(),pixels.size()*sizeof(Pixel));Check(bool(out),"write failed");
 if(!first)manifest<<",\n";first=false;manifest<<"{\"key\":"<<std::quoted(name)<<",\"file\":"<<std::quoted(path.filename().string())<<",\"fragments\":"<<pixels.size()<<",\"last_hash\":"<<Hash(target,H1ScreenWidth*H1ScreenHeight)<<extra<<"}";manifest.flush();
 std::fprintf(stderr,"MENU BAKE %s %zu fragments\n",name.c_str(),pixels.size());
}
}
extern "C" void pvzh1_menu_bake_fragment(uint32_t *destination,uint32_t premultiplied,uint32_t inverse,int opaque){
 if(!capturing)return;
 const uintptr_t begin=(uintptr_t)target,address=(uintptr_t)destination;
 Check(address>=begin&&(address-begin)%4==0&&(address-begin)/4<H1ScreenWidth*H1ScreenHeight,"capture escaped physical target");
 Check(inverse<=255,"invalid blend inverse");pixels.push_back({(uint32_t)((address-begin)/4),premultiplied,inverse,(uint32_t)opaque});
}
extern "C" void pvzh1_export_selector_bake(){
 directory=std::filesystem::path(std::getenv("PVZ_HOST_OUTPUT"))/"segments";std::filesystem::create_directories(directory);
 manifest.open(directory.parent_path()/"segments.json");manifest<<"{\"physical_width\":"<<H1ScreenWidth<<",\"physical_height\":"<<H1ScreenHeight<<",\"logical_width\":"<<H1LogicalWidth<<",\"logical_height\":"<<H1LogicalHeight<<",\"segments\":[\n";
 auto *screen=gLawnApp->mGLInterface->GetScreenImage();auto *physical=H1GetPhysicalScreen(screen);Check(physical&&physical->Active(),"export requires original physical-screen renderer");target=physical->Target()->GetBits();
 H1RenderPlanTestEnable(false);H1FastReducedSpanTestEnable(false);
 GameSelector selector(gLawnApp);auto *animation=gLawnApp->ReanimationGet(selector.mSelectorReanimID);
 animation->PlayReanim("anim_sign",ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD,0,0);animation->mAnimTime=1;
 selector.mSelectorState=SelectorAnimState::SELECTOR_IDLE;
 if(std::getenv("PVZ_BAKE_ADVENTURE")){
    auto* hand=gLawnApp->AddReanimation(-70.0f,10.0f,0,ReanimationType::REANIM_ZOMBIE_HAND);
    hand->mLoopType=ReanimLoopType::REANIM_PLAY_ONCE_AND_HOLD;
    for(int i=0;i<hand->mDefinition->mTracks.count;i++)if(!strncasecmp(hand->mDefinition->mTracks.tracks[i].mName,"rock",4))hand->mTrackInstances[i].mIgnoreClipRect=true;
    for(unsigned update=0;update<=450;update++){
        if(update)hand->Update();if(update%4&&update!=450)continue;
        Export("hand-"+std::to_string(update),[&]{Graphics g(screen);g.SetLinearBlend(true);g.SetClipRect(0,0,H1LogicalWidth,BOARD_HEIGHT-40);hand->Draw(&g);});
    }
    auto* button=selector.mAdventureButton;button->mBtnNoDraw=false;button->mDisabled=false;button->mOverAlpha=0;
    for(int start:{0,1}){
        button->mButtonImage=start?IMAGE_REANIM_SELECTORSCREEN_STARTADVENTURE_BUTTON:IMAGE_REANIM_SELECTORSCREEN_ADVENTURE_BUTTON;
        button->mOverImage=button->mDownImage=start?IMAGE_REANIM_SELECTORSCREEN_STARTADVENTURE_HIGHLIGHT:IMAGE_REANIM_SELECTORSCREEN_ADVENTURE_HIGHLIGHT;
        selector.TrackButton(button,start?"SelectorScreen_StartAdventure_button":"SelectorScreen_Adventure_button",0,0);button->SetBkgColor(Color(80,80,80));
        const std::string geometry=",\"x\":"+std::to_string(button->mX)+",\"y\":"+std::to_string(button->mY)+",\"width\":"+std::to_string(button->mWidth)+",\"height\":"+std::to_string(button->mHeight);
        for(int state=0;state<3;state++){button->mIsOver=state!=0;button->mIsDown=state==2;Export(std::string("button-100-")+(start?"start80":"80")+"-"+std::to_string(state),[&]{Graphics g(screen);g.SetLinearBlend(true);g.Translate(button->mX,button->mY);button->Draw(&g);},geometry);}
    }
    hand->ReanimationDie();
    manifest<<"\n],\"original_vs_replay_checks\":"<<checks<<",\"passed\":true}\n";manifest.close();return;
 }
 const char *buttons[]={"SelectorScreen_Adventure_button","SelectorScreen_StartAdventure_button","SelectorScreen_Survival_button","SelectorScreen_Challenges_button","SelectorScreen_ZenGarden_button"};
 for(const char *name:buttons)animation->AssignRenderGroupToTrack(name,RENDER_GROUP_HIDDEN);
 auto track=[&](int i){Graphics g(screen);g.SetLinearBlend(true);ReanimatorFrameTime time;animation->GetFrameTime(&time);PvzpTriangleGroup group;animation->DrawTrack(&g,i,0,&group,&time);group.DrawGroup(&g);};
 for(int i=0;i<animation->mDefinition->mTracks.count;++i){
  const std::string name=animation->mDefinition->mTracks.tracks[i].mName;
  auto *instance=&animation->mTrackInstances[i];
  const std::string base="track-"+std::to_string(i);
  const std::string color=",\"color\":"+std::to_string((uint32_t)instance->mTrackColor.ToInt());
  Export(base+"-normal",[&]{track(i);},color);
  if(name=="woodsign2"||name=="woodsign3"){
   instance->mImageOverride=name=="woodsign2"?IMAGE_REANIM_SELECTORSCREEN_WOODSIGN2_PRESS:IMAGE_REANIM_SELECTORSCREEN_WOODSIGN3_PRESS;
   Export(base+"-press",[&]{track(i);},color);instance->mImageOverride=nullptr;
  }
  if(name=="almanac_key_shadow"){
   instance->mImageOverride=IMAGE_REANIM_SELECTORSCREEN_ALMANAC_SHADOW;Export(base+"-almanac",[&]{track(i);},color);
   instance->mImageOverride=IMAGE_REANIM_SELECTORSCREEN_KEY_SHADOW;Export(base+"-key",[&]{track(i);},color);instance->mImageOverride=nullptr;
  }
 }
 struct Button {int id;NewLawnButton *pointer;const char *track;float dx,dy;};
 Button list[]={{100,selector.mAdventureButton,"SelectorScreen_Adventure_button",0,0},{101,selector.mMinigameButton,"SelectorScreen_Survival_button",0,0},{102,selector.mPuzzleButton,"SelectorScreen_Challenges_button",0,0},{103,selector.mOptionsButton,"SelectorScreen_BG_Right",494,434},{104,selector.mHelpButton,"SelectorScreen_BG_Right",576,458},{105,selector.mQuitButton,"SelectorScreen_BG_Right",644,469},{106,selector.mChangeUserButton,"woodsign2",24,10},{108,selector.mAlmanacButton,"SelectorScreen_BG_Right",256,387},{107,selector.mStoreButton,"SelectorScreen_BG_Right",334,441},{109,selector.mZenGardenButton,"SelectorScreen_BG_Right",100,360},{110,selector.mSurvivalButton,"SelectorScreen_ZenGarden_button",0,0},{111,selector.mZombatarButton,"woodsign3",0,0},{112,selector.mAchievementsButton,"SelectorScreen_BG_Left",20,480}};
 for(auto &entry:list){auto *button=entry.pointer;selector.TrackButton(button,entry.track,entry.dx,entry.dy);button->mBtnNoDraw=false;button->mDisabled=false;button->mOverAlpha=0;
  if(entry.id==100){button->mButtonImage=IMAGE_REANIM_SELECTORSCREEN_ADVENTURE_BUTTON;button->mOverImage=button->mDownImage=IMAGE_REANIM_SELECTORSCREEN_ADVENTURE_HIGHLIGHT;}
  for(int tint:{255,128}){if(tint==128&&entry.id!=101&&entry.id!=102&&entry.id!=110)continue;button->SetBkgColor(Color(tint,tint,tint));
   const std::string geometry=",\"x\":"+std::to_string(button->mX)+",\"y\":"+std::to_string(button->mY)+",\"width\":"+std::to_string(button->mWidth)+",\"height\":"+std::to_string(button->mHeight);
   for(int state=0;state<3;++state){button->mIsOver=state!=0;button->mIsDown=state==2;Export("button-"+std::to_string(entry.id)+"-"+std::to_string(tint)+"-"+std::to_string(state),[&]{Graphics g(screen);g.SetLinearBlend(true);g.Translate(button->mX,button->mY);button->Draw(&g);},geometry);}
  }
  if(entry.id==100){button->SetBkgColor(Color::White);button->mButtonImage=IMAGE_REANIM_SELECTORSCREEN_STARTADVENTURE_BUTTON;button->mOverImage=button->mDownImage=IMAGE_REANIM_SELECTORSCREEN_STARTADVENTURE_HIGHLIGHT;selector.TrackButton(button,"SelectorScreen_StartAdventure_button",0,0);const std::string geometry=",\"x\":"+std::to_string(button->mX)+",\"y\":"+std::to_string(button->mY)+",\"width\":"+std::to_string(button->mWidth)+",\"height\":"+std::to_string(button->mHeight);for(int state=0;state<3;++state){button->mIsOver=state!=0;button->mIsDown=state==2;Export("button-100-start-"+std::to_string(state),[&]{Graphics g(screen);g.SetLinearBlend(true);g.Translate(button->mX,button->mY);button->Draw(&g);},geometry);}}
 }
 manifest<<"\n],\"pose_frame\":40,\"frame_base_pose\":"<<animation->mFrameBasePose<<",\"overlay_matrix\":["<<animation->mOverlayMatrix.m00<<','<<animation->mOverlayMatrix.m01<<','<<animation->mOverlayMatrix.m02<<','<<animation->mOverlayMatrix.m10<<','<<animation->mOverlayMatrix.m11<<','<<animation->mOverlayMatrix.m12<<','<<animation->mOverlayMatrix.m20<<','<<animation->mOverlayMatrix.m21<<','<<animation->mOverlayMatrix.m22<<"],\"original_vs_replay_checks\":"<<checks<<",\"passed\":true}\n";manifest.close();std::fprintf(stderr,"MENU BAKE COMPLETE %u checks\n",checks);
}
