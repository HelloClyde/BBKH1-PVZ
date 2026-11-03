# H1 menu bake v1

`tools/bake_selector.py` renders the original Selector with the physical
360×270 screen and the existing 9/20 resource set. Its isolated copy of the
original 32-bit SWTri shader observes each final fragment before the original
write. The ordinary renderer still draws the reference frame. Runtime render
plans and the reduced-span shortcut are explicitly disabled for capture.
Production engine files and original resources are not modified by this tool.

The stable pose is global frame **40**: `anim_sign` spans 13..40 and is held.
`anim_open` spans 0..12. `anim_idle` exists in the asset at 41..77 but is not
played by GameSelector. The source contains 48 tracks of 706 transforms each.
The separate clouds, grass, flowers, hand, trophy, dynamic welcome text and
child widgets retain their original paint order and event/update logic.

The runtime virtual path is `/PVZ/h1menu/selector.h1mb`. The selected native
resource directory remains `<drive>:\应用\数据\游戏\PVZ`. The new file is
optional; missing/invalid data or an unrepresented state uses the original
renderer. Original reanimations and images remain installed.

## Encoding

All integers are little endian. Dimensions and destination indices refer to
the **physical 360×270 target**, not the 800×600 logical input space.

Header: 64 bytes, sixteen `u32` values:

| Word | Meaning |
|---|---|
| 0 | `0x424d3148`, bytes `H1MB` |
| 1..3 | version=1, headerBytes=64, exact fileBytes |
| 4..7 | physicalW=360,H=270, logicalW=800,H=600 |
| 8..10 | entryCount, directoryOffset=64, entryBytes=40 |
| 11..12 | held poseFrame=40, trackCount=48 |
| 13..15 | reserved zero |

Each directory entry is ten `u32` values (40 bytes):
`kind,id,variant,colorARGB,runCount,pixelCount,dataOffset,dataBytes,geometry0,geometry1`.

* Kind 1: original Selector track index. Variant 0 is its original image,
  1 is woodsign PRESS, 2 is almanac shadow, 3 is key shadow. Color is the
  exact track ARGB. Empty marker and invisible tracks have valid zero-run
  entries, so a whole group can be preflighted before any replay.
* Kind 2: original NewLawnButton ID (100..112). Variants 0/1/2 are
  normal/hover/pressed; bit 8 selects StartAdventure for ID100. The exact
  background color is part of the key. White is exported for every button;
  gray128 is additionally exported for IDs101,102,110. Disabled-image and
  intermediate hover-fade states fall back when no exact entry exists.
* For kind2, `geometry0=(uint16)x|((uint16)y<<16)` and
  `geometry1=(uint16)width|((uint16)height<<16)`. The runtime verifies these
  against the live logical button geometry. Kind1 geometry words are zero.

Each entry's data is `runCount` run records followed by `pixelCount` u32 words.
A run is 12 bytes: `destinationIndex:u32,count:u16,kind:u16,payloadWordOffset:u32`.
Runs never cross a destination row. The fragment sequence is original painter
order; even overlapping destination ranges are preserved, never sorted.

* Run kind0: final opaque ARGB words (alpha byte `ff`), direct copy.
* Run kind1: `(inverseAlpha<<24)|premultipliedRGB`, with the exact premultiplied
  RGB and inverse computed by the original shader. Per destination word `d`:

```cpp
uint32_t rb = (((d & 0xff00ffu) * inverse) >> 8) & 0xff00ffu;
uint32_t g  = (((d & 0x00ff00u) * inverse) >> 8) & 0x00ff00u;
d = 0xff000000u | (premultipliedRGB + (rb | g));
```

No conventional alpha re-composition or RGB565 conversion is performed.
Keeping each original fragment prevents the non-associative integer rounding
error of flattening multiple translucent layers into one transparent bitmap.

## Required runtime guards

Use only the held sign pose with the ordinary identity selector placement,
full clip and the matching physical target. Opening, sign motion, slide,
starting-game flash, additional filters, shake, unexpected attachments and
unrepresented image overrides fall back. Select entries using the live
render groups, exact image/color state and original widget state. Preflight
the complete group before drawing its first entry. Six clouds remain between
back group1 and normal foreground. Never move these spans across clouds or
the later child-widget/overlay paint boundaries.

Validate all counts, offsets, lengths, run kinds and row/payload bounds before
marking the file ready. Entry buffers may be separately allocated and are
loaded once on selector entry; playback does not open or read files.

## Offline correctness checks

Every entry is compared against the same original draw on three different
patterned opaque backgrounds, over the full target. Missing capture of a
non-SWTri operation fails this comparison. Packing additionally decodes every
merged run back into the exact original fragment tuples. Empty entries are
accepted only when their reference draws leave all three backgrounds intact.
Manifest and verification JSON keep source/profile/resource hashes and button
geometry. These checks establish pixel equivalence for exported entries;
they do not claim that unexported animation states are baked or faster.
