# TODO

## CLI

### UTF-8 argument encoding on Windows

PowerShell 5.1 (and `cmd.exe`) pass command-line arguments using the active console
code page (e.g. Windows-1250 for Polish locale) instead of UTF-8.  Passing names
with non-ASCII characters (e.g. `--name "Rozdział 01"`) causes
`json.exception.type_error.316: invalid UTF-8 byte` when the workspace is saved.

Two approaches to fix:

**Option A — set UTF-8 console code page at startup (simple):**
```cpp
// cli/main.cpp  — top of main()
#ifdef _WIN32
#include <windows.h>
SetConsoleCP(CP_UTF8);
SetConsoleOutputCP(CP_UTF8);
#endif
```

**Option B — use `wmain` + UTF-16 → UTF-8 conversion (robust):**
```cpp
// cli/main.cpp
#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
{
    std::vector<std::string> args_utf8;
    for (int i = 0; i < argc; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, s.data(), len, nullptr, nullptr);
        args_utf8.push_back(std::move(s));
    }
    // rebuild char* argv from args_utf8 and call run(argc, argv)
}
#endif
```

Option B is more correct — `SetConsoleCP` only affects input read from the console,
not arguments already passed by the shell.

---

## Tests

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

### ~~Per-format output options (`PngOptions` / `WebpOptions`)~~ ✅

`OutputProfile` now models `PngOptions` (compression 0–9, interlaced) and
`WebpOptions` (quality 0–100, lossless, effort 0–6) alongside `JpegOptions`;
serialized additively (guarded, no schema bump). `ImageIO::save()` takes the whole
`OutputProfile` and applies the option struct matching `outputFormat` via
`vips_pngsave` / `vips_jpegsave` / `vips_webpsave`. Format + options live on the
`OutputProfile` (selected per project via `outputProfileId`); the GUI Output tab
edits the selected profile inline (`groupBoxPNG`/`groupBoxJPG`/`groupBoxWebP`).
`OutputProfileDialog` could also be extended to edit the PNG/WebP option fields
(currently only JPEG + format).

### Output size estimation / platform limits

Estimate average & max slice size and total batch size **before** a render (from
canvas/scaling/slice geometry + encoder settings), and/or report actual sizes
**after**. Surface platform caps (Webtoon: ≤ 2 MB per slice, ≤ 25 MB per chapter).
Lib computes the estimate; GUI displays/warns. (Mirrored in the GUI TODO — owner
component TBD, but the estimate logic belongs here.)

### ~~Wire `ProjectItem::canvasProfileIds` into `CanvasProfileMatcher`~~ ✅

All items completed (schema v2):
- `OutputProfile::id` added.
- `ProjectItem::canvasProfileIds` and `outputProfileId` added.
- `Workspace::activeCanvasProfileName` and `activeOutputProfileName` removed (UI state moved to `MainWindow`).
- `WorkspaceSerializer` updated; v1→v2 migration assigns IDs to legacy OutputProfiles.
- CLI: `project mod --add-canvas-profile / --rm-canvas-profile / --output-profile`; `process` passes `project.canvasProfileIds` to `CanvasProfileMatcher`.
- `ProjectItem::addCanvasProfile()` implemented with conflict guard.

---

## Distribution

### Windows MinGW — DLL pruning

**Problem:** `cmake --workflow --preset release-windows-mingw` copies the entire
`mingw64/bin/` directory from the build machine into `bin/`, producing ~175 DLLs
(~190 MB in the ZIP). Most of them (AWS SDK, FFTW3 Fortran, libGLESv2, ncurses, …)
are not needed at runtime by `libplatemaker.dll`.

**Fix — `file(GET_RUNTIME_DEPENDENCIES)` (CMake ≥ 3.16):**

Replace the blanket `install(DIRECTORY "${_vips_bin_dir}/" ...)` in
`lib/CMakeLists.txt` with an `install(CODE ...)` block that computes the actual
transitive DLL closure of `libplatemaker.dll` at install time:

```cmake
# lib/CMakeLists.txt — replaces install(DIRECTORY ...) for MinGW
install(CODE "
    file(GET_RUNTIME_DEPENDENCIES
        LIBRARIES \"\$<TARGET_FILE:platemaker-lib>\"
        RESOLVED_DEPENDENCIES_VAR   _deps_resolved
        UNRESOLVED_DEPENDENCIES_VAR _deps_unresolved
        DIRECTORIES \"${_vips_bin_dir}\"
        PRE_EXCLUDE_REGEXES  \"^api-ms-\" \"^ext-ms-\"
        POST_EXCLUDE_REGEXES \".*[Ss]ystem32/.*\\\\.dll$\"
    )
    foreach(_dep IN LISTS _deps_resolved)
        file(INSTALL \${_dep}
             DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
    endforeach()
    if(_deps_unresolved)
        message(WARNING \"Unresolved runtime deps: \${_deps_unresolved}\")
    endif()
" COMPONENT Runtime)
```

Expected result: ~35–45 DLLs (libvips, format codec DLLs, GLib stack, libstdc++,
libgcc\_s\_seh, libwinpthread). Verify on a clean Windows machine **without MSYS2
installed** before publishing.

**Also consider:**
- Turning the non-empty `_deps_unresolved` case into a hard error to guard against
  a forgotten new format plugin.
- Running `ldd libplatemaker.dll` inside MSYS2 and comparing the list against what
  ends up in the ZIP.

---

### Linux — install script in the tarball

**Problem:** The tarball contains only `libplatemaker.so` + headers. On a fresh
machine the developer must already know they need `libvips-dev` — nothing in the
package tells them this.

**Plan:**

1. Write `cmake/install.sh.in` which CMake configures (substituting `@PROJECT_VERSION@`
   etc.) and which is included in the package as `install.sh`:

```bash
#!/usr/bin/env bash
# install.sh — installs libplatemaker from the extracted tarball
set -euo pipefail

PREFIX="${1:-/usr/local}"

# Check for libvips
if ! pkg-config --exists vips-cpp 2>/dev/null; then
    echo "ERROR: libvips not found. Install it first:"
    if   command -v apt-get &>/dev/null; then echo "  sudo apt install libvips-dev"
    elif command -v dnf     &>/dev/null; then echo "  sudo dnf install vips-devel"
    elif command -v pacman  &>/dev/null; then echo "  sudo pacman -S libvips"
    else echo "  install libvips via your distribution's package manager"
    fi
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
install -d "$PREFIX/lib" "$PREFIX/include" "$PREFIX/lib/cmake/platemaker"
cp -a "$SCRIPT_DIR"/lib/libplatemaker.so*  "$PREFIX/lib/"
cp -r "$SCRIPT_DIR"/include/*              "$PREFIX/include/"
cp    "$SCRIPT_DIR"/lib/cmake/platemaker/* "$PREFIX/lib/cmake/platemaker/"
ldconfig 2>/dev/null || true
echo "libplatemaker installed to $PREFIX"
```

2. Add the script to CPack install rules in `CMakeLists.txt`:

```cmake
if(UNIX AND NOT APPLE)
    install(PROGRAMS "${CMAKE_SOURCE_DIR}/cmake/install.sh"
            DESTINATION ".")
endif()
```

3. Consider also generating a `platemaker.pc` pkg-config file in the package so that
   projects not using CMake can discover the library as well.
