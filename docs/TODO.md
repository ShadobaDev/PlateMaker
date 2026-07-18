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

### Dynamic thread spawning for processing

`ProcessingPipeline` runs single-threaded because the virtual strip is built
incrementally and a single slice may span more than one input. A future
pre-process could split the strip at input boundaries into segments that each
yield a whole number of slices; independent segments could then be scaled/sliced
on separate threads and the slice files numbered deterministically afterwards.

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
