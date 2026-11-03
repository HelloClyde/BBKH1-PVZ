The optional `xmp_numeric_patches.py` is not automatically enabled by adding
these files. It leaves the pinned vendor checkout unchanged.

For an explicitly selected next build, import its `apply(prepared_source_root)`
and replace just the two original `vendor/libxmp/src/period.c` and `mixer.c`
entries with the returned Paths. Add `-I <project>/platform/audio` to those C
compilations. The generated copies explicitly include the original vendor C
`common.h` by relative path, avoiding a case-insensitive collision with Sexy's
C++ `Common.h`. Capture the patch, header and both generated-source hashes in
the normal build manifest. Existing `H1_PVZ_PORTABLE` enables the cache; leave
`PVZH1_XMP_NUMERIC_STATS` undefined in the release build.

The cache contains only exact integer keys and original double bit results:
128 two-way note/bend entries and 64 c5spd/frequency/period entries. Actual
MIPS static storage is 3,624 bytes without test counters. No allocation,
module/sample/voice pointers, interpolation, phase, format or clock changes
are involved. A miss executes the pinned original expression in its original
order. Only positive normal inputs/results are memoized; NaN, infinity,
signed zero, subnormals and invalid step inputs retain the original path.

Lifetime is process-static POD. Complete keys make reuse across module unload,
rate changes and independent players safe. No music data remains referenced.
Optional reset functions clear the tables and counters between serial playback
calls; they must not run concurrently with playback. This adapter relies on
the port's existing single-threaded libxmp contract and fixed floating-point
environment. It preserves PCM rather than promising identical floating-point
exception flags for every skipped successful calculation.

Reproduce the streaming cross-process oracle after preparing the existing
unmodified libxmp test objects:

```
python tools/test_xmp_shared.py --cc C:/msys64/ucrt64/bin/gcc.exe
python tools/tests/test_xmp_numeric.py
python tools/tests/test_xmp_numeric_mips.py
```

The last check uses the retained Hot32 target-flags report for a reproducible
MIPS32/o32/soft-float `-Os` instruction proxy; it compiles four objects, does
not build a BDA and does not execute on a device. Existing vendor unused and
signed-comparison warnings are excluded equally for pristine and candidate.

`build/xmp-numeric-candidate/report.json` records two original tracks and the
actual menu order152 stream, each7200 blocks, with exact PCM plus order/row/
frame/speed/BPM/time/loop equality. Streams are compared through bounded pipes,
not saved as large audio files. `mips-proxy.json` records actual object/BSS
sizes and enumerated cache-hit instruction paths, not native cycle counts.
The GUI is dominated by drawing; reduced arithmetic calls alone make no FPS
promise. Integration and a real-device timing window remain separate steps.
