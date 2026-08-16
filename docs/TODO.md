# TODO

Roadmap grouped by **which semver position a change forces**, not by priority or by the order
work happens in.

**How to read this**
- The version numbers are provisional, non-binding hints — a *kind of bump*, not a schedule.
- A section is the **minimum bump the change forces**. This lib is pre-1.0, so the shifted scale
  applies (recorded in `temp/SEMVER.md`): a **breaking** change bumps the **MINOR**, additions and
  fixes bump the **PATCH**. "Breaking" means **removing or modifying** an existing public symbol
  — a changed signature, argument, or semantics — not only deleting it, and it is judged over
  the *whole* change: a new class whose purpose is to take away existing public access is breaking,
  not additive.
- A change that only forces a patch may still be **bundled into a higher release** if it ships
  alongside bigger work. The header is the floor, not an assignment.
- **Cascade.** Whichever section releases first takes its slot; the rest re-derive from the new
  baseline — ship a MINOR before a pending PATCH and the patch re-derives onto the new baseline.

Baseline: **0.4.0 released (2026-08-05); 0.4.1 in progress** (`CMakeLists.txt`). 0.4.1 is binary-identity
metadata only — additive/fixes — so it is a **PATCH**: code built against 0.4.0 keeps compiling.

---

## Done — 0.4.1 (PATCH)

Binary-identity metadata; nothing a consumer must react to.

- **Windows `VERSIONINFO`** on `libplatemaker.dll` and `platemaker-cli.exe` — Explorer's *Details* tab
  and Process Explorer show proper identity instead of blanks. Generated from `PROJECT_VERSION` via
  `lib/cmake/version.rc.in` / `cli/version.rc.in` (MinGW/windres, `WIN32`-only).
- **Embedded `@(#)` version marker** on the library and CLI binaries (all platforms) — the version is
  discoverable *without running the binary* where there is no `VERSIONINFO` resource (Linux/macOS):
  `what libplatemaker.so.<ver>` or `strings … | grep '@(#)'`. Build-tree-only `lib/cmake/ident.hpp.in`,
  kept from stripping with `__attribute__((used))`. Portable companion to the `.so` `SOVERSION`.

---

## PATCH — future

Fixes and additive changes — nothing a consumer must react to; code built against 0.4.x keeps compiling.

### EXIF orientation is ignored → camera-photo inputs render wrong (black band + wrong split)

**Reported from a Windows 10 test with three 3264×2448 phone photos.** Two rendered fine, the third
landed in its own output slice with a black band, instead of all three flowing into one continuous
strip (`output_001` full + tail). See the GUI TODO's matching entry and `temp/win10/`.

**Root cause: the pipeline assumes inputs have no meaningful EXIF orientation, which is false for
camera JPEGs.** Two independent code paths read the image and neither normalises orientation, and
they disagree:

- **Matching reads raw header dimensions.** `headerDim()`
  (`src/core/processing_pipeline/processing_pipeline.cpp:34-42`) returns `img->Xsize / img->Ysize`
  straight from the header — the *stored* pixel dimensions, which ignore the EXIF `Orientation` tag.
- **Scaling loads raw pixels, no auto-rotate.** `Scaler::scale(filePath, …)`
  (`src/core/scaler/scaler.cpp:66`) uses `vips_image_new_from_file` + `vips_resize`; the comment
  states the design assumption explicitly — *"Procreate exports are already in the correct
  orientation … so no auto-rotation handling is needed here."*

The test data breaks that assumption: all three files are stored `3264×2448` (landscape pixels) but
carry EXIF `Orientation` = **3, 3, 6**. The third (`Orientation 6` = rotate 90°) is a *portrait*
photo whose pixels are stored landscape. Every viewer shows it portrait; Platemaker sees landscape
pixels, matches on landscape dimensions, and appends landscape pixels — so its geometry and the
displayed reality diverge, which is what produces the black band and the mis-split. (The two
`Orientation 3` = 180° images survive because a 180° image is still landscape either way; only the
90°/270° cases visibly break.)

**The exact black-band geometry still needs a debug render to pin** — but the defect is clear:
orientation is neither applied to the pixels nor reflected in the dimensions used for matching.

