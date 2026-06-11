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

### Option A — ZIP archive (releases)

Produces a versioned archive ready to attach to a GitHub Release.

```powershell
# Windows MSVC — configure + build + package in one command
cmake --workflow --preset release-windows-msvc
# → build/windows-msvc/platemaker-dev-0.1.1-Windows-AMD64.zip

# Windows MinGW
cmake --workflow --preset release-windows-mingw
# → build/windows-mingw/platemaker-dev-0.1.1-Windows-AMD64.zip
```

```bash
# Linux
cmake --workflow --preset release-linux
# → build/linux-system/platemaker-dev-0.1.1-Linux-x86_64.tar.gz
```

### Option B — cmake --install (local development)

Installs to `install/<preset>/` for direct use during active development of the Qt GUI.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc
cmake --install build/windows-msvc --config Release
# → install/windows-msvc/
```

### Install layout

```
install/<preset>/
  bin/
    platemaker.dll            Windows runtime
    libvips*.dll              runtime DLLs (MSVC: vips package; MinGW: MSYS2 mingw64/bin/)
  lib/
    platemaker.lib            Windows import lib
    libplatemaker.so.1        Linux (SOVERSION symlink)
  lib/cmake/platemaker/
    platemaker-config.cmake
    platemaker-config-version.cmake
    platemaker-targets.cmake
  include/platemaker/         all public headers
```

---

## Consuming libplatemaker in your CMake project

Two integration paths are available: automatic download via FetchContent, or pointing CMake at a locally extracted archive.

---

### Windows

#### Option A — FetchContent (automatic download)

CMake downloads and extracts the release archive during configure — no manual step required.

```cmake
cmake_minimum_required(VERSION 3.21)
project(my-app)

include(FetchContent)

set(PLATEMAKER_VERSION "0.1.1")   # ← set to the desired release version

FetchContent_Declare(
    platemaker
    URL "https://github.com/ShadobaDev/Platemaker/releases/download/v${PLATEMAKER_VERSION}/platemaker-dev-${PLATEMAKER_VERSION}-Windows-AMD64.zip"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(platemaker)

# Locate the versioned subdirectory that the archive unpacks into
FetchContent_GetProperties(platemaker SOURCE_DIR _pm_src)
file(GLOB _pm_dir LIST_DIRECTORIES true "${_pm_src}/platemaker-dev-*")
list(GET _pm_dir 0 _pm_dir)
list(APPEND CMAKE_PREFIX_PATH "${_pm_dir}")

find_package(platemaker CONFIG REQUIRED)
```

#### Option B — local dev package

Download and extract `platemaker-dev-X.Y.Z-Windows-AMD64.zip` anywhere, then point CMake at it:

```cmake
cmake_minimum_required(VERSION 3.21)
project(my-app)

list(APPEND CMAKE_PREFIX_PATH "C:/path/to/platemaker-dev-0.1.1-Windows-AMD64")

find_package(platemaker CONFIG REQUIRED)
```

Or pass it on the command line without touching `CMakeLists.txt`:

```powershell
cmake -B build -DCMAKE_PREFIX_PATH="C:/path/to/platemaker-dev-0.1.1-Windows-AMD64"
```

---

### Linux

#### Option A — FetchContent (automatic download)

```cmake
cmake_minimum_required(VERSION 3.21)
project(my-app)

include(FetchContent)

set(PLATEMAKER_VERSION "0.1.1")   # ← set to the desired release version

FetchContent_Declare(
    platemaker
    URL "https://github.com/ShadobaDev/Platemaker/releases/download/v${PLATEMAKER_VERSION}/platemaker-dev-${PLATEMAKER_VERSION}-Linux-x86_64.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(platemaker)

FetchContent_GetProperties(platemaker SOURCE_DIR _pm_src)
file(GLOB _pm_dir LIST_DIRECTORIES true "${_pm_src}/platemaker-dev-*")
list(GET _pm_dir 0 _pm_dir)
list(APPEND CMAKE_PREFIX_PATH "${_pm_dir}")

find_package(platemaker CONFIG REQUIRED)
```

#### Option B — local dev package

```cmake
cmake_minimum_required(VERSION 3.21)
project(my-app)

list(APPEND CMAKE_PREFIX_PATH "/path/to/platemaker-dev-0.1.1-Linux-x86_64")

find_package(platemaker CONFIG REQUIRED)
```

Or on the command line:

```bash
cmake -B build -DCMAKE_PREFIX_PATH="/path/to/platemaker-dev-0.1.1-Linux-x86_64"
```

---

### Linking

Once `find_package` succeeds, link against the namespaced target — identical on all platforms:

```cmake
add_executable(my-app main.cpp)

target_link_libraries(my-app PRIVATE Platemaker::platemaker)
```

---

### Windows: copying runtime DLLs

`platemaker.dll` and its libvips dependencies must sit next to your executable at runtime.  The snippet below copies them automatically after each build:

```cmake
if(WIN32)
    # $<TARGET_FILE_DIR:Platemaker::platemaker> resolves to the package bin/ directory,
    # which contains platemaker.dll and all libvips runtime DLLs.
    add_custom_command(TARGET my-app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_FILE_DIR:Platemaker::platemaker>"
            "$<TARGET_FILE_DIR:my-app>"
        COMMENT "Copying platemaker runtime DLLs"
        VERBATIM
    )
endif()
```

---

### Linux: runtime library path

On Linux the package's library has an embedded RPATH of `$ORIGIN/../lib`, so executables installed alongside it find `libplatemaker.so` automatically.  During **development** (running from the build tree without installing), point the loader at the package manually:

```bash
export LD_LIBRARY_PATH="/path/to/platemaker-dev-0.1.1-Linux-x86_64/lib:$LD_LIBRARY_PATH"
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
