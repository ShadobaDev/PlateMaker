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

Baseline: **0.3.1 released, 0.3.2 in progress** (`CMakeLists.txt`).

---

## PATCH — next: 0.3.2

Fixes and additive changes — nothing a consumer must react to. Code built against 0.3.1 keeps
compiling. (These ride `0.3.2`; per the cascade rule above, whichever section releases first takes
its slot and the rest re-derive.)

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
Windows packages still ship the whole libvips dependency graph (~40 DLLs: glib, libpng, libjpeg,
zlib, expat, …), several LGPL, and distributing them carries notice obligations that listing only
"libvips" does not discharge. Both toolchains now bundle the same libvips web-build graph, so one
closure covers MSVC and MinGW.

What remains is to enumerate that closure into the same SBOM (more `packages[]` entries + their
licence texts). The DLL closure is already computed at install time
(`_pm_install_mingw_dll_closure()`), so the list can be derived rather than maintained by hand; a
scanner such as `syft` over the packaged `bin/` is the likely tool. Each DLL needs mapping to its
SPDX licence and text — the web-zip build ships no per-DLL licence files, so canonical texts must be
vendored the way `lib/cmake/licenses/` already does for LGPL-2.1 and MIT.

## MINOR — next: 0.4.0

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

### Custom libvips build to drop unused codecs (~9 MB) — LOW priority

After switching Windows to the prebuilt `vips-dev-x64-web` build, the bundled third-party DLLs are
~40 / ~25 MB. The largest are **codecs Platemaker never uses** but that `libvips-42.dll` imports
directly, so the install-time closure cannot prune them:

| DLL | Size | Format |
|---|---|---|
| `libaom.dll` | 5.3 MB | AV1 / AVIF |
| `librsvg-2-2.dll` | 2.5 MB | SVG |
| `libheif.dll` | 1.3 MB | HEIF |

That is ~9 MB for formats we neither load nor save (we do PNG/JPEG/WebP/TIFF). The only way to shed
them is a **custom libvips build** with those loaders disabled (e.g. `-Dheif=disabled -Dsvg=disabled`
and dropping aom), replacing the FetchContent of the prebuilt zip. That would take the package under
~17 MB, but it means owning a libvips build (meson/toolchain, per-arch, kept in step with the pinned
version) instead of downloading an official zip — a real maintenance cost for a size-only win, hence
low priority. Applies to both MSVC and MinGW (they now share the same web zip). No API/behaviour
change — the same formats are still supported.

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
(editor snapshot/restore, more presets, MinGW libvips slimming), so a patch. **In progress: `0.3.2`.**

**Order is forced: lib first.** The GUI pins `LIBPLATEMAKER_VERSION`, which also builds the
FetchContent URL, so until a lib version is on GitHub Releases anyone without a local
`LIBPLATEMAKER_DIR` gets a 404. Tag and upload the lib, then the GUI.

**⏭️ Cut the next release through the GitHub Actions `Release` workflow**, not a manual local
build + upload. Pushing a bare version tag (e.g. `0.3.2`) runs `.github/workflows/release.yml`:
matrix build (MinGW + Linux) → provenance attestation → `dist/*` archives + `.sha256` uploaded as
Release assets → VirusTotal scan (VT_API_KEY secret is set) appended to the release body. The current
backlog since `0.3.1` is all infra (checksums, CI), so there's nothing to ship *yet* — but the next
real change should validate this end-to-end path for the first time.

**Lockstep on a lib bump the GUI needs.** The GUI pins the lib version it requires — currently
`find_package(platemaker 0.3.1 CONFIG REQUIRED)` (1.3.0 adopts the 0.3.1 editor snapshot/restore) —
so an older lib is rejected at configure time instead of failing at compile time. With the
config-version file now `SameMinorVersion`, that pin also rejects a later breaking `0.MINOR`.
