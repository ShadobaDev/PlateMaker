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

### Multi-source slices get a black band, and EXIF orientation is ignored → camera-photo inputs render wrong

> **Update 2026-08-17 — Step 1 (diagnostic instrumentation) is done and has been run against the
> three `temp/win10/` photos. It overturned the single-cause hypothesis below: there are TWO
> independent defects, and the *black band* is the bigger, more general one — it is a
> `vips_arrayjoin` layout bug, not the EXIF issue. See "Step 1 — findings" further down.**

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

**Step 1 — instrument the pipeline geometry first (DONE).** Rather than guess at the black-band
mechanism from thumbnails, the library now has a small **component-gated diagnostic logger**
(`platemaker/infrastructure/log/log.hpp`, `Infrastructure::Log`). Each component owns one bit of
a runtime `uint64` mask (`0x1` ProcessingPipeline, `0x2` Scaler, `0x4` ScaledStrip, …); the mask
defaults to 0 so the library is silent unless a host opts components in. Every geometry line lives in
the component that owns it: **ProcessingPipeline** logs each input's header `Xsize×Ysize` + EXIF
`Orientation`; **Scaler** logs `src → scaled` dims; **ScaledStrip** logs each `append`
(`startY`/`height`/`totalHeight`), the `sliceAll` totals (`numFull`/`tail`), and — the decisive one —
each `buildSlice` as `req=[startY,endY) built=W×H parts=N` with every source segment. The CLI turns it
on with the shadow argument `--trace=0x…`; output goes to the logger sink (stderr by default). There
are no severity levels — only the per-component on/off gate. (This replaced an earlier throwaway
`PLATEMAKER_GEOM_TRACE` env-var trace routed through `onLog`.)

Reproduce (uses only the three photos, isolated from the PNG screenshots in `temp/win10/`; `0x7`
= ProcessingPipeline | Scaler | ScaledStrip):

```powershell
platemaker-cli process --workspace ws.platemaker.json --input <dir-with-3-jpgs> `
    --output out --no-profile --target-width 800 --slice-height 1280 --trace=0x7
```

**Step 1 — findings (captured 2026-08-17). Two independent defects:**

**(A) `vips_arrayjoin` black-band padding — this is the black band, and it is NOT the EXIF issue.**
The trace showed `buildSlice 0 req=[0,1280) built=800x1800 parts=3` — the parts summed to
600+600+80=1280 but the slice came out **1800** tall with a solid black bottom third (verified
visually). Isolating it with *two* plain `Orientation 3` photos and `--slice-height 800` reproduced it
with no rotation in play: `buildSlice 0 req=[0,800) built=800x1200` (parts 600+200). Root cause:
`ScaledStrip::buildSlice()`'s
multi-part branch joins with `vips_arrayjoin(parts, …, "across", 1, …)`, and **arrayjoin sizes every
grid cell to the largest input** (default `vspacing` = max height). So any slice that combines sources
of *unequal contributed height* becomes `N × maxHeight` tall, the shorter parts black-filled. This
fires on ordinary multi-page strips whenever a slice boundary falls inside a page — it is only masked
in the author's normal workflow because tall pages make most slices single-source (the single-part
branch is correct and does no join). The three short landscape photos force multi-source slices and
expose it.

**(B) EXIF orientation is ignored** (the original hypothesis, still true and separate). All three
files are stored `3264×2448`; orientations `3, 3, 6`. The `Orientation 6` photo (a portrait shot)
scaled to `800×600` **landscape** instead of `~800×1066` portrait, and the two output files each
inherited their first source's `orientation` tag (`output_001`→`3`, `output_002`→`6`), so viewers
re-rotate them inconsistently. Autorotation is applied nowhere.

**These compound but are fixed separately. Autorot alone (old Step 2) does NOT remove the black band.**

**Step 2a — fix the join (black band; the priority, and general).** Replace the padding `arrayjoin`
in `ScaledStrip::buildSlice()` with a tight vertical concatenation of the same-width parts — fold
`vips_join(a, b, &out, VIPS_DIRECTION_VERTICAL, nullptr)` over `parts` (or `vips_arrayjoin` with an
explicit `vspacing`/tight layout that does not pad). Assert `built.height == sliceH` (except an
intentional `PadWhite` tail) as a post-condition so this can never silently regress. Regression check:
re-render the three photos with `PLATEMAKER_GEOM_TRACE=1` and confirm every `built=` equals the
requested slice height and no output exceeds it.

> **Step 2a — DONE (2026-08-17).** `ScaledStrip::buildSlice()` now folds
> `vips_join(…, VIPS_DIRECTION_VERTICAL)` over the same-width parts instead of the padding
> `vips_arrayjoin`, and asserts `built.height == sliceH` as a post-condition (throws on a padded slice).
> Verified on the three `temp/win10/` photos (`--trace=0x7`): the multi-source slice now reads
> `buildSlice 0 req=[0,1280) built=800x1280 parts=3` (600+600+80 — was `800x1800` with a black band);
> tail `buildSlice 1 built=800x520 parts=1`. Two clean output files, exit 0, assertion silent, full
> suite **145/145** green. **Still pending: Step 2b** — the same trace confirms the `Orientation 6`
> photo still scales landscape `800x600`, so the EXIF defect is untouched and separate.

**Step 2b — normalise to display orientation on load, in both paths.** Apply `vips_autorot` (or load
with autorotate) in `Scaler::scale(filePath)` so the strip is built from display-correct pixels; read
orientation-corrected dimensions in `headerGeometry()` (autorot then `Xsize/Ysize`) so matching and
scaling agree; and strip/normalise the `orientation` tag on output so slices are not re-rotated by
viewers. `vips_autorot` is **idempotent** for `Orientation` absent/`1`, so this is a no-op for the
already-correct Procreate case and only changes the rotated camera photos. Decide the contract for a
genuinely *portrait* page once corrected (e.g. `2448×3264`): match a portrait canvas profile / render
as a tall strip segment, which is what the reporter expected.

Both are **bugfixes** (wrong output → correct output), so PATCH — but both *change the rendered result*
(2a for any multi-page strip with non-aligned boundaries; 2b for any EXIF-rotated input); call them
out in the changelog.

**Also seen (separate, file to its own entry if confirmed): mixed band counts abort a whole render.**
Rendering the `temp/win10/` folder *including* its PNG screenshots failed hard with
`arrayjoin: not one band or 4 bands` — the pipeline does not normalise band count (RGB vs RGBA vs
grey) before joining, so one odd input kills the run instead of being coerced or skipped.

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
