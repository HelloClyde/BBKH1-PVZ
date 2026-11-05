"""Build the hardware-tested widescreen profile without workspace siblings."""
import argparse,subprocess,sys
from pathlib import Path
from build_paths import ROOT,TOOLCHAIN,RUNTIME_PREFIX
# Normal RGB32 kernels retain their targeted O2 pragma; other generated
# triangle formats use Os to keep the BDA below the firmware caller stack.
HOT=('SoftwareRaster.cpp','RenderPlanCache.cpp','TextReplay.cpp','ActorPose.cpp','EarlyLawn.cpp','RewardEffects.cpp',
     'PhysicalScreen.cpp','GLImage.cpp','GLInterface.cpp','MemoryImage.cpp',
     'MemoryImageCache.cpp','ImageFont.cpp','Reanimator.cpp','h1_host.cpp',
     'mixer.c','period.c','SelectorOffline.cpp')
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--prefix',type=Path,default=RUNTIME_PREFIX)
 p.add_argument('--toolchain',type=Path,default=TOOLCHAIN/'bin')
 p.add_argument('--jobs',type=int,default=6)
 p.add_argument('--build-dir',default='build/native-widescreen')
 p.add_argument('--output-dir',default='dist')
 a=p.parse_args()
 command=[sys.executable,ROOT/'tools/build.py','--prefix',a.prefix.resolve(),'--toolchain',a.toolchain.resolve(),
          '--jobs',str(a.jobs),'--build-dir',a.build_dir,'--output-dir',a.output_dir,
          '--optimization','Os','--xmp-numeric-cache']
 for name in HOT:command.extend(('--optimize-source',name))
 for define in ('PVZH1_PHYSICAL_SCREEN','PVZH1_WIDESCREEN','PVZ_H1_RAW_TICK_HZ=40','PVZ_NATIVE_PROFILE','PVZ_NATIVE_PROFILE_RAW_TICKS'):
  command.extend(('--define',define))
 subprocess.run(list(map(str,command)),cwd=ROOT,check=True)
if __name__=='__main__':main()
