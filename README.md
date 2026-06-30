# Platemaker

> **Comic artist canvas tool** — pre-processing and post-processing for Webtoon-style publishing.

Platemaker handles both ends of the comic-art workflow: generating canvas templates with margin guides before drawing, and scaling + slicing finished artwork into upload-ready panels. It is designed around the Webtoon vertical-scroll format (800 px wide, 1280 px-high slices) but is architecturally extensible to other formats.

This repository contains **`libplatemaker`** (the core processing library) and the **`platemaker`** CLI binary. The Qt GUI lives in a separate repository and links against `libplatemaker` as a shared library.

For full requirements, data models, pipeline details, and the development roadmap see **[docs/SPECIFICATION.md](docs/SPECIFICATION.md)**.

---

## Project structure

```
platemaker/
├── CMakeLists.txt           # Root — finds dependencies, adds sub-projects
├── CMakePresets.json        # Presets: linux-system, linux-gcc, windows-msvc, …
├── vcpkg.json               # vcpkg manifest — Windows C++ deps
├── lib/                     # libplatemaker — Core + Infrastructure
│   ├── cmake/               # CMake package config template
│   ├── include/platemaker/
│   │   ├── models/          # Shared data types (CanvasProfile, Workspace, …)
│   │   ├── core/            # Pipeline components (Scaler, ScaledStrip, Slicer, …)
│   │   └── infrastructure/  # IO + caching (ImageIO, WorkspaceSerializer, ThumbnailCache)
│   └── src/                 # Implementation files (mirrors include/ structure)
├── cli/                     # platemaker binary — links libplatemaker
├── tests/
│   ├── lib-unit-tests/      # GoogleTest C++ unit tests for libplatemaker
│   └── cli-tests/           # pytest-based Python integration tests for the CLI
└── docs/
    ├── SPECIFICATION.md
    └── CODING_STYLE.md
```

---

## System requirements

### All platforms

| Tool | Version | Notes |
|---|---|---|
| CMake | 3.25+ | Presets format v6 |
| Git | any | |
| Python 3 | any | Only for CLI integration tests |

---

### Windows — MSVC (recommended)

**Tools to install:**

