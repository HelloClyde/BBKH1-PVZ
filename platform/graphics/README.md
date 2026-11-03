# PvZ-Portable H1 software drawing backend

This replaces the OpenGL device of upstream revision
`98b659380014c89c3553fbb2839f5e5a84d6df3d`. It preserves the upstream `Image`,
`Graphics`, `ImageFont`, Lawn, reanimation and particle interfaces and their
original pixel dimensions. It contains no substitute game rules or sprites.

Build replacements:

- Replace `graphics/GLImage.cpp` and `graphics/GLInterface.cpp` with these files.
- Overlay `GLPlatform.h` at the corresponding generated source path. No GLAD,
  OpenGL, shader compiler or SDL graphics context is required.
- Compile `SoftwareRaster.cpp`, `MemoryImageCache.cpp`, the unchanged upstream
  `MemoryImage.cpp`, `SWTri.cpp`, `NativeDisplay.cpp`, `Quantize.cpp` and normal
  framework image/font/graphics sources. `SWTri.cpp` includes its own `.inc`
  implementations; do not compile those includes separately.
- Call `memory_patches.apply(sourceRoot)` on a generated source copy. The patch
  checks the pinned implementation and leaves the upstream checkout unchanged.
- Include this platform directory and `platform/resources` before framework
  include directories. Use the original `GLImage.h` and `GLInterface.h`.

`SoftwareDisplay.h` exposes C callbacks for presenting the original-size
`0xAARRGGBB` frame and notifying resource code after a frame. The normal game
frame is 800×600 (1,920,004 bytes including its memory sentinel). Native H1
display conversion and input coordinate conversion belong to the platform
window/display layer. `mPresentationRect` retains the original aspect ratio.
After presentation the software screen is cleared to opaque black, matching
the original GL swap/clear lifecycle and preventing old title pixels from
surviving in portions of later screens that the original game does not redraw.

Ordinary image blits and the triangle scan converter are upstream software
implementations. The adapter supplies behavior missing from those software
fallbacks: additive primitives and textured triangles, stretch tinting,
mirroring, safe coverage spans, and wrapping with bilinear sampling for the
original pool-caustic texture. Additive triangles use a temporary clipped
bounding box per triangle to preserve overlap order. This favors correctness;
H1 performance has not yet been measured.

The image cache initially stores dimensions, path, automatic-alpha selection,
and each image's original alpha-mask color. Pixel access loads directly into
the original `MemoryImage` backing array, without an additional decoder copy.
The default source-cache budget is 20 MiB. Normal budget trimming happens after
presentation, using pixel-access order to choose file-backed sources.
It never invalidates a naked `GetBits` pointer in the middle of a draw. The
active frame can exceed the budget; completed frames reclaim sources down to
the requested limit. Dynamic surfaces, modified images,
and reanimation atlases are not evicted. Source-cache accounting is separate
from those surfaces and the C++ heap. Original alpha-edge sanding is replayed
after a source reload; arbitrary pixel mutations permanently leave the cache.
Explicit ResourceManager alpha images and alpha grids are recorded with the
descriptor and applied lazily from mask scanlines, using the original blue-byte
alpha and tile layout. The application chooses the native cache budget.
Font loading also leaves unopened file-backed images lazy when the upstream
requests optional lossless palette conversion. Once pixels are resident, the
original palette conversion still runs; later access expands that palette and
releases its index/table buffers instead of loading a second ARGB copy.

On allocation failure, `pvzh1_cache_memory_pressure(size_t)` can additionally
release file-source buffers last accessed in previously completed frames. The
callback never initializes the cache, allocates, logs or changes its descriptor
map, and rejects recursive calls. Every pixel/cache getter and direct palette
reader pins its source to the current frame before allocating or returning a
pointer; image loading and alpha composition pin before their first allocation.
Current-frame sources and dynamic surfaces remain untouched. The callback
reclaims all eligible older sources so the allocator can coalesce free blocks,
then the runtime retries once. Image dimensions and the normal cache budget do
not change. Pressure counters are available as `pvzh1_cache_pressure_diagnostic`.

`ReanimAtlasCache.cpp` supplies the completed-frame callback
`pvzh1_reanim_atlas_end_frame(void*)`. It preserves every atlas referenced by a
live original Reanimation holder entry, including restored save entries. For an
unused atlas it first restores all encoded handles to the original `Image*`,
then discards the atlas. Definitions, full animation tracks, original image
dimensions, effects and instance capacities remain intact. Later initialization
uses the original atlas builder and sanding again. Cross-frame instances in the
pinned engine belong to the holder; temporary stack animations have returned
before the completed-frame callback. Host integration audits preserve active
atlas pointers, compare restored-handle hashes and compare rebuilt atlas pixels.

The checked `data_array_patches.py` patch retains the original logical pool
capacities and ID/index format, while allocating stable pages of 32 original
objects on first access. Pages are value-initialized exactly as the original
array, and remain allocated until the original pool is disposed. Page allocation
happens before free-list or high-water updates, so allocation failure leaves
those fields intact. Numeric address-range checks recover each original index
without subtracting pointers from unrelated allocations. The pinned object
constructors have no random-number or external-allocation side effects; live
slots still undergo the original reset/construction sequence.

Native size probing finds 2,653,312 bytes of original fixed object payload across
the twelve pool types. Touching one 32-slot page of each type uses 91,520 bytes;
the original maximum ID arrays remain allocated. Actual use depends on the
running scene. The portable v4 save interface retains its fields and slot IDs.
Legacy saves that copy C++ object layouts are not certified compatible with this
container layout change; they are distinct from the tested portable v4 path.

The paged-container differential test compares 32- and 64-slot pages against
the unchanged original header, including full 1,024-slot capacity, 100,000 random
allocation/free operations, explicit ID-key wrap, stable live pointers, inactive
slot/save accesses and allocation-failure atomicity:

```powershell
python platform/graphics/tests/run_data_array.py
```

Two original static scratch pools move to equal-capacity lazy heap allocations:
the 65,536-byte polygon coverage buffer and ImageFont's 4,096 render commands.
Their indexing and algorithms are unchanged. The original triangle kernels also
skip a texel when vertex modulation reduces its alpha to zero, preventing the
transparent-destination division by zero observed during the original intro.

Run the isolated host regression suite:

```powershell
python platform/graphics/tests/run.py
```

This compiles the real upstream software graphics with the replacements. Only
the application's surrounding object and unrelated image decoder fallback are
test fixtures. The lazy-cache tests read real `.h1i`/`.h1a` files through the H1
C resource loader. Covered operations include clipping, alpha, tint, additive
accumulation, mirror, stretch, rotation, matrices, normal/mixed triangle batches,
repeat sampling, coverage gaps, transform stack, presentation, metadata-only
loading, original alpha-mask colors and grids, frame-safe eviction/reload,
sanding replay, mutation preservation, polygon scratch allocation and zero-alpha
triangle modulation. These are host tests, not complete-game, firmware or
physical-H1 verification.

`tools/build_host.py` separately compiles the complete original game. Its host
I/O services export actual 800×600 frames, WAV audio and read-only entity logs;
scripted mouse/key events pass through the same platform input layer as H1.
The optional first-level controller reads existing entities and emits ordinary
clicks only. Its complete-game evidence is recorded under `build/host/` and is
distinct from both these unit fixtures and native or physical-device testing.
