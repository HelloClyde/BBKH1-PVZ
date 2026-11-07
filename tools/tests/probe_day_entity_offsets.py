"""Compile-only target layout probe. No production source or guest access."""
from pathlib import Path
import argparse,hashlib,json,subprocess,struct
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir',type=Path,default=Path('build/mips'))
parser.add_argument('--source',type=Path)
parser.add_argument('--output',type=Path,default=Path('build/input-offsets'))
parser.add_argument('--reanimations',action='store_true',help='Include read-only animation matrix layout')
args=parser.parse_args()
BUILD=(ROOT/args.build_dir).resolve()
OUT=(ROOT/args.output).resolve()
OUT.mkdir(parents=True,exist_ok=True)
SOURCE=(ROOT/args.source).resolve() if args.source else (BUILD/'source' if (BUILD/'source').is_dir() else ROOT/'build/source')
TC=ROOT.parent/'.tools/toolchain/bin'
report=json.loads((BUILD/'compile-report.json').read_text())
original=(SOURCE/'PvzpLib/DataArray.h').read_text(encoding='utf8')
public=original.replace('template <typename T> class DataArray\n{','template <typename T> class DataArray\n{\npublic:',1).replace('    class PagedItems {','    class PagedItems {\n    public:',1)
assert public!=original
(OUT/'DataArray.public.h').write_text(public,encoding='utf8')
entries=[
 ('pointer_bytes','sizeof(void*)'),('page_size','P::PagedItems::PAGE_SIZE'),
 ('data_array_bytes','sizeof(P)'),('paged_items_bytes','sizeof(P::PagedItems)'),
 ('vector_bytes','sizeof(decltype(((P*)0)->mItems.mPages))'),
 ('unique_ptr_bytes','sizeof(std::unique_ptr<P::DataArrayItem[]>)'),
 ('array.items','OFF(P,mItems)'),('array.page_vector','OFF(P,mItems)+OFF(P::PagedItems,mPages)'),
 ('array.page_capacity','OFF(P,mItems)+OFF(P::PagedItems,mCapacity)'),
 ('array.initialized','OFF(P,mItems)+OFF(P::PagedItems,mInitialized)'),
 ('array.item_ids','OFF(P,mItemIds)'),('array.max_used','OFF(P,mMaxUsedCount)'),
 ('array.max_size','OFF(P,mMaxSize)'),('array.free_head','OFF(P,mFreeListHead)'),
 ('array.size','OFF(P,mSize)'),('array.next_key','OFF(P,mNextKey)'),('array.name','OFF(P,mName)'),
 ('board.menu_button','OFF(Board,mMenuButton)'),('app.widget_manager','OFF(LawnApp,mWidgetManager)'),
 ('board.seed_bank','OFF(Board,mSeedBank)'),('board.cursor','OFF(Board,mCursorObject)'),('cursor.type','OFF(CursorObject,mCursorType)'),
 ('board.cutscene','OFF(Board,mCutScene)'),('board.sod_position','OFF(Board,mSodPosition)'),('board.total_waves','OFF(Board,mNumWaves)'),
 ('cutscene.time','OFF(CutScene,mCutsceneTime)'),('cutscene.sod_time','OFF(CutScene,mSodTime)'),('cutscene.seed_choosing','OFF(CutScene,mSeedChoosing)'),
 ('seed_bank.bytes','sizeof(SeedBank)'),('seed_bank.num_packets','OFF(SeedBank,mNumPackets)'),('seed_bank.packets','OFF(SeedBank,mSeedPackets)'),
 ('seed_bank.x','OFF(SeedBank,mX)'),('seed_bank.y','OFF(SeedBank,mY)'),('seed_packet.stride','sizeof(SeedPacket)'),
 ('seed_packet.type','OFF(SeedPacket,mPacketType)'),('seed_packet.x','OFF(SeedPacket,mX)'),('seed_packet.y','OFF(SeedPacket,mY)'),
 ('seed_packet.width','OFF(SeedPacket,mWidth)'),('seed_packet.height','OFF(SeedPacket,mHeight)'),
 ('seed_packet.active','OFF(SeedPacket,mActive)'),('seed_packet.refreshing','OFF(SeedPacket,mRefreshing)'),
 ('widget.x','OFF(Sexy::Widget,mX)'),('widget.y','OFF(Sexy::Widget,mY)'),('widget.width','OFF(Sexy::Widget,mWidth)'),('widget.height','OFF(Sexy::Widget,mHeight)'),
 ('button.parent_widget','OFF(GameButton,mParentWidget)'),('widget.disabled','OFF(Sexy::Widget,mDisabled)'),('button.no_draw','OFF(GameButton,mBtnNoDraw)'),
 ('button.x','OFF(GameButton,mX)'),('button.y','OFF(GameButton,mY)'),
 ('button.width','OFF(GameButton,mWidth)'),('button.height','OFF(GameButton,mHeight)'),('button.disabled','OFF(GameButton,mDisabled)'),
 ('manager.last_mouse_x','OFF(Sexy::WidgetManager,mLastMouseX)'),('manager.last_mouse_y','OFF(Sexy::WidgetManager,mLastMouseY)'),('manager.focus','OFF(Sexy::WidgetManager,mFocusWidget)'),
 ('app.board','OFF(LawnApp,mBoard)'),('app.scene','OFF(LawnApp,mGameScene)'),('app.mode','OFF(LawnApp,mGameMode)'),
 ('board.bytes','sizeof(Board)'),('board.zombies','OFF(Board,mZombies)'),
 ('board.plants','OFF(Board,mPlants)'),('board.projectiles','OFF(Board,mProjectiles)'),
 ('board.coins','OFF(Board,mCoins)'),('board.paused','OFF(Board,mPaused)'),
 ('board.main_counter','OFF(Board,mMainCounter)'),('board.update_counter','OFF(Board,mBoardUpdateCounter)'),
 ('board.level','OFF(Board,mLevel)'),('board.current_wave','OFF(Board,mCurrentWave)'),
 ('board.sun','OFF(Board,mSunMoney)'),('board.x','OFF(Board,mX)'),('board.y','OFF(Board,mY)'),
 ('plant.stride','sizeof(P::DataArrayItem)'),('plant.bytes','sizeof(Plant)'),
 ('plant.type','OFF(Plant,mSeedType)'),('plant.col','OFF(Plant,mPlantCol)'),
 ('plant.row','OFF(Plant,mRow)'),('plant.health','OFF(Plant,mPlantHealth)'),
 ('plant.max_health','OFF(Plant,mPlantMaxHealth)'),('plant.dead','OFF(Plant,mDead)'),
 ('plant.x','OFF(Plant,mX)'),('plant.y','OFF(Plant,mY)'),
 ('zombie.stride','sizeof(Z::DataArrayItem)'),('zombie.bytes','sizeof(Zombie)'),
 ('zombie.type','OFF(Zombie,mZombieType)'),('zombie.row','OFF(Zombie,mRow)'),
 ('zombie.health','OFF(Zombie,mBodyHealth)'),('zombie.max_health','OFF(Zombie,mBodyMaxHealth)'),
 ('zombie.dead','OFF(Zombie,mDead)'),('zombie.pos_x','OFF(Zombie,mPosX)'),('zombie.pos_y','OFF(Zombie,mPosY)'),
 ('projectile.stride','sizeof(J::DataArrayItem)'),('projectile.bytes','sizeof(Projectile)'),
 ('projectile.type','OFF(Projectile,mProjectileType)'),('projectile.row','OFF(Projectile,mRow)'),
 ('projectile.dead','OFF(Projectile,mDead)'),('projectile.pos_x','OFF(Projectile,mPosX)'),
 ('projectile.pos_y','OFF(Projectile,mPosY)'),('projectile.age','OFF(Projectile,mProjectileAge)'),
 ('coin.stride','sizeof(C::DataArrayItem)'),('coin.bytes','sizeof(Coin)'),
 ('coin.type','OFF(Coin,mType)'),('coin.dead','OFF(Coin,mDead)'),
 ('coin.pos_x','OFF(Coin,mPosX)'),('coin.pos_y','OFF(Coin,mPosY)'),
 ('coin.width','OFF(Coin,mWidth)'),('coin.height','OFF(Coin,mHeight)'),
 ('coin.collecting','OFF(Coin,mIsBeingCollected)'),
]
if args.reanimations:
 entries += [('app.effects','OFF(LawnApp,mEffectSystem)'),('effects.reanimations','OFF(EffectSystem,mReanimationHolder)'),
  ('holder.array','OFF(ReanimationHolder,mReanimations)'),('reanim.stride','sizeof(DataArray<Reanimation>::DataArrayItem)'),
  ('reanim.bytes','sizeof(Reanimation)'),('reanim.type','OFF(Reanimation,mReanimationType)'),
  ('reanim.dead','OFF(Reanimation,mDead)'),('reanim.time','OFF(Reanimation,mAnimTime)'),('reanim.rate','OFF(Reanimation,mAnimRate)'),
  ('reanim.start','OFF(Reanimation,mFrameStart)'),('reanim.count','OFF(Reanimation,mFrameCount)'),
  ('reanim.base','OFF(Reanimation,mFrameBasePose)'),('reanim.matrix','OFF(Reanimation,mOverlayMatrix)')]
