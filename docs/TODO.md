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
  baseline — ship a MINOR before the pending PATCH and that patch becomes `0.3.1`, the next MINOR
  becomes `0.4.0`.

Baseline: **0.2.1 released, 0.3.0 in progress** (`CMakeLists.txt`). The additive `0.2.2` work
(buildInfo, SBOM) was folded into `0.3.0`, which also makes breaking preset/CLI changes — so the
release is a minor, and `0.2.2` never ships on its own.

---

## PATCH — next: 0.3.1

Fixes and additive changes — nothing a consumer must react to. Code built against 0.3.0 keeps
compiling. (These ride `0.3.1` now that `0.3.0` takes the minor slot; per the cascade rule above,
anything finished in time may still be bundled into `0.3.0` before it ships.)

### Persist last render log

The GUI render log is in-memory only (cleared on exit). Optionally persist the
last run's log (and the slice/skip summary) next to the workspace so a user can
review what the previous render did.

### Dynamic thread spawning for processing

`ProcessingPipeline` runs single-threaded because the virtual strip is built
incrementally and a single slice may span more than one input. A future
pre-process could split the strip at input boundaries into segments that each
yield a whole number of slices; independent segments could then be scaled/sliced
on separate threads and the slice files numbered deterministically afterwards.

### Third-party notices: extend the SBOM to the full bundled DLL graph

**Foundation shipped in 0.3.0.** The package now emits `credits/sbom.spdx.json` (an SPDX 2.3 SBOM)
plus `credits/licenses/` — but only for the **direct** dependencies (libvips, nlohmann/json). The
Windows packages still ship the whole libvips dependency graph (~90 DLLs: glib, libpng, libjpeg,
zlib, expat, …), several LGPL, and distributing them carries notice obligations that listing only
"libvips" does not discharge.

What remains is to enumerate that closure into the same SBOM (more `packages[]` entries + their
licence texts). The DLL closure is already computed at install time
(`_pm_install_mingw_dll_closure()`), so the list can be derived rather than maintained by hand; a
scanner such as `syft` over the packaged `bin/` is the likely tool. Each DLL needs mapping to its
SPDX licence and text — note MSYS2 does **not** ship a licence file for every package (libvips
itself has none under `share/licenses/`), so some canonical texts must be vendored the way
`lib/cmake/licenses/` already does for LGPL-2.1 and MIT.

### Slim the MinGW package by switching to the `vips-dev-x64-web` variant

