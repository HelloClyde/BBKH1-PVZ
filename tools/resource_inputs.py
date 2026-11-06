"""Locate reproducible build inputs without copying them onto the H1."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ARCHIVE=ROOT/'private/resource-build-inputs'

def input_path(runtime, name):
    name=Path(name);assert not name.is_absolute() and '..' not in name.parts
    direct=Path(runtime)/name
    if direct.is_file():return direct
    if Path(runtime).resolve()!=(ROOT/'private/resources-h1-wide').resolve():raise FileNotFoundError(direct)
    for path in (ARCHIVE/'optimized'/name,ARCHIVE/name):
        if path.is_file():return path
    raise FileNotFoundError(direct)
