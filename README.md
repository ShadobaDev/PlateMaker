# Platemaker

> **Comic artist canvas tool** — pre-processing and post-processing for Webtoon-style publishing.

Platemaker handles both ends of the comic-art workflow: generating canvas templates with margin guides before drawing, and scaling + slicing finished artwork into upload-ready panels. It is designed around the Webtoon vertical-scroll format (800 px wide, 1280 px-high slices) but is architecturally extensible to other formats.

For full requirements, data models, pipeline details, and the development roadmap see **[docs/SPECIFICATION.md](docs/SPECIFICATION.md)**.

---

## Project structure

```
platemaker/
├── CMakeLists.txt           # Root — finds dependencies, adds sub-projects
├── CMakePresets.json        # Presets: linux-system, linux-gcc, windows-msvc, …
├── vcpkg.json               # vcpkg manifest — Windows deps; Linux uses apt
├── lib/                     # libplatemaker — Core + Infrastructure (zero Qt in CLI builds)
│   ├── include/platemaker/
│   │   ├── models/          # Shared data types (CanvasProfile, Workspace, …)
│   │   ├── core/            # Pipeline components (Scaler, ScaledStrip, Slicer, …)
│   │   └── infrastructure/  # IO + caching (ImageIO, WorkspaceSerializer, ThumbnailCache)
│   └── src/                 # Implementation files (mirrors include/ structure)
├── cli/                     # platemaker binary — links libplatemaker, no Qt
├── gui/                     # platemaker-gui binary — links libplatemaker + Qt 6
│   └── panels/              # ToolPanel subclasses (one file per tab)
├── tests/                   # Unit tests (GTest, mock ImageIO)
└── docs/
    ├── SPECIFICATION.md
    └── CODING_STYLE.md
```

---

## Development environment

### Prerequisites (all platforms)

| Tool | Minimum version | Notes |
|---|---|---|
| CMake | 3.21 | Build system; presets file uses format version 3 |
| Ninja | any | Generator used by all Linux presets |
| C++ compiler | GCC 11 / MSVC 17 | C++20 support required |

---

### Linux

Tested on **Ubuntu 22.04 LTS** (native and inside WSL 2).  
Two paths are available: **system packages** (fast, recommended for daily dev) and **vcpkg** (fully reproducible, required for CI).

---

#### Path A — System packages `linux-system` *(recommended for development)*

No vcpkg installation needed.  All dependencies come from the Ubuntu package manager.

**Step 1 — Build toolchain**

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git
```

**Step 2 — C++ dependencies**

```bash
sudo apt install -y \
    libvips-dev \
    nlohmann-json3-dev \
    qt6-base-dev \
    qt6-base-dev-tools \
    libgl-dev \
    libgles-dev
```

Package notes:
- `libvips-dev` — image processing (libvips 8.12 on Ubuntu 22.04)
- `nlohmann-json3-dev` — header-only JSON library (3.10 on Ubuntu 22.04)
- `qt6-base-dev` + `qt6-base-dev-tools` — Qt 6 Core / Gui / Widgets / Concurrent (6.2.4 on Ubuntu 22.04)
- `libgl-dev` + `libgles-dev` — OpenGL headers required by Qt 6 Gui

**Step 3 — Configure and build**

```bash
# Full build: libplatemaker + CLI + Qt GUI
cmake --preset linux-system
cmake --build --preset linux-system

# Debug build with AddressSanitizer + UBSan
cmake --preset linux-system-debug
cmake --build --preset linux-system-debug

# Headless build (no Qt): libplatemaker + CLI only
cmake --preset linux-system-cli
cmake --build --preset linux-system-cli
```

Build artefacts are written to `build/linux-system/bin/` and `build/linux-system/lib/`.

**Run tests:**
```bash
ctest --preset linux-system
```

---

#### Path B — vcpkg `linux-gcc` *(CI / reproducible builds)*

Uses vcpkg to manage nlohmann/json; libvips and Qt 6 still come from apt (vcpkg
manifest marks them as Windows-only so they are never built from source on Linux).

**Step 1 — Build toolchain** *(same as Path A Step 1)*

**Step 2 — libvips + Qt system packages** *(same as Path A Step 2)*

**Step 3 — vcpkg**

```bash
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
```

Add to `~/.bashrc` (or `~/.zshrc`):

```bash
export VCPKG_ROOT="$HOME/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"
```

```bash
source ~/.bashrc   # reload shell
```

**Step 4 — Configure and build**

```bash
cmake --preset linux-gcc
cmake --build --preset linux-gcc

