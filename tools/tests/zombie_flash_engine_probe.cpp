/* Exact ordinary and hit-flash replay, plus real menu cache lifetime. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/EarlyLawn.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[12],pvzh1_early_lawn_diagnostic[8];
namespace{void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}}
extern "C" void pvzh1_zombie_flash_probe(){
 using namespace Sexy;PlayerInfo p;p.mName="FLASH_REPLAY";p.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 Reanimation* r=nullptr;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;unsigned comparisons=0;
 auto fill=[&](unsigned bg){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};
 for(unsigned cosmetic=0;cosmetic<6;++cosmetic){auto* z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_NORMAL,2,0);r=gLawnApp->ReanimationGet(z->mBodyReanimID);r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;r->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;
  r->AssignRenderGroupToPrefix("anim_head",RENDER_GROUP_HIDDEN);r->AssignRenderGroupToTrack(cosmetic<3?"anim_head1":"anim_head2",RENDER_GROUP_NORMAL);r->AssignRenderGroupToPrefix("anim_innerarm",RENDER_GROUP_HIDDEN);char arm[32];snprintf(arm,sizeof(arm),"anim_innerarm%u",cosmetic%3+1);r->AssignRenderGroupToTrack(arm,RENDER_GROUP_NORMAL);
 for(unsigned variant=0;variant<3;++variant){
  r->mEnableExtraAdditiveDraw=false;r->mFrameStart=44;r->mFrameCount=47;r->mAnimTime=0;if(variant==1)z->DropArm(0);if(variant==2)z->DropHead(0);
 for(unsigned frame:{0u,1u,88u,89u,182u,183u,234u,235u,354u,355u,356u,357u,432u,433u,498u,499u})for(unsigned shade=0;shade<=25;++shade)for(unsigned bg=0;bg<3;++bg){
  r->mFrameStart=frame/2;r->mFrameCount=2;r->mAnimTime=(frame&1)*.5f;r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);
  H1ActorPoseTestEnable(false);fill(bg);Graphics g(screen);r->DrawRenderGroup(&g,0);std::vector<uint32_t> expected(pixels,pixels+480*272);
  H1ActorPoseTestEnable(true);fill(bg);auto before=pvzh1_actor_pose_diagnostic[5];r->DrawRenderGroup(&g,0);Check(pvzh1_actor_pose_diagnostic[5]==before+1,"flash did not replay");Check(expected==std::vector<uint32_t>(pixels,pixels+480*272),"flash replay pixels differ");++comparisons;
 }}}
 r->mExtraAdditiveColor=Color(100,30,80,255);Graphics g(screen);Check(!H1ActorPoseDraw(r,&g,0),"unsupported tint used gray table");r->mEnableExtraAdditiveDraw=false;r->mColorOverride=Color(255,255,255,120);Check(!H1ActorPoseDraw(r,&g,0),"fade used opaque table");
 h1DirtyCanvas.enabled=true;Check(pvzh1_actor_pose_diagnostic[0]&&pvzh1_early_lawn_diagnostic[0],"missing preloaded cache");gLawnApp->ShowGameSelector();Check(!gLawnApp->mBoard&&!pvzh1_actor_pose_diagnostic[0]&&!pvzh1_early_lawn_diagnostic[0],"return menu retained board-only data");gLawnApp->KillGameSelector();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"zombie-flash-engine-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"intensities\":26,\"unsupported_color_guards\":2,\"menu_releases_board_caches\":true,\"board_cache_bytes_after_menu\":0}\n";
}
