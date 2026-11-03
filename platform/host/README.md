# Complete-game host integration

`tools/build_host.py` compiles the pinned game's complete CMake business-source
set, the H1 platform implementations, official libxmp-lite, and zlib. It does
not link the old simplified tower-defense game or the graphics test fixtures.
The host hardware replacements are the native H1 display, keyboard/touch, clock,
logging and PCM device services in `host_io.cpp`. This runner is headless: it
writes frames and audio to files and accepts scripted events; it does not open
an interactive game window. Optional diagnostic probes use local fixture widgets
with the real engine and renderer, independently of gameplay verification.

Run the commands below from the `h1-pvz-portable` project directory. These are
the matching MSYS2 UCRT64 compiler paths used by the recorded Windows runs;
adjust both paths if that toolchain is installed elsewhere. Prepare the complete
converted resources in `private/resources/` first, as described in the project
README. Choose unused save and output directories for a fresh run.

This reproduces the original new profile, menu/intro, first-level combat, pause
and resume, four-wave win, sunflower reward and level-2 unlock. The controller
only reads existing entities/widgets and sends ordinary mouse/key input through
`Input.cpp`; it does not alter resources, cooldowns, entities, progression or
save files directly.

```powershell
python tools/build_host.py --cxx C:/msys64/ucrt64/bin/g++.exe --cc C:/msys64/ucrt64/bin/gcc.exe --jobs 4 --run --native-loading-progress --trim-unused-atlases --atlas-entry-probe --allocator-probe --autoplay-first --stop-after-first-win --speed 3 --frames 14000 --script platform/host/first-adventure.script --save build/host/demo-fresh-save --output build/host/demo-first
```

Reopen that same save with the original continue dialog, then enter the original
options and return to the main menu:

```powershell
python tools/build_host.py --cxx C:/msys64/ucrt64/bin/g++.exe --cc C:/msys64/ucrt64/bin/gcc.exe --jobs 4 --run --native-loading-progress --trim-unused-atlases --autoplay-first --speed 3 --frames 1150 --script platform/host/resume-menu.script --save build/host/demo-fresh-save --output build/host/demo-restore
```

`--native-loading-progress` exercises the H1 loading pump. `--trim-unused-atlases`
adds a host graphics-cache audit, including an exact-pixel rebuild comparison.
The shared platform default retains a 7 MiB file-image cache after completed
frames. This covers immutable source pixels, not the whole application heap;
an active drawing batch may exceed it and native allocation pressure still
uses the tested one-source/retry policy.
Before a root draw to the actual screen, the platform also releases unused
reconstructible atlases. All holder instances, including dead instances still
awaiting the original deletion queue, remain protected. Nested and offscreen
draws cannot trigger this collection.
On first successful animation XML load, the platform also replaces growth
arrays with exact-sized storage before the original missing-field inheritance.
Every track, frame and original image/font/text pointer is retained.
Immutable RGB3 files of at least 800,000 pixels can use lossless 3072-byte RGB pages for integer,
normal, white blits to the opaque screen. Original clipping and offscreen
early returns remain in effect. Other modes retain the original renderer;
generic `GetBits()` releases paged storage before allocating ARGB. No image
dimensions or samples are reduced. Pages take part in the same cache budget,
completed-epoch pinning, pressure and mutation cleanup.
The 800x600 Title retains its original contiguous ARGB/RL allocation path;
the original 1400x600 gameplay background remains eligible for pages.
Global allocation pools choose a modest refill batch only when first
initialized: `clamp(4096 / itemSize, 1, 16)`. Further growth remains unlimited.
At root screen entry, fully empty pools can release their slabs while retaining
allocator identity and configuration. Live and pending-deletion allocations
remain protected. Completed background and complete pool-texture batches also
provide scoped pixel-use boundaries; original water motion and layers remain.
Only the unused SelectorScreen definition can also be retired after leaving
the menu. A fixed ownership table captures its XML strings before inheritance;
loading, live/dead holders, menu widgets and Credits prevent retirement. The
original definition is reset completely and reloaded by the original parser
when returning to the menu. A six-generation fixture checks exact one-time
string deletion, field/render equality and shutdown cleanup; the real v4/menu
run confirms retirement and reloading during ordinary gameplay.
`--stop-after-first-win` requests the normal window-close/shutdown path after
the original game reaches level 2. The frame limits are ceilings, not claims
about fixed run duration. `--speed 3` accelerates the host elapsed clock for
integration testing and provides no H1 performance measurement.

