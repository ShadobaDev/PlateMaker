# TODO

## CLI

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

## Library

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

### Dynamic thread spawning for processing

`ProcessingPipeline` runs single-threaded because the virtual strip is built
incrementally and a single slice may span more than one input. A future
pre-process could split the strip at input boundaries into segments that each
yield a whole number of slices; independent segments could then be scaled/sliced
on separate threads and the slice files numbered deterministically afterwards.

### Aggregate `ProcessingPipeline::run()` parameters into a callbacks struct — **breaking, 0.3.0**

`run()` already takes 10 parameters. The next feature (live per-input status for the GUI,
see the GUI TODO) needs an 11th — a per-input callback — which is the tipping point.

Replace the loose trailing parameters with one aggregate:

```cpp
struct ProcessingCallbacks {
    ProgressFn   onProgress;     //!< after each slice is saved
    LogFn        onLog;
    SliceSavedFn onSliceSaved;
    InputDoneFn  onInputDone;    //!< NEW: per input, as the strip is built or it is skipped
};

ProcessingOutcome run(inputs, outProfile, canvasProfiles, canvasProfileIds, outputDir,
                      const CancellationToken& cancel,
                      const ProcessingCallbacks& callbacks = {},
                      const std::unordered_set<std::string>* onlySlices = nullptr) const;
```

Whether the cancellation token joins the struct is open: it is not a callback, and grouping
it would blur "what the caller wants told" with "how the caller stops the run". Leaning
towards keeping it a separate parameter.

Why `onInputDone` is needed at all: inputs are consumed in phase 1 (strip building) before
the first slice exists, and the pipeline currently reports nothing per input, so a GUI cannot
show input progress live no matter what it does on its side.

**This is an API/ABI break → lib 0.3.0**, and the GUI must move in lockstep (it will pin
`find_package(platemaker 0.3.0 …)`).

### Public API: report what the lib links, at which version, under which licence

Consumers currently have to hardcode this. The GUI's About dialog lists libvips and its
licence with the values injected from its **own** CMake — i.e. the GUI is asserting facts
about the lib's dependencies, which it cannot actually know. Swap libvips for another
backend, or bump it across a licence change, and the GUI keeps confidently showing stale
information with nothing to catch it.

The lib is the only thing that knows what it linked, so it should say so:

```cpp
struct LinkedComponent {
    std::string name;     //!< e.g. "libvips"
    std::string version;  //!< runtime version where available, else build-time
    std::string licence;  //!< SPDX id, e.g. "LGPL-2.1-or-later"
};

/// Third-party components this build of libplatemaker links, for About boxes and
/// licence notices (LGPL requires naming what is used and under what terms).
[[nodiscard]] PLATEMAKER_EXPORT std::vector<LinkedComponent> linkedComponents();
```

Pairs with the existing compile-time `platemaker/version.hpp`: that answers "which lib is
this", this answers "what is inside it". Entries carry versions from wherever they are
actually knowable — libvips at **runtime** via `vips_version(0/1/2)`, nlohmann/json from its
`NLOHMANN_JSON_VERSION_*` macros at build time — which also closes the GUI's open item of
showing them at all, without leaking either into the GUI's link line.

Components to report: **libvips** (LGPL-2.1-or-later) and **nlohmann/json** (MIT). GoogleTest
is test-only and never shipped, so it does not belong here.

**Additive**, so no version bump implied; natural to land with the 0.3.0 API work.

**Alternative considered — export it through CMake instead.** The lib could attach the facts
to its exported target and skip the runtime API entirely:

```cmake
target_compile_definitions(platemaker-lib INTERFACE
    PLATEMAKER_VIPS_LICENCE="LGPL-2.1-or-later")
```

`INTERFACE` definitions land in `platemaker-targets.cmake`, so every consumer picks them up
automatically — including one that downloaded a prebuilt package via FetchContent, since the
values are baked into the installed config files. Package-config variables
(`set(platemaker_VIPS_VERSION …)` in `platemaker-config.cmake.in`) work the same way.

It does move ownership to the lib, which is the main point, and it needs no API change — but
it reports what the lib was **built** against, not what the process actually loaded. For an
app that ships its own DLLs (and where a user can swap `libvips-42.dll`), the runtime answer
is the honest one. Not worth maintaining both, so: skip the CMake route and do the function
in 0.3.0.

### Third-party notices for the bundled DLLs

The About dialog names the direct dependencies, but the Windows packages ship the whole
libvips dependency graph (~90 DLLs: glib, libpng, libjpeg, zlib, expat, …), several of them
LGPL. Distributing them carries notice obligations that listing only "libvips" does not
discharge. Worth a generated `THIRD-PARTY-NOTICES` file in the package rather than more rows
in a dialog — the DLL closure is already computed at install time
(`_pm_install_mingw_dll_closure()`), so the list can be derived rather than maintained by hand.

### Package version compatibility is wrong for a 0.x library

`lib/CMakeLists.txt:200` uses `COMPATIBILITY SameMajorVersion`. With major `0`, that treats
**every** `0.y` as compatible — so a GUI pinned to `0.3.0` would happily accept `0.4.0`, even
though under semver a `0.x` minor bump is exactly where breaking changes live (as 0.1.1 → 0.2.0
just was).

Switch to `SameMinorVersion` while the lib is pre-1.0: it accepts only the same `0.MINOR.*`
and still rejects anything older than requested. Cheap now, and it is the difference between
the pin actually holding and only appearing to.

### Persist last render log

The GUI render log is in-memory only (cleared on exit). Optionally persist the
last run's log (and the slice/skip summary) next to the workspace so a user can
review what the previous render did.

---

## Packaging

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

## Releases

### Next release: lib **0.2.0** + GUI **1.0.1** (current state, after manual testing)

`CMakeLists.txt` already says `0.2.0` and the GUI already says `1.0.1` — nothing to bump.

**It must not be tagged `0.1.2`.** What is in the tree now breaks the API against the released
`0.1.1`: `ProjectItem::sanitize()` gained a required parameter and `applyProcessingResults()`
gained two. Under semver a pre-1.0 library signals a breaking change by bumping the **minor**,
so `0.2.0` is correct and `0.1.2` would label a breaking change as a patch — which is precisely
what would let someone's GUI fail to compile against a "patch" update.

Ship both together: a GUI built against `0.1.1` will not compile against `0.2.0`, and vice versa.
Until `0.2.0` is on GitHub Releases, the GUI must keep pointing `PLATEMAKER_DIR` at a local
install — its FetchContent fallback would pull `0.1.1` and fail to build.

### Release after that: lib **0.3.0** + GUI **1.1.0**

Carries the callbacks-struct change above (breaking → minor bump for a 0.x lib). From that
release on the GUI pins `find_package(platemaker 0.3.0 CONFIG REQUIRED)`, so an older lib is
rejected at configure time instead of failing at compile time.

GUI goes to **1.1.0, not 2.0.0**: it gains features (live status, batch render) but breaks
nothing for the user — the workspace format change was additive and reads both ways, so older
builds still open newer workspaces. A major bump should be saved for something that actually
strands the user, e.g. a workspace format older versions cannot read.