**Step 1 — instrument the pipeline geometry first (do this before any fix).** We are guessing at the
black-band mechanism from thumbnails; add per-stage diagnostic logging so a render of the three
`temp/win10/` photos prints exactly what happened. Emit (via the existing `onLog` channel, at
`Info`/Debug level) for each input, in order:
- **on read** — the raw header `Xsize × Ysize` and the EXIF `Orientation` tag (from
  `vips_image_get_typeof(img, VIPS_META_ORIENTATION)` / `vips_image_get_int`);
- **after scale** — the scaled buffer `width × height` returned by `Scaler::scale`;
- **on strip append** — the entry's `startY` and cached `height`, and the running `m_totalHeight`
  (`ScaledStrip::append`);
- **on slice** — `numFull`, `tail`, and each slice's `[sliceStartY, sliceStartY+sliceHeight)` versus
  which entries it overlaps (`sliceAll` / `buildSlice`).

That trace makes the divergence explicit (where reserved geometry ≠ actual pixels, hence the black
fill) and becomes the regression check after the fix — re-render the same three photos and confirm a
single continuous strip. Keep it as a guarded/verbose log, not always-on spam.

**Step 2 — fix: normalise to display orientation on load, in both paths:**
- Apply `vips_autorot` (or load with autorotate) in `Scaler::scale(filePath)` so the strip is built
  from display-correct pixels.
- Read orientation-corrected dimensions in `headerDim()` (autorot then `Xsize/Ysize`) so matching
  and scaling agree.
- `vips_autorot` is **idempotent** for images with `Orientation` absent or `1` — so this is a no-op
  for the "already correct" Procreate/exported case the current code was written for, and only
  changes behaviour for the rotated camera photos it currently mishandles. Verify against a Procreate
  export (no rotation) to confirm no regression, and re-test the three `temp/win10/` photos.
- Decide the contract for a genuinely *portrait* page once corrected (e.g. a 2448×3264 page): it
  should match a portrait canvas profile / render as a tall strip segment, which is the behaviour the
  reporter expected.

Semantically a **bugfix** (wrong output → correct output), so PATCH — but note it *changes the
rendered result* for any workspace whose inputs carry a non-trivial EXIF orientation; call it out in
the changelog.

### Dynamic thread spawning for processing

`ProcessingPipeline` runs single-threaded because the virtual strip is built
incrementally and a single slice may span more than one input. A future
pre-process could split the strip at input boundaries into segments that each
yield a whole number of slices; independent segments could then be scaled/sliced
on separate threads and the slice files numbered deterministically afterwards.

---

## MINOR — follow-ups (non-breaking) on the 0.4.0 structured-error system

The structured error system shipped in **0.4.0** (breaking; typed `ProcessingError`, the
`applyProcessingResults()` return, the `InputResult` tag). What is left is additive and not yet wired:

Not yet wired (follow-ups, non-breaking): GUI localisation/grouping UI beyond surfacing category/code;
a `ProfileMatch` fatal path (reserved category — implicit render stays non-fatal).

---

## MAJOR — next: 1.0.0

The stability commitment, or a change that strands the user (e.g. a workspace format older
versions cannot read). Nothing here is scheduled.

### C facade / stable ABI — evaluated, deliberately deferred

**Idea:** expose the library through a C ABI, so any compiler on a given platform could consume
one binary regardless of how the lib was built — and, optionally, so the lib could be loaded
dynamically at runtime.

**The diagnosis behind it is correct.** C++ has no standardised ABI; a C++ DLL built with MSVC
is not consumable from MinGW. The proof that the C-ABI answer works is already in our own
dependency tree: **libvips** is usable from both toolchains precisely because it is a C library.

**But name mangling is the smallest part of it.** Measured against the current public API:

| obstacle | scale |
|---|---|
| `std::string` / `std::vector` layouts differ between MSVC STL and libstdc++ | **137 lines** of public headers use them |
| exceptions cannot cross a C ABI at all | **41** `throw std::` sites — the entire error strategy |
| allocate in the DLL, free in the exe with a different CRT = UB | every returned container |
| C has no move semantics | `Workspace`, `ProjectItem`, `PixelBuffer`, `ScaledStrip` are move-only |
| `std::function` callbacks | 4 types → function pointer + `void* userdata` |

