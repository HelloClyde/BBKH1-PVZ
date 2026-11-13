/* Compare actual positioned bank reads with independent stdio and pixels. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <unistd.h>
#include <io.h>
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
extern "C" __declspec(dllimport) DWORD WINAPI GetFinalPathNameByHandleW(HANDLE,LPWSTR,DWORD,DWORD);
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern void H1ActorPosePageTestSlots(unsigned);
namespace {
unsigned reads=0,maxRequest=0;
uint64_t checkedBytes=0;
bool capture=false,inside=false;
void Check(bool value,const char* reason){
 if(!value){fprintf(stderr,"SPARSE_IO: %s\n",reason);throw std::runtime_error(reason);}
}
}
using ReadResult=decltype(::read(0,(void*)nullptr,0));
extern "C" ReadResult __real_read(int,void*,unsigned);
extern "C" ReadResult __wrap_read(int fd,void* buffer,unsigned count){
 const auto at=lseek(fd,0,SEEK_CUR);
 const auto got=__real_read(fd,buffer,count);
 if(!capture||inside||got<=0)return got;
 inside=true;
 wchar_t path[2048]{};
 const auto n=GetFinalPathNameByHandleW((HANDLE)_get_osfhandle(fd),path,2048,0);
 Check(at>=0&&n&&n<2048,"Cannot identify actual bank fd");
 FILE* oracle=_wfopen(path,L"rb");
 Check(oracle&&_fseeki64(oracle,at,SEEK_SET)==0,"Cannot position independent oracle");
 std::vector<unsigned char> expected(size_t(got),0);
 Check(fread(expected.data(),1,size_t(got),oracle)==size_t(got),"Oracle short read");
 fclose(oracle);
 Check(!memcmp(buffer,expected.data(),size_t(got)),"Positioned page differs from file");
 ++reads;checkedBytes+=got;maxRequest=std::max(maxRequest,count);inside=false;
 return got;
}
extern "C" void pvzh1_actor_sparse_io_probe(){
 using namespace Sexy;
 capture=true;H1ActorPosePageTestSlots(2);
 PlayerInfo profile;profile.mName="SPARSE_IO";profile.mLevel=2;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_NORMAL,2,0);
 auto* r=gLawnApp->ReanimationGet(z->mBodyReanimID);
 r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;r->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;
 r->AssignRenderGroupToPrefix("anim_head",RENDER_GROUP_HIDDEN);
 r->AssignRenderGroupToTrack("anim_head1",RENDER_GROUP_NORMAL);
 r->AssignRenderGroupToPrefix("anim_innerarm",RENDER_GROUP_HIDDEN);
 r->AssignRenderGroupToTrack("anim_innerarm1",RENDER_GROUP_NORMAL);
 auto* target=H1GetPhysicalScreen(gLawnApp->mGLInterface->GetScreenImage())->Target();
 auto* bits=target->GetBits();h1DirtyCanvas.enabled=false;unsigned comparisons=0;
 for(unsigned half:{0u,89u,355u,499u})for(unsigned shade:{0u,10u,25u})for(unsigned bg=0;bg<3;++bg){
  r->mFrameStart=half/2;r->mFrameCount=2;r->mAnimTime=(half&1)*.5f;
  r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);
  std::vector<uint32_t> expected;
  for(bool replay:{false,true}){
   H1ActorPoseTestEnable(replay);h1DirtyCanvas.RequireClear();
   for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);
   Graphics g(gLawnApp->mGLInterface->GetScreenImage());auto before=pvzh1_actor_pose_diagnostic[5];
   r->DrawRenderGroup(&g,0);
   if(!replay)expected.assign(bits,bits+480*272);
   else{Check(pvzh1_actor_pose_diagnostic[5]==before+1,"Real zombie did not replay");Check(std::equal(expected.begin(),expected.end(),bits),"Replay pixels changed");++comparisons;}
  }
 }
 Check(reads&&checkedBytes&&maxRequest<=4128,"No bounded storage reads were tested");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"actor-sparse-io-verification.json");
 out<<"{\"passed\":true,\"pixel_comparisons\":"<<comparisons<<",\"file_reads_compared\":"<<reads<<",\"bytes_compared\":"<<checkedBytes<<",\"maximum_request\":"<<maxRequest<<",\"decoded_slots\":2}\n";
 capture=false;h1DirtyCanvas.enabled=true;H1ActorPosePageTestSlots(128);
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