# Debug with AddressSanitizer + UBSan
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug

# CLI only (no Qt)
cmake --preset linux-cli-only
cmake --build --preset linux-cli-only
```

**Run tests:**
```bash
ctest --preset linux-gcc
```

---

#### VS Code IntelliSense (Linux)

After the first successful configure, `compile_commands.json` is generated at
`build/linux-system/compile_commands.json` (or `build/linux-gcc/…` depending on preset).
The VS Code C/C++ extension picks this up automatically.

Run **CMake: Select Configure Preset** from the Command Palette to switch presets
inside the editor without touching the terminal.

---

### Windows

Tested on **Windows 10 22H2** and **Windows 11** with **Visual Studio 2022**.  
All C++ dependencies (libvips, nlohmann/json, Qt 6) are built by vcpkg on first configure.

#### Step 1 — Prerequisites

1. [Visual Studio 2022](https://visualstudio.microsoft.com/) — select the
   **"Desktop development with C++"** workload.  MSVC, CMake, and Ninja are
   bundled with this workload.

2. [Git for Windows](https://git-scm.com/download/win)

#### Step 2 — vcpkg

Open **Developer PowerShell for VS 2022**:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

Set a permanent user environment variable (PowerShell as administrator, or via
*System Properties → Advanced → Environment Variables*):

```powershell
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
```

Restart the terminal / VS Code after setting the variable.

#### Step 3 — Configure and build

**Expect the first configure to take 30–60 minutes** on a fast machine (vcpkg builds
libvips + Qt 6 from source).  Subsequent builds are fast and incremental.

Open **Developer PowerShell for VS 2022**, then:

```powershell
cd C:\path\to\PlateMaker

# Release build
cmake --preset windows-msvc
cmake --build --preset windows-msvc

# Debug build
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

Build artefacts: `build\windows-msvc\bin\` and `build\windows-msvc\lib\`.

**Run tests:**
```powershell
ctest --preset windows-msvc
```

#### Step 4 — VS Code (Remote-WSL)

Keep the source tree on the **Windows filesystem** (`C:\`) so that both the MSVC
build and the WSL 2 GCC build share the same files without copying.  Open the
folder in VS Code via **Remote-WSL** (`code .` from inside WSL, or *File → Open Folder*
from the Remote-WSL window pointing to the Windows path).

CMake Tools preset recommendation: **"Linux · GCC · Debug · system packages"**
(`linux-system-debug`) for daily WSL development; switch to **"Windows · MSVC · Release"**
(`windows-msvc`) for release verification.

---

## Build presets summary

| Preset name | Platform | vcpkg? | Qt? | Use case |
|---|---|---|---|---|
| `linux-system` | Linux / WSL2 | ❌ | ✅ | **Recommended daily dev** — system packages |
| `linux-system-debug` | Linux / WSL2 | ❌ | ✅ | Debug + AddressSanitizer / UBSan |
| `linux-system-cli` | Linux / WSL2 | ❌ | ❌ | Headless build, no Qt required |
| `linux-gcc` | Linux / WSL2 | ✅ | ✅ | Reproducible build via vcpkg (CI) |
| `linux-gcc-debug` | Linux / WSL2 | ✅ | ✅ | vcpkg debug + ASan |
| `linux-cli-only` | Linux / WSL2 | ✅ | ❌ | vcpkg headless / no Qt |
| `windows-msvc` | Windows | ✅ | ✅ | Primary Windows release build |
| `windows-msvc-debug` | Windows | ✅ | ✅ | Windows debug |

> All Linux presets use **Ninja** as the generator.  
> Windows presets use **Visual Studio 17 2022** (MSBuild multi-config).

---

## Third-party dependencies

| Library | Licence | Linux (apt) | Windows (vcpkg) |
|---|---|---|---|
| [libvips](https://www.libvips.org/) | LGPL 2.1 | `libvips-dev` | `vips[cpp]` |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | `nlohmann-json3-dev` | `nlohmann-json` |
| [Qt 6](https://www.qt.io/) | LGPL 3 | `qt6-base-dev`, `qt6-base-dev-tools`, `libgl-dev`, `libgles-dev` | `qtbase[concurrent,gui,widgets]`, `qtconcurrent` |

Qt must be **dynamically linked** in all builds to comply with LGPL terms (see
[SPECIFICATION.md §1](docs/SPECIFICATION.md) for the licensing rationale).

---

## Coding style

See **[docs/CODING_STYLE.md](docs/CODING_STYLE.md)** for naming conventions, Doxygen
comment format, namespace-to-directory mapping rules, and file layout requirements.