1. **[Visual Studio 2022](https://visualstudio.microsoft.com/)** — select the **"Desktop development with C++"** workload.  
   This installs: MSVC compiler, CMake, Ninja, Windows SDK.

2. **[Git for Windows](https://git-scm.com/download/win)**

No package manager needed. `libvips`, `nlohmann-json`, and `GoogleTest` are all fetched automatically by CMake on the first configure.

**Optional (for CLI tests):**

```powershell
# Python 3 from https://www.python.org — check "Add Python to PATH"
pip install -r tests\cli-tests\requirements.txt
```

---

### Windows — MinGW (MSYS2)

**Tools to install:**

1. **[MSYS2](https://www.msys2.org/)** — installs to `C:\msys64` by default.

2. In the MSYS2 **MinGW64** terminal, install the required packages:

```bash
pacman -S \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-pkg-config \
  mingw-w64-x86_64-libvips
```

`nlohmann-json` and `GoogleTest` are fetched automatically by CMake — no pacman packages needed for them.

**Required environment variable** (set as a permanent Windows user variable):

| Variable | Value |
|---|---|
| `PKG_CONFIG_PATH` | `C:\msys64\mingw64\lib\pkgconfig` |

**Required PATH entry:** add `C:\msys64\mingw64\bin` to your Windows system PATH.  
This makes `gcc`, `g++`, `ninja`, `pkg-config`, and the vips DLLs available in any terminal.

> **Tip:** The MSYS2 installer offers to add itself to PATH automatically — accept that, then also add `C:\msys64\mingw64\bin` manually if it was not added.

---

### Linux / WSL2

Tested on **Ubuntu 22.04 LTS** (native and inside WSL 2).

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build pkg-config git \
    libvips-dev \
    python3 python3-pip
pip install -r tests/cli-tests/requirements.txt
```

`nlohmann-json` and `GoogleTest` are fetched automatically from source if the system packages (`nlohmann-json3-dev`, `libgtest-dev`) are not installed. Installing them via apt is optional but speeds up the first configure.

---

## Using libplatemaker from a release package

Download the pre-built package for your platform from the [Releases page](https://github.com/ShadobaDev/PlateMaker/releases).

### What's in each package

| Package | File | Contents |
|---|---|---|
| Linux x86\_64 | `platemaker-dev-<ver>-linux-x86_64.tar.gz` | `libplatemaker.so` + headers + CMake config |
| Windows MinGW | `platemaker-dev-<ver>-windows-mingw.zip` | `libplatemaker.dll` + headers + CMake config + all runtime DLLs |

The Linux package contains **only** the library and headers — `libvips` and its dependencies must be installed separately (see below). The Windows MinGW package is self-contained: all required DLLs are bundled in `bin/`.

### Linux — system prerequisites

`libplatemaker.so` links dynamically against libvips. Install it before using the package:

```bash
# Ubuntu / Debian
sudo apt install libvips-dev

# Fedora / RHEL
sudo dnf install vips-devel

# Arch Linux
sudo pacman -S libvips
```

`libvips` itself pulls in a chain of transitive dependencies (GLib, libjpeg, libpng, libwebp, libtiff, libheif, …). The `apt`/`dnf`/`pacman` package handles all of these automatically.

No other system packages are required — `nlohmann/json` is a private dependency whose types do not appear in the public API, so consumers do not need its headers.

### Windows MinGW — prerequisites

None. All required DLLs are in the `bin/` directory of the ZIP.

Make `bin/` visible to Windows' DLL loader by doing **one** of the following:
- Copy the contents of `bin/` next to your `.exe`.
- Add the `bin/` directory to your `PATH` (convenient for development).
- Use the CMake snippet below — it copies the DLLs automatically at build time.

### CMake integration

Extract the archive and configure your project with `CMAKE_PREFIX_PATH`:

```cmake
find_package(platemaker CONFIG REQUIRED)
target_link_libraries(my-app PRIVATE Platemaker::platemaker)

# Windows: copy libplatemaker.dll and its runtime DLLs next to the executable
if(WIN32)
    add_custom_command(TARGET my-app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Platemaker::platemaker>"
            "$<TARGET_FILE_DIR:my-app>"
        COMMENT "Copying platemaker runtime DLLs"
        VERBATIM
    )
endif()
```

At configure time, tell CMake where the extracted package lives:

```bash
# Linux
cmake -B build -DCMAKE_PREFIX_PATH="/path/to/platemaker-dev-0.1.1-linux-x86_64"

# Windows (PowerShell)
cmake -B build -DCMAKE_PREFIX_PATH="C:/path/to/platemaker-dev-0.1.1-windows-mingw"
```

CMake caches `CMAKE_PREFIX_PATH`, so you only need to pass it once per build directory.

### Runtime dependency summary

| Platform | Runtime requirement | Provided by package |
|---|---|---|
| Linux x86\_64 | `libvips.so.42` + transitive deps | No — install via package manager |
| Windows MinGW | vips DLLs, GLib, GCC runtime | Yes — bundled in `bin/` |

---

## Build instructions

### Windows MSVC

Open **Developer PowerShell for VS 2022**:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc

# Debug build
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug

# Run tests
ctest --preset windows-msvc
```

### Windows MinGW

Open **any PowerShell** (MSYS2 environment variables must be set — see above):

```powershell
cmake --preset windows-mingw
cmake --build --preset windows-mingw
```

### Linux

```bash
cmake --preset linux-system
cmake --build --preset linux-system
ctest --preset linux-system

# Debug + AddressSanitizer
cmake --preset linux-system-debug
cmake --build --preset linux-system-debug
```

---

## Building the dev package

The dev package contains the shared library, all public headers, CMake config files, and bundled runtime DLLs (Windows).

There are two ways to produce it. They run the **same install rules** (including the MinGW DLL pruning that bundles only libvips' actual runtime-dependency closure) — the difference is what they wrap around those rules:

| | `cmake --workflow --preset release-<platform>` | `cmake --install <build-dir>` |
|---|---|---|
| **Purpose** | Cut a release artifact | Install locally for GUI development |
| **Output** | Versioned archive in `dist/` (`.zip` / `.tar.gz`) | Loose directory tree in `install/<preset>/` |
| **Configure** | Runs its own clean configure (`*-release` preset, **tests OFF**) | Reuses your existing build tree |
| **Build** | Builds as part of the workflow | You must `cmake --build` first |
| **Packaging** | Yes — runs CPack | No — just copies the install output |
| **When to use** | Publishing to GitHub Releases | Day-to-day: point the Qt GUI at `install/<preset>/` |

In short: `--install` gives you a folder; `--workflow` builds from scratch (no tests) and additionally zips that same folder.

### Option A — release archive (`--workflow`)

Configure + build (no tests) + package, in a single command. Produces a versioned archive ready to attach to a GitHub Release.

```powershell
# Windows MSVC
cmake --workflow --preset release-windows-msvc
# → dist/platemaker-dev-0.1.1-windows-msvc.zip

# Windows MinGW
cmake --workflow --preset release-windows-mingw
# → dist/platemaker-dev-0.1.1-windows-mingw.zip
```

```bash
# Linux
cmake --workflow --preset release-linux
# → dist/platemaker-dev-0.1.1-linux-x86_64.tar.gz
```

### Option B — local install (`cmake --install`)

Installs an already-built tree to `install/<preset>/` for direct use while developing the Qt GUI. **Configure and build first, then install** — three commands:

```powershell
# Windows MSVC  — multi-config generator → --config Release is REQUIRED
cmake --preset windows-msvc
cmake --build --preset windows-msvc
cmake --install build/windows-msvc --config Release
# → install/windows-msvc/

# Windows MinGW — single-config Ninja → no --config (the windows-mingw preset is Debug)
cmake --preset windows-mingw
cmake --build --preset windows-mingw
cmake --install build/windows-mingw
# → install/windows-mingw/
```

```bash
# Linux — single-config Ninja; note the build dir lives under $HOME, not ./build
cmake --preset linux-system
cmake --build --preset linux-system
cmake --install ~/build/platemaker/linux-system
# → install/linux-system/
```

> **About `--config Release`:** it matters **only for multi-config generators** (Visual Studio / MSVC), where one build tree holds both Debug and Release and you must say which to install. Single-config generators (Ninja — used by MinGW and Linux) bake the build type in at configure time, so `--config` is silently ignored there.
>
> The `windows-mingw` preset is **Debug** by default (`windows-msvc` and `linux-system` are already Release). For an optimised local MinGW install, configure with the release preset and install from its build dir instead:
> ```powershell
> cmake --preset windows-mingw-release
> cmake --build --preset windows-mingw-release
> cmake --install build/windows-mingw-release
> # → install/windows-mingw-release/
> ```

### Install layout

```
install/<preset>/
  bin/
    libplatemaker.dll         Windows runtime (MinGW)
    platemaker.dll            Windows runtime (MSVC)
    libvips*.dll  …           runtime DLLs (MSVC: full vips package; MinGW: pruned to libvips' actual dependency closure)
  lib/
    libplatemaker.dll.a       MinGW import library
    platemaker.lib            MSVC import library
    libplatemaker.so          Linux shared library (+ .so.0 / .so.0.1.1 symlinks)
  lib/cmake/platemaker/
    platemaker-config.cmake
    platemaker-config-version.cmake
    platemaker-targets.cmake
  include/platemaker/         all public headers
```

---

## Consuming libplatemaker in your CMake project

The recommended pattern tries a local dev package first, then falls back to an automatic download from GitHub Releases.  Add the block below to your `CMakeLists.txt`, then point CMake at your local install via the `PLATEMAKER_DIR` cache variable — no paths hardcoded in source.

```cmake
# ─── platemaker ────────────────────────────────────────────────────────────────
# Discovery order:
#   1. PLATEMAKER_DIR cache variable  — explicit local dev package (set once, cached)
#   2. find_package                   — system install or CMAKE_PREFIX_PATH
#   3. FetchContent                   — downloads pre-built package from GitHub Releases
# ───────────────────────────────────────────────────────────────────────────────
set(PLATEMAKER_VERSION "0.1.1")
set(PLATEMAKER_DIR "" CACHE PATH "Path to a local platemaker dev package")

if(PLATEMAKER_DIR)
    list(PREPEND CMAKE_PREFIX_PATH "${PLATEMAKER_DIR}")
endif()

find_package(platemaker CONFIG QUIET)

if(NOT platemaker_FOUND)
    include(FetchContent)

    if(WIN32)
        set(_pm_archive "platemaker-dev-${PLATEMAKER_VERSION}-Windows-AMD64.zip")
    else()
        set(_pm_archive "platemaker-dev-${PLATEMAKER_VERSION}-Linux-x86_64.tar.gz")
    endif()

    FetchContent_Declare(
        platemaker_pkg
        URL "https://github.com/ShadobaDev/PlateMaker/releases/download/v${PLATEMAKER_VERSION}/${_pm_archive}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(platemaker_pkg)
    FetchContent_GetProperties(platemaker_pkg SOURCE_DIR _pm_src)
    file(GLOB _pm_dir LIST_DIRECTORIES true "${_pm_src}/platemaker-dev-*")
    list(GET _pm_dir 0 _pm_dir)
    list(PREPEND CMAKE_PREFIX_PATH "${_pm_dir}")
    find_package(platemaker CONFIG REQUIRED)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE Platemaker::platemaker)

# Windows: copy platemaker.dll and all libvips runtime DLLs next to the executable
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Platemaker::platemaker>"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying platemaker runtime DLLs"
        VERBATIM
    )
endif()
```

### Pointing to a local dev package

Set `PLATEMAKER_DIR` once on the command line — CMake caches it for subsequent configures:

```powershell
# Windows
cmake -B build -DPLATEMAKER_DIR="C:/path/to/platemaker-dev-0.1.1-Windows-AMD64"
```

```bash
# Linux
cmake -B build -DPLATEMAKER_DIR="/path/to/platemaker-dev-0.1.1-Linux-x86_64"
```

If `PLATEMAKER_DIR` is not set and platemaker is not found on the system, CMake automatically downloads the correct release archive from GitHub Releases during configure.

### Linux: runtime library path

The library has an embedded RPATH of `$ORIGIN/../lib`, so executables installed alongside it find `libplatemaker.so` automatically.  During development (running from the build tree without installing), point the loader at the package manually:

```bash
export LD_LIBRARY_PATH="/path/to/platemaker-dev-0.1.1-linux-x86_64/lib:$LD_LIBRARY_PATH"
./my-app
```

---

## Build presets reference

| Preset | Platform | Compiler | Notes |
|---|---|---|---|
| `linux-system` | Linux / WSL2 | GCC | libvips from apt; others auto-fetched |
| `linux-system-debug` | Linux / WSL2 | GCC | + ASan/UBSan |
| `windows-msvc` | Windows | MSVC | all deps auto-fetched |
| `windows-msvc-debug` | Windows | MSVC | debug symbols |
| `windows-mingw` | Windows | MinGW GCC | libvips from MSYS2; others auto-fetched |

| Workflow preset | Steps |
|---|---|
| `release-windows-msvc` | configure → build → CPack ZIP |
| `release-windows-mingw` | configure → build → CPack ZIP |
| `release-linux` | configure → build → CPack TGZ |

---

## Third-party dependencies

| Library | Licence | Linux (apt) | Windows |
|---|---|---|---|
| [libvips](https://www.libvips.org/) | LGPL 2.1 | `libvips-dev` | auto-fetched (MSVC) / MSYS2 pacman (MinGW) |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | `nlohmann-json3-dev` (optional) | auto-fetched |
| [GoogleTest](https://github.com/google/googletest) | BSD 3-Clause | `libgtest-dev` (optional) | auto-fetched |
| [pytest](https://pytest.org/) | MIT | `pip install pytest` | `pip install pytest` |

`libplatemaker` is distributed as a **shared library** — LGPL 3.0 requires that end users be able to relink against a modified version.

---

## Coding style

See **[docs/CODING_STYLE.md](docs/CODING_STYLE.md)** for naming conventions, Doxygen comment format, namespace-to-directory mapping rules, and file layout requirements.