Default resources are `private/resources/`, a generated private copy of all
original assets. The host output and disposable test profile live under
`build/host/run/` and `build/host/save/`. Override these paths with `--resources`,
`--output` and `--save`. The runner does not delete existing save data, but the
original game can update it during play and shutdown. Use a separate new
directory for original new-profile creation and separate output directories for
each run. Reusing an output directory can mix old screenshots and appended logs.

Script input uses original 800×600 game coordinates and H1 keyboard matrix
numbers. Syntax is `frame N COMMAND A B` or `scene S N COMMAND A B`, where the
latter waits N presented frames after the first entry into original scene S.
Commands are `click`, `down`, `up`, `move`, `key`, `capture`, and `quit`. A click
emits real pointer-down/up events through the H1 `Input.cpp`; a key emits real
key transitions. The script never sets a level, profile, plant, or resource.

The default clock uses host wall time. `PVZ_HOST_SPEED` optionally scales that
clock. `PVZ_HOST_MAX_FRAMES` bounds a run and requests the game's real Shutdown
path, equivalent to closing a platform window. `PVZ_HOST_CAPTURE_EVERY` controls
the periodic screenshot interval, with a default of 60 presented frames.

Artifacts:

- `compile-report.json`: every compiled source, hash, compiler flags, errors and
  whether the complete executable linked.
- `pvz.log`: original game/platform logs plus script inputs.
- `states.jsonl`: original scene, level, wave, sun, update counters, source-cache
  statistics, and audio sample counts at scene changes and periodic checkpoints.
- `frame-*.ppm`: actual original-size frames from the production renderer.
- `audio.wav`: actual mixed PCM from the SFX/music backends.
- `crash.txt`: Windows exception code and address if a runtime crash occurs.

`build/host/verification.json` binds the recorded first-level and restore runs
to their prepared-source, compiler, executable, input and save hashes. The
current 7 MiB, root-entry atlas collection, animation array compaction,
paged-RGB, global-pool, Selector retirement and exact font-palette evidence is in
`run64-paged-font-first/` and `run65-paged-font-restore/`.
Those full-game runs retain their recorded shader/source identities. The final
transformed-texture and two-button-label change is separately exercised by
`run66-raster-pages-resume-menu/`: original v4 Continue, Options and return to the
original menu. `build/host/final-ui-verification.json`, generated with
`python platform/host/verify_final_ui.py`, binds that current run and the real
Selector/texel differential fixture. It does not relabel earlier full-game
evidence as a new full first-level run.
The first run includes the separate atlas-entry fixture described below.
Earlier font/raster/pressure diagnostics remain in `run42-staged-probes/`,
which used 6 MiB and preceded root-entry collection and array compaction. The report records the
specific changed sources and keeps those historical checks distinct from the
new callback's fixture and current complete-game runs.
The verified first-level win and level-2 unlock/restore do not establish
compatibility with all later levels, modes or a physical H1.

`--atlas-entry-probe` tests the root-screen callback using the actual sunflower
definition, original atlas builder, original holder and widget manager. It
compares complete screen and rebuilt atlas pixels, verifies restored image
handles, protects active and dead holder instances, and checks that nested
and offscreen draws retain outer atlas pointers. These temporary graphics
fixtures are removed before normal gameplay; their JSON is separate from the
ordinary-input first-level and save evidence.

`--allocator-probe` uses a real planted animation to compare screen pixels
before and after empty-pool collection. It checks live and dead track pointers,
allocator identity, zeroed regrowth, and byte-identical saves through the
original v4 writer. `build/global-allocator-policy-test/` separately exercises
the original allocator methods under both refill policies.
`build/pool-epoch-probe/` compares complete original day/night water rendering
at two counters across root, nested and offscreen drawing, including pressure
reclamation and outer raw-pointer safety. These reports remain separate from
the ordinary-input gameplay evidence.

`build/definition-compaction-probe/` keeps a separate test-only baseline loader
without the automatic compaction hook and frozen original host objects. All
146 resource animations are compared before and after the same helper: every
frame field and original pointer, name/text contents, four original-renderer
screen poses and atlas pixels remain equal. Its 7,707,512-byte native array
saving describes the complete library, not the definitions present in any one
scene or an assurance of contiguous native space. The largest exact native
replacement array is 34,848 bytes. The JSON report and baseline identity are
linked from `build/host/verification.json`.

`build/paged-rgb-probe/` uses the current `H1Image` header ABI and current
production objects. Fifteen clip/pan/mode cases compare the actual cache and
GL backend with original `MemoryImage::Blt`, including unchanged draw flags.
It checks current-epoch pins, eviction/reload, partial allocation/read failure
cleanup, no extra warm reads, and that pages are freed before generic ARGB
allocation. `assets/raw-rgb-verification.json` records the separate C reader's
byte comparisons, input rejection and mixed-read tests.

