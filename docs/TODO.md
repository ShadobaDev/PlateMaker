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

Baseline: **0.3.1 released; 0.4.0 in progress** (`CMakeLists.txt`). The structured-error system (below)
is breaking, so the in-progress `0.3.2` work (checksums, release CI) re-derives into `0.4.0`.

---

## PATCH — next: 0.4.1

Fixes and additive changes — nothing a consumer must react to. Code built against 0.4.0 keeps
compiling. (These were slated for `0.3.2`, but `0.4.0` ships first — per the cascade rule above they
re-derive to `0.4.1`.)

### ~~Persist last render log~~

Moved to GUI

### Dynamic thread spawning for processing

`ProcessingPipeline` runs single-threaded because the virtual strip is built
incrementally and a single slice may span more than one input. A future
pre-process could split the strip at input boundaries into segments that each
yield a whole number of slices; independent segments could then be scaled/sliced
on separate threads and the slice files numbered deterministically afterwards.

### Third-party notices: full bundled DLL graph — DONE (0.4.0)

Landed. The Windows packages now ship, in `credits/`:
- **`THIRD-PARTY-NOTICES.txt`** — every bundled component (version, upstream, copyright, licence),
- **`licenses/`** — 18 canonical/upstream licence texts,
- **`sbom.spdx.json`** — 32 SPDX packages (the 3 direct deps + the ~29 libvips runtime / compiler-runtime
  components).

Generated at configure time by [`cmake/gen_credits.cmake`](../lib/cmake/gen_credits.cmake) from the
web-build's authoritative `versions.json` (versions) + the curated
[`cmake/third_party.json`](../lib/cmake/third_party.json) (licence / copyright / homepage). The
[`Third-party coverage`](../.github/workflows/thirdparty-coverage.yml) CI guard fails if a bundled DLL
isn't mapped, so a libvips web-build bump can't ship a new undisclosed dependency. The committed
`sbom/sbom.spdx.json` snapshot was refreshed to the full graph.

**⚠ Flagged for a final legal pass before the public release:** the SPDX ids / copyrights in
`third_party.json` are curated best-effort. Two strong-copyleft cases are disclosed as-is —
**libimagequant is GPL-3.0-or-later** (dual w/ commercial) and the **MinGW libstdc++/libgcc are GPL-3.0
WITH the GCC Runtime Library Exception**. Dropping libimagequant (unused PNG quantiser) via a custom
libvips build would remove the only strong-copyleft *runtime* DLL — see the codec-slimming item below.

## MINOR — in progress: 0.4.0

Breaking changes — a consumer must rebuild or adapt. These ship together, and the GUI pins the
version in lockstep.

### Structured error system — DONE (0.4.0)

Landed. `Models::ProcessingError` (`code` + `category` `load`/`profile-match`/`slice`/`encode`/`io`
+ `message` + `file`/`slice`) is one typed vocabulary used wherever a failure is consumed as a
*result*: `ProcessingOutcome::error` (replaces the free-text `errorMessage`), the value now returned by
`ProjectItem::applyProcessingResults()`, and the `errorCode`/`errorCategory` tag on
`Core::InputResult`. The silent unreadable-after-render loop is fixed via a sticky `FileStatus::Error`
(non-forcing in `sanitize()`) plus the returned typed failure.

**No `onError` callback** (revised from the original note above): a live `onError` was rejected as
duplicative — for a *fatal* error the run returns immediately, so `outcome.error` is read at the same
moment; a *non-fatal* per-input skip already flows through `onInput`. `onLog(Error, …)` stays as the
human transcript. Errors are consumed as results, not as a second live stream.

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

## No versioned changes

### Add dependency manifest — done (SBOM submission)

**Done.** GitHub's Dependency graph could not read our CMake dependencies (FetchContent / find_package /
prebuilt libvips zip), and it does **not** ingest an SBOM merely committed to the repo — the *Export
SBOM* button only exports. So instead of a fake `package.json`, we feed the graph through the
**Dependency Submission API**: a committed SPDX snapshot at [`sbom/sbom.spdx.json`](../sbom/sbom.spdx.json)
is submitted by [`.github/workflows/dependency-submission.yml`](../.github/workflows/dependency-submission.yml)
(via `advanced-security/spdx-dependency-submission-action`) on every push touching `sbom/`. This lists
the direct deps (libvips, nlohmann/json) with their `purl`s, so the graph populates and Dependabot can
raise CVE alerts.

