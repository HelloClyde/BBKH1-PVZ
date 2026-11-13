/* Exercise every baked hand pose, repeat and reverse against the original
 * uncompressed bake. This is a display oracle, not gameplay-input evidence. */
#include "LawnApp.h"
#include "Lawn/Widget/GameSelector.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/ScreenLayout.h"
#include "graphics/Graphics.h"
#include "selector/SelectorOffline.h"
#include "Common.h"
#include <zlib.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>
using namespace Sexy;
static unsigned Word(const unsigned char*p){return unsigned(p[0])|unsigned(p[1])<<8|unsigned(p[2])<<16|unsigned(p[3])<<24;}
extern "C" void pvzh1_compressed_hand_probe(){
 auto check=[](bool b,const char*why){if(!b)throw std::runtime_error(why);};
 std::ifstream input(GetResourcePath("h1menu/adventure.h1mb"),std::ios::binary);
 std::vector<unsigned char> old((std::istreambuf_iterator<char>(input)),{});check(old.size()>64,"Missing original uncompressed oracle");
 check(Word(old.data()+52)<=1,"Unknown oracle codec");
 PlayerInfo profile;profile.mName="HAND_TEST";profile.mLevel=1;
 auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->ShowGameSelector();auto* selector=gLawnApp->mGameSelector.get();selector->mSelectorState=SELECTOR_IDLE;
 auto* proxy=H1GetPhysicalScreen(gLawnApp->mGLInterface->GetScreenImage());check(proxy&&proxy->Active(),"No physical target");
 auto* pixels=proxy->Target()->GetBits();Graphics initial(proxy);
 auto* animation=gLawnApp->ReanimationGet(selector->mSelectorReanimID);
 animation->PlayReanim("anim_sign",REANIM_PLAY_ONCE_AND_HOLD,0,30);animation->mAnimTime=1;animation->mLoopCount=1;
 selector->Update();
 check(H1SelectorOfflineDrawGroup(selector,&initial,animation,1),"Menu bundle did not load");
 selector->mStartingGame=true;
 auto* hand=gLawnApp->AddReanimation(0,0,0,REANIM_ZOMBIE_HAND);
 std::vector<unsigned> sequence;for(unsigned i=0;i<=448;i+=4){sequence.push_back(i);sequence.push_back(i);}sequence.push_back(450);
 for(int i=448;i>=0;i-=4)sequence.push_back(unsigned(i));
 unsigned cases=0;
 for(unsigned counter:sequence){
  const unsigned char* row=nullptr;
  for(unsigned i=0;i<Word(old.data()+32);++i){auto*r=old.data()+64+40*i;if(Word(r)==3&&Word(r+4)==counter){row=r;break;}}
  check(row,"Missing oracle pose");
  std::vector<unsigned> expected(480*272);
  for(unsigned i=0;i<expected.size();++i)pixels[i]=expected[i]=0xff000000u|((i*1664525u+counter*1013904223u)&0xffffffu);
  const unsigned runs=Word(row+16);const unsigned char* ops=old.data()+Word(row+24);
  std::vector<unsigned char> raw;
  if(Word(old.data()+52)){
   const unsigned needed=runs*12+Word(row+20)*4;raw.resize(std::max(needed,1u));uLongf bytes=raw.size();
   check(uncompress(raw.data(),&bytes,ops,Word(row+28))==Z_OK&&bytes==needed,"Standard decoder rejected oracle packet");ops=raw.data();
  }
  const unsigned char* words=ops+runs*12;
  for(unsigned i=0;i<runs;++i){auto*r=ops+i*12;unsigned dest=Word(r),count=unsigned(r[4])|unsigned(r[5])<<8,kind=unsigned(r[6])|unsigned(r[7])<<8,at=Word(r+8);
   for(unsigned j=0;j<count;++j){unsigned literal=Word(words+4*(at+j)),p=expected[dest+j],inverse=literal>>24;
    expected[dest+j]=kind?0xff000000u|((literal&0xffffffu)+((((p&0xff00ffu)*inverse)>>8)&0xff00ffu)+((((p&0xff00u)*inverse)>>8)&0xff00u)):literal;}
  }
  selector->mStartingGameCounter=counter;Graphics g(proxy);g.ClipRect(0,0,H1LogicalWidth,BOARD_HEIGHT-40);
  check(H1SelectorOfflineDrawHand(selector,&g,hand),"Baked hand path rejected");
  check(!std::memcmp(pixels,expected.data(),expected.size()*4),"Expanded hand frame pixels differ from original bake");++cases;
 }
 check(pvzh1_selector_offline_diagnostic[2]<5000000,"All hand frames expanded in RAM");
 std::ofstream proof(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"compressed-hand-verification.json");
 proof<<"{\"passed\":true,\"frame_cases\":"<<cases<<",\"original_bake_oracle\":true,\"repeat_and_reverse\":true,\"resident_bytes\":"<<pvzh1_selector_offline_diagnostic[2]<<"}\n";
 gLawnApp->mPlayerInfo=previous;
}