The complete-game report also records the actual background during the full
Intro/camera pan and first level. It stays in 821 pages with 2,520,000 RGB bytes,
loads once per descriptor, and never materializes source ARGB or RL pixels on
that observed path. These host assertions prevent a large host heap from
concealing a fallback allocation; native memory and frame rate still require
firmware measurements.

Original A8 font masks can enter the original software palette branch before
the blit borrows source pointers. Each alpha byte is an index into the exact
256-entry `(alpha << 24) | composeRGB` table. This A8 branch applies only to
validated mask-only files. Existing ARGB buffers are never replaced while a caller may hold
them. Generic `GetBits`, scaled fonts and changed point sizes retain the
original exact expansion path.

After building the current complete host, reproduce the real Font32 glyph
comparison with:

```powershell
python platform/graphics/tests/run_alpha_palette.py
```

It writes `build/alpha-palette-probe/`: both actual Font32 descriptors, 18
color/fade/alpha-target/compose-color/scale/point-size cases, identical original
ARGB and production-loader pixel hashes, and object/source identities. Twelve
ordinary-size cases retain palette data without the 900508-byte ARGB buffer;
six scaled/point-size cases exercise the original expansion and remain pixel
identical. The separate `build/alpha-loader-probe/` report covers original
Normal/Additive blits, current-epoch pins, eviction/reload, warm I/O, partial
failure cleanup, raw-pointer preservation, self blits and non-mask rejection.

Immutable RGBA font strips within the normalized `data/` directory can use a
separate exact palette of at most 256 complete ARGB values. It preserves RGB
even at zero alpha. A 257th distinct color rejects the optimization and records
that rejection for the descriptor lifetime; generic original rendering remains
available without quantization. Actual headers, dimensions and path identity
are checked, and RGB3 images, modified/sanded/alpha-grid sources and self-blit
preparation are excluded. The original HouseOfTerror28 strip has 43 colors:
233550 index bytes plus a 1024-byte table replace its 934204-byte ARGB buffer.

`python tools/tests/test_rgba_font_palette.py` reproduces the separate actual
HouseOfTerror28 glyph/direct-blit, exact full-RGBA restoration, 257-color
rejection, partial failure, pressure/pin and generic-expansion checks. It writes
`build/rgba-font-palette-probe/`; its source/object identity and result are bound
by the complete-game verification report. Scaled/point-size font rendering may
still require the original contiguous ARGB representation.

For a validated RGBA font with more than 256 colors, ordinary normal/additive
glyph draws can retain the original pixels in 4 KiB ARGB pages. Each clipped
glyph is copied into a small temporary `MemoryImage` with the complete source's
alpha/transparency flags, then drawn by the original blitter. This reuses the
original blend arithmetic. The source pages enter the existing budget, pins,
eviction and mutation handling; generic `GetBits` releases pages before its
original full allocation. Glyph rectangles are bounded to 256x256 and at most
16384 pixels. Other requests retain their original fallback.

`python tools/tests/test_font_pages.py` writes `build/font-page-probe/`, comparing
real Dwarven36 glyphs, normal/additive colors and alpha destinations, single-pixel
mode-sensitive crops, warm I/O, partial failure cleanup and exact generic
expansion. Current ordinary Options draws use 271 pages holding 1109200 bytes
with a maximum 4096-byte data allocation. The full source ARGB/native-alpha/RL
buffers stay absent; small temporary glyph buffers and allocator overhead remain.
The complete-game report records that observed default UI path separately from
scaled/point-size fallback tests.

Large immutable RGBA textures also support synchronous paged SWTri views. Only
five texture reads change storage access; original unsigned indices, end guards,
UV calculations, interpolation, alpha and blend math remain unchanged. Initial
sanding retains the original path. After eviction, a sanded source reloads into
pages and invokes the original neighborhood averaging through three row buffers,
preserving every ARGB value, including transparent RGB. Raster-origin storage
has separate diagnostics from font-glyph storage. RGB3 Title remains on its
previous path.

`python tools/tests/test_texture_pages.py` writes `build/texture-page-probe/`.
It exercises the actual Selector definition/reload and original NO_ATLAS draw,
all 408800 post-sanding pixels of its Right image, interpolation and page edges,
warm I/O, pin/reclamation, partial failure and generic expansion. The final
host UI run records Right in 400 pages with 1635200 pixel bytes and no new large
ARGB allocation on return; the first startup's original sanding allocation is
recorded separately.

