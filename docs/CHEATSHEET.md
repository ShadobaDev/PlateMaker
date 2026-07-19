# Platemaker — build & package cheatsheet

Pipeline: **configure → build → [test] → package**.
Each `cmake --build` / `ctest` / `cpack` needs the matching `cmake --preset`
(configure) to have run first in that build dir. `cmake --workflow` glues
configure + build + package into one command.

Preset names are `platform-buildtype` and tell you the build type directly:
`linux-release`, `linux-debug`, `mingw-release`, `mingw-debug`
(plus `linux-asan` for local sanitizer runs, `msvc-release` / `msvc-debug`).

Every package run produces **two** archives in `dist/`:
- `platemaker-dev-…`  → headers + cmake config + import lib + runtime libs
- `platemaker-cli-…`  → `platemaker-cli` + a self-contained copy of the runtime

Build type is encoded in the archive name (`-release` / `-debug`); debug archives
keep their symbols (no strip).

---

## Linux · Release

```bash
cmake --preset linux-release          # configure
cmake --build --preset linux-release  # build
ctest --preset linux-release          # test (optional)
cmake --workflow --preset dist-linux-release   # configure + build + package
# → dist/platemaker-dev-0.1.1-linux-x86_64-release.tar.gz
# → dist/platemaker-cli-0.1.1-linux-x86_64-release.tar.gz
```

## Linux · Debug

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
cmake --workflow --preset dist-linux-debug
# → dist/platemaker-dev-0.1.1-linux-x86_64-debug.tar.gz   (unstripped)
# → dist/platemaker-cli-0.1.1-linux-x86_64-debug.tar.gz   (unstripped)
```

## Windows MinGW · Release

Run in any shell with the MSYS2 MinGW64 env on PATH (`gcc`, `ninja`, `pkg-config`,
`PKG_CONFIG_PATH` → `C:\msys64\mingw64\lib\pkgconfig`).

```powershell
cmake --preset mingw-release
cmake --build --preset mingw-release
ctest --preset mingw-release
cmake --workflow --preset dist-mingw-release
# → dist/platemaker-dev-0.1.1-windows-mingw-release.zip
# → dist/platemaker-cli-0.1.1-windows-mingw-release.zip
```

## Windows MinGW · Debug

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
cmake --workflow --preset dist-mingw-debug
# → dist/platemaker-dev-0.1.1-windows-mingw-debug.zip    (unstripped)
# → dist/platemaker-cli-0.1.1-windows-mingw-debug.zip    (unstripped)
```

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
cmake --workflow --preset dist-msvc-release
# → dist/platemaker-dev-0.1.1-windows-msvc-release.zip
# → dist/platemaker-cli-0.1.1-windows-msvc-release.zip
```

---

## Package only (build already done)

`cpack` reuses an existing build tree and emits both archives:

```bash
cpack --preset linux-release      # or linux-debug / mingw-release / mingw-debug
```

## Local install (develop the Qt GUI against the lib)

Installs to `install/<preset>/` (no archive). Point the GUI's `PLATEMAKER_DIR`
at that folder.

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
cmake --install build/mingw-debug
# → install/mingw-debug/   (set PLATEMAKER_DIR to this in Qt Creator)
```

```bash
cmake --install build/linux-system
cmake --install build/linux-debug
cmake --install build/linux-release
```
> Linux builds in `$HOME/build/platemaker/<preset>` (not `./build`), so install from there:
> ```bash
> cmake --install ~/build/platemaker/linux-release
> ```
> `--config Release` is only needed for MSVC (multi-config); Ninja presets (MinGW/Linux)
> bake the build type at configure time.

## Local sanitizer build (not packaged)

```bash
cmake --preset linux-asan
cmake --build --preset linux-asan
ctest --preset linux-asan
```

## MSVC (secondary)

```powershell
cmake --workflow --preset dist-msvc-release
# → dist/platemaker-dev-0.1.1-windows-msvc-release.zip + platemaker-cli-…
```
> The Qt GUI links with MinGW; an MSVC-built lib mismatches its name mangling.
> Use MSVC only for standalone CLI / testing, not for GUI consumption.
