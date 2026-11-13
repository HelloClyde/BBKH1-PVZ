/* Host-only attribution: observe the production raster without replacing it.
 * Counts describe this fixed replay, not timings or a native FPS measurement. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpParticle.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/MemoryImage.h"
#include "graphics/SWTri.h"
#include "widget/WidgetManager.h"
#include "Common.h"
#include <cmath>
#include <map>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>

extern void H1PreloadCurrentBoardForPlay(LawnApp*);
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
namespace {
struct Origin {const char* owner="board_or_ui";int ownerType=-1,reanimation=-1,particle=-1;};
Origin origin;
struct Scope {Origin previous;Scope():previous(origin){}~Scope(){origin=previous;}};
using Key=std::tuple<std::string,int,int,int,std::string>;
struct Counts {unsigned calls=0,triangles=0,three=0,four=0,offscreen=0,axis=0,additive=0,modulated=0,vertex=0,blend=0,width=0,height=0,celWidth=0,celHeight=0;};
std::map<Key,Counts> records;
using MissKey=std::tuple<int,int,int,unsigned,unsigned,unsigned>;
struct Miss {unsigned calls=0;float rate=0,matrix[4]{};uint64_t key=0,withoutAttachments=0;bool signature=false;int base=0,attachments=0,oldHalf=-1;std::string attachmentTracks;};
std::map<MissKey,Miss> actorMisses;
std::map<Reanimation*,int> blendSources;
bool enabled=false;
void Check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
}
using namespace Sexy;
#define SHAPE_ARGS SWHelper::XYZStruct* vertices,int count,MemoryImage* image,const Color& color,int mode,const Rect& clip,void* surface,int pitch,int format,bool blend,bool vertexColor
void RealShape(SHAPE_ARGS) asm("__real__ZN4Sexy8SWHelper11SWDrawShapeEPNS0_9XYZStructEiPNS_11MemoryImageERKNS_5ColorEiRKNS_5TRectIiEEPviibb");
void ObserveShape(SHAPE_ARGS) asm("__wrap__ZN4Sexy8SWHelper11SWDrawShapeEPNS0_9XYZStructEiPNS_11MemoryImageERKNS_5ColorEiRKNS_5TRectIiEEPviibb");
void ObserveShape(SHAPE_ARGS){
 if(enabled){
  std::string name=image?image->mFilePath:"<solid>";
  if(image&&name.empty())name="<generated "+std::to_string(image->mWidth)+"x"+std::to_string(image->mHeight)+">";
  auto& row=records[{origin.owner,origin.ownerType,origin.reanimation,origin.particle,name}];
  if(image){row.width=image->mWidth;row.height=image->mHeight;row.celWidth=image->GetCelWidth();row.celHeight=image->GetCelHeight();}
  ++row.calls;row.triangles+=count>=3?count-2:0;row.three+=count==3;row.four+=count==4;
  row.additive+=mode==Graphics::DRAWMODE_ADDITIVE;row.modulated+=color!=Color::White;row.vertex+=vertexColor;row.blend+=blend;
  if(count){
   float x0=vertices[0].mX,x1=x0,y0=vertices[0].mY,y1=y0;bool axis=true;
   for(int i=0;i<count;++i){x0=std::min(x0,vertices[i].mX);x1=std::max(x1,vertices[i].mX);y0=std::min(y0,vertices[i].mY);y1=std::max(y1,vertices[i].mY);}
   for(int i=0;i<count;++i)axis&=(std::fabs(vertices[i].mX-x0)<.0001f||std::fabs(vertices[i].mX-x1)<.0001f)&&(std::fabs(vertices[i].mY-y0)<.0001f||std::fabs(vertices[i].mY-y1)<.0001f);
   row.axis+=axis;row.offscreen+=x1<clip.mX||y1<clip.mY||x0>=clip.mX+clip.mWidth||y0>=clip.mY+clip.mHeight;
  }
 }
 RealShape(vertices,count,image,color,mode,clip,surface,pitch,format,blend,vertexColor);
}

void RealPlant(Plant*,Graphics*) asm("__real__ZN5Plant4DrawEPN4Sexy8GraphicsE");
void ObservePlant(Plant*,Graphics*) asm("__wrap__ZN5Plant4DrawEPN4Sexy8GraphicsE");
void ObservePlant(Plant* p,Graphics* g){Scope s;origin.owner="plant";origin.ownerType=int(p->mSeedType);RealPlant(p,g);}
void RealZombie(Zombie*,Graphics*) asm("__real__ZN6Zombie4DrawEPN4Sexy8GraphicsE");
void ObserveZombie(Zombie*,Graphics*) asm("__wrap__ZN6Zombie4DrawEPN4Sexy8GraphicsE");
void ObserveZombie(Zombie* p,Graphics* g){Scope s;origin.owner="zombie";origin.ownerType=int(p->mZombieType);RealZombie(p,g);}
void RealParticle(PvzpParticleSystem*,Graphics*) asm("__real__ZN18PvzpParticleSystem4DrawEPN4Sexy8GraphicsE");
void ObserveParticle(PvzpParticleSystem*,Graphics*) asm("__wrap__ZN18PvzpParticleSystem4DrawEPN4Sexy8GraphicsE");
void ObserveParticle(PvzpParticleSystem* p,Graphics* g){Scope s;origin.particle=int(p->mEffectType);RealParticle(p,g);}
void RealReanim(Reanimation*,Graphics*) asm("__real__ZN11Reanimation4DrawEPN4Sexy8GraphicsE");
void ObserveReanim(Reanimation*,Graphics*) asm("__wrap__ZN11Reanimation4DrawEPN4Sexy8GraphicsE");
void ObserveReanim(Reanimation* p,Graphics* g){Scope s;origin.reanimation=int(p->mReanimationType);RealReanim(p,g);}
void RealReanimGroup(Reanimation*,Graphics*,int) asm("__real__ZN11Reanimation15DrawRenderGroupEPN4Sexy8GraphicsEi");
void ObserveReanimGroup(Reanimation*,Graphics*,int) asm("__wrap__ZN11Reanimation15DrawRenderGroupEPN4Sexy8GraphicsEi");
void ObserveReanimGroup(Reanimation* p,Graphics* g,int group){Scope s;origin.reanimation=int(p->mReanimationType);RealReanimGroup(p,g,group);}
void RealNoteBlend(Reanimation*,int) asm("__real__Z20H1ActorPoseNoteBlendP11Reanimationi");
void ObserveNoteBlend(Reanimation*,int) asm("__wrap__Z20H1ActorPoseNoteBlendP11Reanimationi");
void ObserveNoteBlend(Reanimation* p,int time){
 if(p&&p->mReanimationType==REANIM_PEASHOOTER&&time==20&&(p->mFrameStart==29||p->mFrameStart==54)){
  ReanimatorFrameTime f;p->GetFrameTime(&f);int half=f.mAnimFrameBeforeInt*2;
  if(f.mAnimFrameAfterInt!=f.mAnimFrameBeforeInt){if(f.mFraction>=.75f)half=f.mAnimFrameAfterInt*2;else if(f.mFraction>=.25f)++half;}
  blendSources[p]=half;
 }
 RealNoteBlend(p,time);
}
bool RealActorPose(Reanimation*,Graphics*,int) asm("__real__Z15H1ActorPoseDrawP11ReanimationPN4Sexy8GraphicsEi");
bool ObserveActorPose(Reanimation*,Graphics*,int) asm("__wrap__Z15H1ActorPoseDrawP11ReanimationPN4Sexy8GraphicsEi");
bool ObserveActorPose(Reanimation* p,Graphics* g,int group){
 unsigned before[7];for(unsigned i=0;i<7;++i)before[i]=pvzh1_actor_pose_diagnostic[i];
 const bool replay=RealActorPose(p,g,group);
 if(enabled&&!replay&&p){
  unsigned reasons=0;for(unsigned i:{2u,3u,4u,6u})if(pvzh1_actor_pose_diagnostic[i]!=before[i])reasons|=1u<<i;
  unsigned blend=0;for(int i=0;i<p->mDefinition->mTracks.count;++i)blend=std::max(blend,unsigned(p->mTrackInstances[i].mBlendCounter));
  auto& row=actorMisses[{int(p->mReanimationType),p->mFrameStart,p->mFrameCount,blend,unsigned(p->mEnableExtraAdditiveDraw)|(unsigned(p->mEnableExtraOverlayDraw)<<1),reasons}];
  ++row.calls;row.rate=p->mAnimRate;row.matrix[0]=p->mOverlayMatrix.m00;row.matrix[1]=p->mOverlayMatrix.m01;row.matrix[2]=p->mOverlayMatrix.m10;row.matrix[3]=p->mOverlayMatrix.m11;
  row.signature=H1ActorPose::Signature(p,group,row.key,true,true);row.base=p->mFrameBasePose;row.attachments=0;
  row.attachmentTracks.clear();
  for(int i=0;i<p->mDefinition->mTracks.count;++i)if(p->mTrackInstances[i].mAttachmentID!=ATTACHMENTID_NULL){++row.attachments;if(!row.attachmentTracks.empty())row.attachmentTracks+=',';row.attachmentTracks+=p->mDefinition->mTracks.tracks[i].mName;}
  // Compare identity words without changing the real actor. In this specific
  // non-per-track schema: eight header words, then five words per track;
  // the second word is the attachment-presence flag.
  row.withoutAttachments=14695981039346656037ull;unsigned word=0;
  H1ActorPose::VisitSignature(p,group,[&](uint32_t value){if(word>=8&&(word-8)%5==1)value=0;H1ActorPose::Mix(row.withoutAttachments,value);++word;},true,true,false);
  if(blendSources.contains(p))row.oldHalf=blendSources[p];
 }
 return replay;
}

extern "C" void pvzh1_triangle_source_inventory_probe(){
 PlayerInfo profile;profile.mName="DENSE_TRIANGLE_INVENTORY";profile.mLevel=8;profile.mId=1;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 // The probe enters at loading-complete. Retire the title widget through the
 // original fast-load entry; otherwise its Update spawns loader decorations
 // into the same global effect holder while the board is being observed.
 gLawnApp->FastLoad(GAMEMODE_ADVENTURE);
 // Match TryLoadGame: create a clean board, then use Board::LoadGame so that
 // saved background/layer/widget state is restored too. NewGame's chooser
 // and introduction must not survive into this saved-game observation.
 gLawnApp->MakeNewBoard();
 auto* board=gLawnApp->mBoard;
 const auto input=std::filesystem::absolute("build/track-invalidation-v45-hardware/fixture-dense-plants.v4");
 Check(board->LoadGame(input.string()),"Cannot load the physical dense-board input");
 board->mPaused=false;board->mX=130;board->mY=0;gLawnApp->mGameScene=SCENE_PLAYING;
 H1PreloadCurrentBoardForPlay(gLawnApp);
 unsigned plants=0,zombies=0;for(auto* p:board->mPlants)plants+=!p->mDead;for(auto* z:board->mZombies)zombies+=!z->mDead;
 Check(plants==45&&zombies==5&&board->mMainCounter==226,"Unexpected inventory initial state");
 Check(!gLawnApp->mTitleScreen&&!gLawnApp->mSeedChooserScreen,"Unrelated startup widgets are still active");
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)
  Check(r->mDead||(r->mReanimationType!=REANIM_LOADBAR_SPROUT&&r->mReanimationType!=REANIM_LOADBAR_ZOMBIEHEAD),"Unrelated loader animation is still active");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();
 const auto replayBefore=pvzh1_actor_pose_diagnostic[5];
 SRand(80319); // Fixed legal random stream for repeatable inventory counts.
 records.clear();enabled=true;
 for(unsigned frame=0;frame<74;++frame){
  for(unsigned update=0;update<9;++update)gLawnApp->UpdateFrames();
  h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();
  Check(gLawnApp->mWidgetManager->DrawScreen(),"Inventory unexpectedly skipped the original screen draw");
  H1ImageCacheEndFrame();
 }
 enabled=false;Check(board->mMainCounter==892,"Inventory did not perform the expected real updates");
 unsigned total=0,triangles=0;for(auto& [key,row]:records){total+=row.calls;triangles+=row.triangles;}
 Check(total>0&&pvzh1_actor_pose_diagnostic[5]>replayBefore,"Triangle hook or offline replay was not exercised");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"triangle-source-inventory.json");
 out<<"{\"host_observation_only\":true,\"gameplay_calls_forwarded_unchanged\":true,\"fixed_random_seed\":80319,\"initial_plants\":45,\"initial_zombies\":5,\"main_counter\":[226,892],\"updates\":666,\"draws\":74,\"raster_entry_calls\":"<<total<<",\"input_strip_triangles\":"<<triangles<<",\"offline_actor_replays\":"<<pvzh1_actor_pose_diagnostic[5]-replayBefore<<",\"rows\":[";
 bool first=true;for(auto& [key,r]:records){if(!first)out<<',';first=false;
  const auto& [owner,type,reanim,particle,image]=key;
  out<<"{\"owner\":"<<std::quoted(owner)<<",\"owner_type\":"<<type<<",\"reanimation\":"<<reanim<<",\"particle\":"<<particle<<",\"image\":"<<std::quoted(image)<<",\"calls\":"<<r.calls<<",\"triangles\":"<<r.triangles<<",\"three_vertices\":"<<r.three<<",\"four_vertices\":"<<r.four<<",\"outside_clip_bounds\":"<<r.offscreen<<",\"axis_aligned_bounds\":"<<r.axis<<",\"additive\":"<<r.additive<<",\"modulated\":"<<r.modulated<<",\"vertex_color\":"<<r.vertex<<",\"blend\":"<<r.blend<<",\"logical_size\":["<<r.width<<','<<r.height<<"],\"logical_cell\":["<<r.celWidth<<','<<r.celHeight<<"]}";
 }
 out<<"],\"actor_replay_misses\":[";first=true;
 for(auto& [key,r]:actorMisses){if(!first)out<<',';first=false;auto[type,start,count,blend,flags,reasons]=key;
  out<<"{\"type\":"<<type<<",\"start\":"<<start<<",\"count\":"<<count<<",\"blend\":"<<blend<<",\"extra_flags\":"<<flags<<",\"reason_mask\":"<<reasons<<",\"calls\":"<<r.calls<<",\"rate\":"<<r.rate<<",\"signature_accepted\":"<<(r.signature?"true":"false")<<",\"signature\":"<<r.key<<",\"signature_without_attachments\":"<<r.withoutAttachments<<",\"base_pose\":"<<r.base<<",\"attachments\":"<<r.attachments<<",\"attachment_tracks\":"<<std::quoted(r.attachmentTracks)<<",\"blend_from_half\":"<<r.oldHalf<<",\"linear_matrix\":["<<r.matrix[0]<<','<<r.matrix[1]<<','<<r.matrix[2]<<','<<r.matrix[3]<<"]}";
 }
 out<<"]}\n";gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