The MinGW build ships a much larger libvips than MSVC — **not** because of the compiler,
but because the two branches source different libvips builds
([CMakeLists.txt](../CMakeLists.txt#L54-L95)):

- **MSVC** FetchContents libvips' official `vips-dev-x64-web-8.18.2` zip — a deliberately
  minimal build: **38 DLLs / 22.7 MB**, and no `vips-modules-*` dir (so the "unable to load
  vips-heif/jxl/magick/…" warnings don't appear either).
- **MinGW** uses MSYS2's `mingw-w64-x86_64-libvips` via pkg-config — the **full** build
  (every loader + poppler/openslide/magick/raw/OpenEXR/…), ~92 DLLs / 34 MB even after the
  existing DLL pruning. MSYS2 offers no minimal/web libvips package, so pacman can't provide
  the web variant.

**Plan: point the MinGW branch at the same web zip the MSVC branch already downloads.**
Verified feasible:

- **Formats cover our needs.** The web zip ships PNG, JPEG, WebP, TIFF (+ GIF/HEIF). Platemaker
  saves PNG/JPEG/WebP and loads PNG/JPEG, so nothing we use is missing; only heavy formats we
  never touch (PDF/poppler, openslide, magick, matio, cfitsio, OpenEXR, raw) are gone.
- **ABI is clean because we use the vips C API only** — `grep` confirms zero `VImage` / `vips::`
  / `<vips/vips8>` usage. The web zip's C++ runtime is LLVM libc++ (`libc++.dll`/`libunwind.dll`),
  so linking `libvips-cpp` would clash with our libstdc++ — but we must link the **C** `libvips`
  (not `vips-cpp`), and the C boundary is ABI-stable regardless of the DLL's C++ runtime. Note
  CMake currently links `VIPS::vips-cpp`; switch the alias to the C `libvips`.
- **MinGW can link the web zip's import libs.** It ships a `.def` for every library, so
  `dlltool -d libvips.def -l libvips.dll.a -D libvips-42.dll` produces GNU import libs; modern
  `ld` also often links the MS `.lib` directly.

Side benefits beyond size: fewer DLLs means a smaller `DLL_PROCESS_DETACH` surface at exit —
i.e. less exposure to the loader-shutdown deadlock recorded in
[ISSUES.md](ISSUES.md) (already fixed via fast-exit, but a smaller graph lowers the odds
regardless), and unifies MSVC + MinGW on one libvips source of truth.

**Only real unknown — resolve with a ~30 min spike first:** does MinGW `ld` link the web zip's
libs directly, or is the `dlltool`-from-`.def` step required? Point the MinGW build at the
downloaded `build/msvc-release/_deps/vips_binaries-src`, link the C `libvips`, build the CLI and
run ctest. If green, plan the full switch (CMake branch, DLL-closure/deploy code, README,
CMakePresets).

---

## MINOR — next: 0.4.0

Breaking changes — a consumer must rebuild or adapt. These ship together, and the GUI pins the
version in lockstep.

### Structured error system

The pipeline currently reports failures as ad-hoc strings via the
`ProcessingPipeline` log/`errorMessage` callbacks (the GUI shows them verbatim).
Replace with structured error codes/categories (load / profile-match / slice /
encode / io) carrying a stable code + message, so the GUI can localise, group and
react (e.g. offer "open output folder", "re-scan inputs"). `ProcessingOutcome`
would carry typed errors instead of a plain string.

**Concrete case to fold in — an unreadable file fails silently and loops forever.**
`FileMetaData::computeFileSha256()` returns an empty string both when the file is absent and
when it exists but cannot be opened; the two are indistinguishable. `applyProcessingResults()`
then skips that input's update entirely (`if (!h.empty())` in
`lib/src/models/project_item.cpp`), leaving `status`, `sha256` and `lastProcessed` untouched.
An input that was never processed therefore stays `Pending` even after a successful render,
`sanitize()` agrees, and the next render redoes all the work and overwrites the output —
indefinitely, with nothing reported anywhere.

This is exactly how the non-ASCII path bug stayed invisible (fixed in 0.2.1). The cause is
gone, the *mechanism* is not: a locked file, missing permissions, or an offline network drive
all produce the same silent loop. Whatever shape the structured errors take, a hash that
fails after a successful render has to surface — at minimum the input must not be left
claiming a state the render did not reach.

**Deliver it through the `ProcessingCallbacks` structure — a typed `onError`.** The `run()`
callbacks already exist (`onProgress` / `onLog` / `onInput` / `onSlicingStarted` / `onSliceSaved`
/ `onSliceSkipped`), so the live half of this system is a new field: `onError(ProcessingError)`
carrying the **typed** error (stable code + category `load`/`profile-match`/`slice`/`encode`/`io`,
the file/slice it happened on, maybe the exception type), fired from the catch sites. It is the
live twin of the typed errors `ProcessingOutcome` would then carry.

Deliberately **not** a string `onError` "like `onLog` but stronger": that was considered and
rejected because it duplicates what already exists — `onLog(Error)`, `outcome.errorMessage`, and
`onInput(InputResult{SkippedError, detail})` — and no consumer needs it. Keep the fatal/non-fatal
line the current code already draws: a **fatal** error also sets `outcome.failed` (the return is
the authoritative "did it fail"); a **non-fatal** per-input problem stays on `onInput`, so `onError`
is not spammed for benign skips. The typed payload is also far cleaner across a future C boundary
than string-scraping the log.

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

---

## Release history & coordination

**Shipped:** `0.1.0` → `0.1.1` → `0.2.0` → `0.2.1` (lib); GUI `1.0.0` → `1.0.1` → `1.1.0`.
`0.2.0` broke the API against `0.1.1` (`ProjectItem::sanitize()` gained a required parameter,
`applyProcessingResults()` two) — breaking, hence the minor, not `0.1.2`. `0.2.1` was additive
plus fixes, so a patch. `0.2.2`'s additive work (buildInfo, SBOM) was folded into `0.3.0`, which
also removes/changes the preset & CLI API — breaking, hence the minor. **In progress: `0.3.0`.**

**Order is forced: lib first.** The GUI pins `LIBPLATEMAKER_VERSION`, which also builds the
FetchContent URL, so until a lib version is on GitHub Releases anyone without a local
`LIBPLATEMAKER_DIR` gets a 404. Tag and upload the lib, then the GUI.

**Lockstep on a breaking lib bump.** When the lib reaches `0.3.0` the GUI moves with it and pins
`find_package(platemaker 0.3.0 CONFIG REQUIRED)`, so an older lib is rejected at configure time
instead of failing at compile time.
