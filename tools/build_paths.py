"""Standalone paths; external compiler/runtime installations remain selectable."""
from pathlib import Path
import os,tempfile
ROOT=Path(__file__).resolve().parents[1]
SDK=Path(os.environ.get('H1_SDK_ROOT',ROOT/'sdk')).resolve()
TOOLCHAIN=Path(os.environ.get('H1_TOOLCHAIN',ROOT/'.tools/toolchain')).resolve()
RUNTIME_WORK=Path(os.environ.get('H1_CXX_WORK',Path(tempfile.gettempdir())/'pvz-h1-cxx')).resolve()
RUNTIME_PREFIX=Path(os.environ.get('H1_CXX_PREFIX',RUNTIME_WORK/'install')).resolve()
MSYS2=Path(os.environ.get('MSYS2_ROOT','C:/msys64')).resolve()
