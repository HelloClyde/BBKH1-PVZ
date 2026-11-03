The optional selector bundle is generated with the real host renderer by
`tools/bake_selector.py`. It contains the original, ordered pixel writes at
360×270, not resampled source textures. Install it as
`h1menu/selector.h1mb` under the selected `/PVZ/` resource root.

`selector_offline_patches.py` changes prepared copies of two display functions
and the selector destructor. The upstream checkout, updates, hit testing,
profiles, saves and animation clocks remain unchanged. The stable, fully open
selector uses offline entries for each track and each child-button state. The
six clouds, attachments, decorations and overlays retain their original order.
Open/sign/slide/start transitions, fractional hover fades, other targets or
clips, unsupported variants and missing/malformed bundles use the original
renderer. The complete bundle is read once per selector instance, as separate
entry allocations; warm frames do not read the file.

Welcome text is dynamic user data. Its first normal draw is cached as the exact
changed pixels and their previous background values. A warm hit checks those
background values and copies the saved output; a changed name/font/profile or
background rebuilds it using the original text function. No source-image raw
pointer is retained. Selector destruction releases all bundle and text buffers.

The file is little-endian. The header has sixteen u32 words: `H1MB`, version 1,
header bytes 64, file bytes, physical width/height 360/270, logical width/height
800/600, entry count, directory offset 64, entry size 40, final sign frame,
track count, and three reserved words. Each directory entry has ten u32 words:
kind, ID, variant, ARGB color, run count, payload word count, data offset, data
bytes, position, dimensions. Data is `runCount` twelve-byte runs followed by
u32 literal payloads. A run is `{u32 destination; u16 count, kind; u32 word}`.
Kind 0 copies final opaque ARGB. Kind 1 stores the original shader's premultiplied
RGB in the low 24 bits and inverse alpha in the high byte; replay uses the same
`/256` integer contribution. Run ordering is preserved across overlaps.

Directory kind 1 identifies an original selector track by index. Variants are
default 0, woodsign press 1, almanac shadow 2 and key shadow 3. Kind 2 identifies
the original button ID; variants are normal 0, hover 1, down 2, disabled 3,
with bit 8 selecting Start Adventure for ID 100. Button position packs signed
logical x/y in two u16 halves, and dimensions pack width/height similarly.

The sixteen diagnostic u32 words are: version; loaded entry count; resident
requested bytes; peak resident requested bytes; group hits; button hits;
welcome hits; group fallbacks; missing file; malformed/load failure; allocation
failure; successful bundle loads; missing variant; welcome rebuilds; reserved;
reserved. These are owned requested payload/metadata bytes, not SDK charged
spans. Peak includes the temporary welcome snapshot when observed. The directory
read scratch and SDK allocator overhead are not reported as resident payload.

Reproduce the loader and renderer fixtures after `tools/build_host.py
--build-dir build/host-offline-menu --physical-screen` with:

```
python tools/tests/test_selector_offline.py
python tools/tests/test_selector_offline_engine.py
```

The engine fixture is a test-only controlled-pose comparison against the same
original widget code with hooks disabled. It is not a claim of hardware input,
frame rate, or a campaign playthrough. The file loader rejects malformed sizes,
directories, runs, row crossings and payload references before displaying any
part of a bundle.