So this is not "wrap it in `extern "C"`". It is designing a **second, parallel API** — opaque
handles, out-params, error codes, explicit ownership and free functions — and keeping it in
sync forever. With 37 public classes, a 1:1 mirror would be a maintenance disaster.

**If it were ever built**, the sane shape is a narrow *task-level* API (open/save a workspace,
list and edit profiles, run a render with callbacks, query status) — on the order of 20–30
functions — with the C++ API kept for in-tree consumers. Not a mechanical mirror of the models.

**Why not now:** the consumers are the Qt GUI and the CLI, both built from source with the same
compiler, and the planned web backend calls the **CLI as a subprocess**, where no ABI is
involved at all. Per-toolchain dev packages already cover the distribution case, and the
config-package guard (see the *Library* entry above) closes the practical pain — a mismatch
failing late and cryptically — for a fraction of the cost.

**Triggers that would justify revisiting** (concrete, not vibes):
- shipping a **prebuilt** lib to third parties whose compiler we do not control;
- Python or other language bindings via **ctypes/cffi** — note **pybind11 would not need this**,
  as it compiles against the C++ API with the same compiler;
- a plugin system, or runtime `LoadLibrary` / `dlopen` loading;
- wanting a **single** Windows binary that serves both MinGW and MSVC consumers.

For runtime loading specifically, a C ABI is the prerequisite and the usual shape is a single
`platemaker_get_api(version)` returning a struct of function pointers — but that is another
layer of indirection on top of the facade, and there is no use case for it today.

**Status: recorded, not scheduled.** Revisit only if one of the triggers above actually lands.

---

## No release impact

Investigations and test/dev work that ships no change in the library itself.

### Crash handler for hard faults (segfault / SEH) — deferred, likely not worth it yet

0.4.0 added a Layer-A safety net for **C++ exceptions** (the `ProcessingPipeline::run()` guard + the CLI
top-level `try/catch` in `runCli`). Hard faults — SIGSEGV / null deref / Windows SEH — are **not** C++
exceptions and need an OS-level handler in each app's `main()` (app-level, no lib change). Cost/benefit
verdict (`temp/crash-handling-options.md`, §0): a minidump/Breakpad apparatus is disproportionate for a
simple tool with a small user base; it only helps unreproducible **field** crashes, and archiving only
libplatemaker's small `-g` symbols already covers the frames that matter. **Cheap CLI step, do anytime:**
`std::set_terminate` in `main.cpp`. Defer the OS-level handler until field crashes justify it; the primary
TODO item and full analysis live in the GUI repo / the linked note.

### Stage 1 integration tests (unit tests with real pixel data)

Seven tests are currently stubbed with `GTEST_SKIP()` pending real image fixtures:

| Test | File |
|---|---|
| `PixelBufferTest.MoveConstructorTransfersOwnership` | `test_pixel_buffer.cpp` |
| `PixelBufferTest.MoveAssignmentTransfersOwnership` | `test_pixel_buffer.cpp` |
| `ScalerTest.ScaleNonExistentFileThrows` | `test_pixel_buffer.cpp` |
| `ScaledStripTest.SliceAllWithZeroSliceHeightThrows` | `test_scaled_strip.cpp` |
| `ScaledStripTest.SliceAllCropPolicyDiscardsRemainder` | `test_scaled_strip.cpp` |
| `ScaledStripTest.SliceAllPadWhiteProducesFullHeightTailSlice` | `test_scaled_strip.cpp` |
| `ScaledStripTest.SliceAllKeepAsIsPreservesShortTailSlice` | `test_scaled_strip.cpp` |

Need: small test PNG fixtures in `tests/lib-unit-tests/fixtures/` and a test
helper that calls `vips_init()`/`vips_shutdown()` in a `SetUpTestSuite` /
`TearDownTestSuite` pair.

### Custom libvips build — drop the GPL dep (licensing) + unused codecs (~9 MB)

Not for a near release, but strategically important — see the licensing driver below.