`--font-probe` adds a separate host diagnostic at the original font-loading
checkpoint. It reparses the actual loaded descriptors through `FontData`, then
compares every public define string, compiled glyph/layer/kerning field hash,
and original `ImageFont` rendered pixels before and after retiring parser ASTs.
Normal, scaled, and changed-point-size draws are compared. Its JSON report also
estimates native retained allocation sizes using measured MIPS object layouts.
Run this diagnostic separately when collecting gameplay memory peaks, because
the additional test draws naturally load font images.

`--widget-probe` tests the production widget drawing boundary with the real
`WidgetManager`, `Widget::DrawAll`, original selector background and Font24.
It compares the final pixels with and without simulated allocation pressure
between top-level widgets, including a deferred overlay that reloads pixels.
It also checks that offscreen and nested screen calls preserve outer raw
source pointers. The epoch boundary alone never evicts or presents a frame.

`--triangle-probe` exercises the original `PvzpTriangleGroup::AddTriangle`
change-image flush sequence with all three full-size selector background
textures. It compares complete pixels under allocation pressure, then checks
screen, offscreen and nested scope protection. A local fixture also uses the
original atlas builder and verifies exact rebuilt pixels and unchanged early
return behavior. It changes no game entities, resources or profile state.

`--selector-probe` loads the complete original SelectorScreen definition and
uses the real `Reanimation::DrawRenderGroup` and `DrawTrack` matrix path. This
specifically covers the selector's `NO_ATLAS` flag: these images do not use the
triangle-group path. The fixture compares whole-screen pixels with pressure
after each completed matrix draw, checks all three background calls, and repeats
the comparison with additive/overlay draws, offscreen and nested calls.

Forced-pressure fixtures repeat the one-source cache callback through eligible
source exhaustion to test worst-case pixel and pointer safety. The native
allocation bridge separately retries its real allocation after each release and
stops immediately on success. `tools/test_pressure_retry.py` tests that exact C
module with fragmented/coalescing allocations and reentry. These host tests do
not establish native storage throughput; the firmware's volatile cache counters
record loads, evictions, frames and pixel payload bytes for that comparison.

On Windows, memory checkpoints sample allocated UCRT heap entries using
`_heapwalk` and record the process's peak committed memory. A sampled heap peak
can miss transients between checkpoints; process commit includes host runtime
and display-output buffers. These are host integration measurements, not native
H1 heap/performance claims. Reaching the title screen is not proof of complete
gameplay compatibility.

The offline H1 profile retains 800×600 logical game coordinates and original
font metrics. H1I2 files declare their smaller physical texel dimensions. The
renderer samples those dimensions directly; sprite and glyph boundaries use
the same floor mapping as the converter. Startup fixes the scaled-pack atlas
policy before loading any animations. `private/resources` remains the baseline;
the separately generated full profile is `private/resources-h1-full`.

Build and run the physical screen profile in an independent directory:

```powershell
python tools/build_host.py --build-dir build/host-physical --cxx C:/msys64/ucrt64/bin/g++.exe --cc C:/msys64/ucrt64/bin/gcc.exe --native-loading-progress --physical-screen
python tools/build_host.py --build-dir build/host-physical --cxx C:/msys64/ucrt64/bin/g++.exe --cc C:/msys64/ucrt64/bin/gcc.exe --native-loading-progress --physical-screen --no-prepare --run --resources private/resources-h1-full --save build/host-physical/my-save --output build/host-physical/my-run --frames 1000 --script platform/host/first-adventure.script
```

Only the screen uses a 360×270 backing image. Its public dimensions, clips and
input coordinates stay 800×600; ordinary offscreen images keep their own stride.
An unexpected screen `GetBits` expands once and permanently returns to the full
canvas, preserving writable raw-pointer callers. The physical-screen diagnostic
records this fallback, and normal menu/game/Options runs must keep its count at
zero. Presented host PPMs from this profile are 360×270. `--physical-screen` is a
renderer build option, independent of `--speed`, which only changes the host test
clock. Native firmware performance must be measured separately.

Reproducible focused tests are `tools/tests/test_reduced_mapping.py`,
`test_reduced_renderer.py`, `test_reduced_opaque.py`, and
`test_physical_screen.py`. They cover shared floor boundaries, high-contrast
neighboring cells, mirrors/rotation/clipping, opaque nearest row mapping,
coverage and scanline pixel centers, and the permanent raw-pointer fallback.
`platform/host/verify_scaled_menu.py` binds actual menu frames, executable and
resource identities and verifies zero warm image I/O.