The SBOM is a committed snapshot because our deps are pinned and change rarely; regenerate it from
`build/mingw-release/lib/credits/sbom.spdx.json` when a pinned version changes (see `sbom/README.md`).
Extending the snapshot to the full bundled DLL graph is the separate SBOM item under **PATCH** above.

### Release checksums — done (SHA-256 sidecars)

**Done.** Packaging now emits a `<archive>.sha256` sidecar (sha256sum format) next to every CPack
archive — both the `dev` and `cli` packages, in every config — via a `CPACK_POST_BUILD_SCRIPTS` hook
([`cmake/cpack_checksums.cmake`](../cmake/cpack_checksums.cmake)). One sidecar per archive (rather than a
single `SHA256SUMS.txt`) so nothing collides when several configs are uploaded to one Release. The hook
hashes the CPack temp copy but writes the sidecar into `CPACK_OUTPUT_FILE_PREFIX` (`dist/`), where the
release archives land. Users verify with `sha256sum -c <archive>.sha256` or `Get-FileHash`; the hash can
be pasted into the release notes and the binaries uploaded to VirusTotal. Runs automatically from
`cpack --preset …` / the dist workflows — no extra step.

---

## Release history & coordination

**Shipped:** `0.1.0` → `0.1.1` → `0.2.0` → `0.2.1` → `0.3.0` → `0.3.1` (lib);
GUI `1.0.0` → `1.0.1` → `1.1.0` → `1.2.0` → `1.3.0`.
`0.2.0` broke the API against `0.1.1` (`ProjectItem::sanitize()` gained a required parameter,
`applyProcessingResults()` two) — breaking, hence the minor, not `0.1.2`. `0.2.1` was additive
plus fixes, so a patch. `0.2.2`'s additive work (buildInfo, SBOM) was folded into `0.3.0`, which
also removes/changes the preset & CLI API — breaking, hence the minor. `0.3.1` was additive only
(editor snapshot/restore, more presets, MinGW libvips slimming), so a patch. The in-progress `0.3.2`
work (checksums, release CI) never shipped alone: the **structured-error system is breaking, so it
re-derives the in-progress release to `0.4.0`** (checksums/CI ride along). **In progress: `0.4.0`** —
the first release to be cut through the CI workflow. **This is the version intended for public
promotion (Reddit / itch.io).**

**Order is forced: lib first.** The GUI pins `LIBPLATEMAKER_VERSION`, which also builds the
FetchContent URL, so until a lib version is on GitHub Releases anyone without a local
`LIBPLATEMAKER_DIR` gets a 404. Tag and upload the lib, then the GUI.

**⏭️ Cut the next release through the GitHub Actions `Release` workflow**, not a manual local
build + upload. Pushing a bare version tag (e.g. `0.3.2`) runs `.github/workflows/release.yml`:
matrix build (MinGW + Linux) → provenance attestation → `dist/*` archives + `.sha256` uploaded as
Release assets → VirusTotal scan (VT_API_KEY secret is set) appended to the release body. The current
backlog since `0.3.1` is all infra (checksums, CI), so there's nothing to ship *yet* — but the next
real change should validate this end-to-end path for the first time.

**Lockstep on a lib bump the GUI needs.** The GUI pins the lib version it requires — now
`find_package(platemaker 0.4.0 CONFIG REQUIRED)` / `LIBPLATEMAKER_VERSION 0.4.0` (the GUI adopts the
0.4.0 typed errors; 1.3.0 used 0.3.1) — so an older lib is rejected at configure time instead of
failing at compile time. With the config-version file `SameMinorVersion`, that pin also rejects a later
breaking `0.MINOR`. **Order for the 0.4.0 wave: tag+release the lib through CI first, then the GUI**
(the GUI's FetchContent fallback 404s until the lib `0.4.0` assets are on Releases).