**Primary driver — licensing / a future commercial dual-licence of libplatemaker.** The prebuilt
`vips-dev-x64-web` build is compiled **with libimagequant (GPL-3.0)**, and `libvips-42.dll` imports it
directly, so we ship `libimagequant.dll` in every Windows package. Linking a GPL library makes the
distributed libvips binary a **GPL-3.0 combined work** — so our *bundled* distribution is effectively
GPL-encumbered even though libplatemaker's source is LGPL, and even though we almost certainly never
call the quantiser (we save truecolor PNG/JPEG/WebP, not 8-bit palette PNG). Today that is harmless
(the GUI is GPL-3.0 anyway), **but it blocks a commercial dual-licence of libplatemaker**: you cannot
offer the lib under a proprietary licence while its shipped runtime contains GPL code. Dropping
libimagequant is the prerequisite. If PNG quantisation is ever actually needed, libvips can use
**quantizr (MIT)** instead — a GPL-free swap (needs a Rust toolchain).

**Secondary driver — size (~9 MB).** The prebuilt build also pulls codecs we never use, imported
directly so the install-time closure cannot prune them:

| DLL | Size | Format |
|---|---|---|
| `libaom.dll` | 5.3 MB | AV1 / AVIF |
| `librsvg-2-2.dll` | 2.5 MB | SVG |
| `libheif.dll` | 1.3 MB | HEIF |

~9 MB for formats we neither load nor save (PNG/JPEG/WebP/TIFF only).

**The work.** Replace the FetchContent of the prebuilt zip with a **custom libvips build** (meson) that
disables the GPL quantiser and the unused loaders — `-Dimagequant=disabled` (the licensing fix), plus
`-Dheif=disabled -Dsvg=disabled` and dropping aom (the size win) — bringing the package under ~17 MB and,
crucially, making the bundled runtime **GPL-free**. This means owning a libvips build (meson/toolchain,
per-arch, kept in step with the pinned version) instead of downloading an official zip — a real
maintenance cost, which is why it waits. Applies to both MSVC and MinGW (they share the same web zip).
No API/behaviour change for the formats we support. When done, update `cmake/third_party.json` +
`THIRD-PARTY-NOTICES.txt` (the coverage guard will flag the removed DLLs) and this note.

---

## Release history & coordination

**Shipped:** `0.1.0` → `0.1.1` → `0.2.0` → `0.2.1` → `0.3.0` → `0.3.1` → `0.4.0` (lib);
GUI `1.0.0` → `1.0.1` → `1.1.0` → `1.2.0` → `1.3.0` → `1.4.0`.
`0.2.0` broke the API against `0.1.1` (`ProjectItem::sanitize()` gained a required parameter,
`applyProcessingResults()` two) — breaking, hence the minor. `0.2.1` was additive plus fixes, so a
patch. `0.2.2`'s additive work (buildInfo, SBOM) folded into `0.3.0`, which also removes/changes the
preset & CLI API — breaking, hence the minor. `0.3.1` was additive only (editor snapshot/restore, more
presets, MinGW libvips slimming), so a patch. `0.4.0` (2026-08-05) shipped the **breaking structured-error
system** (typed `ProcessingError`) plus `duplicateProject` and the checksums/release-CI infra — the first
release cut through the CI workflow, and **the version used for public promotion (Reddit / itch.io)**.
`0.4.1` (in progress) is metadata only (VERSIONINFO + `@(#)` marker), so a patch.

**Order is forced: lib first.** The GUI pins `LIBPLATEMAKER_VERSION`, which also builds the
FetchContent URL, so until a lib version is on GitHub Releases anyone without a local
`LIBPLATEMAKER_DIR` gets a 404. Tag and upload the lib, then the GUI.

**⏭️ Cut releases through the GitHub Actions `Release` workflow**, not a manual local build + upload.
Pushing a bare version tag runs `.github/workflows/release.yml`: matrix build (MinGW + Linux) →
provenance attestation → `dist/*` archives + `.sha256` uploaded as Release assets → VirusTotal scan
(VT_API_KEY secret is set) appended to the release body.

**Lockstep on a lib bump the GUI needs.** The GUI pins the lib version it requires — currently
`find_package(platemaker 0.4.0 CONFIG REQUIRED)` / `LIBPLATEMAKER_VERSION 0.4.0` (the GUI adopts the
0.4.0 typed errors) — so an older lib is rejected at configure time. With the config-version file
`SameMinorVersion`, that pin also rejects a later breaking `0.MINOR`. For the **0.4.1 wave**, releasing
the lib lets the GUI bump its pin to `0.4.1` so the bundled `libplatemaker.dll` carries the new metadata;
0.4.1 is additive, so the bump is a one-liner with no code change.