cpp='''#include "DataArray.public.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/Projectile.h"
#include "Lawn/Coin.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/Cutscene.h"
#include "Lawn/CursorObject.h"
#include "Lawn/Widget/GameButton.h"
#include "widget/WidgetManager.h"
using P=DataArray<Plant>;using Z=DataArray<Zombie>;using J=DataArray<Projectile>;using C=DataArray<Coin>;
static_assert(sizeof(void*)==4 && sizeof(P)==sizeof(Z) && sizeof(P)==sizeof(J));
static_assert(sizeof(P::DataArrayItem)==sizeof(Plant));
static_assert(sizeof(Z::DataArrayItem)==sizeof(Zombie));
static_assert(sizeof(J::DataArrayItem)==sizeof(Projectile));
#define OFF(T,F) __builtin_offsetof(T,F)
extern "C" const unsigned pvzh1_day_offsets[]={
'''+'\n'.join(' '+expr+', // '+name for name,expr in entries)+'\n};\n'
# Above comments must not consume separators between entries.
cpp=cpp.replace(',, //',', //')
cpp+='''extern "C" void* pvzh1_probe_page_data(P& a){return a.mItems.mPages.data();}
extern "C" unsigned pvzh1_probe_page_count(P& a){return a.mItems.mPages.size();}
extern "C" Plant* pvzh1_probe_page_get(P& a,unsigned p){return a.mItems.mPages[p].get();}
'''
(OUT/'day_entities.cpp').write_text(cpp,encoding='utf8')
flags=report['flags']+['-Wno-invalid-offsetof','-I',str(SOURCE/'PvzpLib')]
command=[str(TC/'mipsel-none-elf-g++.exe'),*flags,'-c',str(OUT/'day_entities.cpp'),'-o',str(OUT/'day_entities.o')]
r=subprocess.run(command,capture_output=True,text=True)
(OUT/'day_entities.log').write_text(r.stdout+r.stderr)
assert r.returncode==0,r.stderr
subprocess.run([str(TC/'mipsel-none-elf-objcopy.exe'),'-O','binary','-j','.rodata.pvzh1_day_offsets',str(OUT/'day_entities.o'),str(OUT/'day_entities.bin')],check=True)
raw=(OUT/'day_entities.bin').read_bytes();assert len(raw)==len(entries)*4,(len(raw),len(entries))
offsets=dict(zip((n for n,_ in entries),struct.unpack('<'+'I'*len(entries),raw)))
assembly=subprocess.check_output([str(TC/'mipsel-none-elf-objdump.exe'),'-dr',str(OUT/'day_entities.o')],text=True)
(OUT/'day_entities.asm').write_text(assembly)
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
elf=BUILD/'H1PVZPortable.elf'
headers=['PvzpLib/DataArray.h','LawnApp.h','Lawn/Board.h','Lawn/GameObject.h','Lawn/Plant.h','Lawn/Zombie.h','Lawn/Projectile.h','Lawn/Coin.h']
result=dict(test_only=True,compile_only=True,production_modified=False,guest_access=False,
 elf_sha256=digest(elf),object_sha256=digest(OUT/'day_entities.o'),
 cpp_sha256=digest(OUT/'day_entities.cpp'),flags=flags,
 headers={name:digest(SOURCE/name) for name in headers},offsets=offsets,
 private_access_note='Only a build-local DataArray header copy exposes access. No member, type, capacity or order changed. Static assertions validate all entity item strides. Page vector/unique_ptr ABI is independently visible in target getter assembly.',
 vector_abi=dict(begin=0,end=4,end_storage=8,element_bytes=4,unique_array_pointer=0))
(OUT/'day_entities.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(offsets,indent=2))
