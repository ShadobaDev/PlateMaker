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

### ~~Wire `ProjectItem::canvasProfileIds` into `CanvasProfileMatcher`~~ ✅

All items completed (schema v2):
- `OutputProfile::id` added.
- `ProjectItem::canvasProfileIds` and `outputProfileId` added.
- `Workspace::activeCanvasProfileName` and `activeOutputProfileName` removed (UI state moved to `MainWindow`).
- `WorkspaceSerializer` updated; v1→v2 migration assigns IDs to legacy OutputProfiles.
- CLI: `project mod --add-canvas-profile / --rm-canvas-profile / --output-profile`; `process` passes `project.canvasProfileIds` to `CanvasProfileMatcher`.
- `ProjectItem::addCanvasProfile()` implemented with conflict guard.
