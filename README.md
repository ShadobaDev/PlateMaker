# Platemaker

> **Comic artist canvas tool** — pre-processing and post-processing for Webtoon-style publishing.

Platemaker handles both ends of the comic-art workflow: generating canvas templates with margin guides before drawing, and scaling + slicing finished artwork into upload-ready panels. It is designed around the Webtoon vertical-scroll format (800 px wide, 1280 px-high slices) but is architecturally extensible to other formats.

For full requirements, data models, pipeline details, and the development roadmap see **[docs/SPECIFICATION.md](docs/SPECIFICATION.md)**.

---

## Project structure

```
platemaker/
├── CMakeLists.txt           # Root — finds dependencies, adds sub-projects
├── CMakePresets.json        # Presets: windows-msvc, linux-gcc, linux-cli-only, *-debug
├── vcpkg.json               # vcpkg manifest — all third-party dependencies
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

| Tool | Minimum version | Purpose |
|---|---|---|
| CMake | 3.21 | Build system |
| vcpkg | latest | Package manager (dependency download + build) |
| C++ compiler | see per-platform | C++20 support required |

**vcpkg must be installed before running any CMake configure step.**
All C++ dependencies (libvips, nlohmann/json, Qt 6) are fetched and built automatically
by vcpkg in manifest mode — no manual library installation is needed beyond the
prerequisites listed below.

---

### Linux

Tested on **Ubuntu 22.04 / 24.04** and **Debian 12** (native and inside WSL 2).

#### 1 — System build tools

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    curl \
    zip \
    unzip \
    tar
```

#### 2 — libvips system development headers

Building libvips from source via vcpkg requires a large set of native libraries.
The fastest approach during development is to install the system package and let
vcpkg pick it up through pkg-config:

```bash
sudo apt install -y libvips-dev
```

> **Alternative — build libvips via vcpkg (fully reproducible, slower first build):**
> Skip the `apt install libvips-dev` step.  vcpkg will build libvips from source the
> first time `cmake --preset linux-gcc` runs.  This takes ~5 min on a modern machine
> but only happens once; subsequent builds use the cached build.

#### 3 — Qt 6 system development headers

Similarly, Qt 6 is large and builds faster from the system package manager:

```bash
sudo apt install -y \
    qt6-base-dev \
    libqt6concurrent6 \
    qt6-base-dev-tools
```

> **Alternative — build Qt 6 via vcpkg:** remove the apt step above; vcpkg will build
> Qt from source (~20–30 min on first configure).

#### 4 — vcpkg

```bash
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
```

Add to your shell profile (`~/.bashrc` or `~/.zshrc`):

```bash
export VCPKG_ROOT="$HOME/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"
```

Reload your shell:
```bash
source ~/.bashrc   # or: source ~/.zshrc
```

#### 5 — Configure and build

**Full build (libplatemaker + CLI + Qt GUI):**
```bash
cmake --preset linux-gcc
cmake --build --preset linux-gcc
```

**Debug build with AddressSanitizer:**
```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
```

**CLI-only build (no Qt dependency at all):**
```bash
cmake --preset linux-cli-only
cmake --build --preset linux-cli-only
```

**Run tests:**
```bash
ctest --preset linux-gcc
```

Build artefacts are written to `build/linux-gcc/bin/` and `build/linux-gcc/lib/`.

#### 6 — VS Code IntelliSense

After the first successful configure step, `compile_commands.json` is generated at
`build/linux-gcc/compile_commands.json`.  VS Code C/C++ extension picks this up
automatically via the `cmake.buildDirectory` setting.  All "cannot open source file"
squiggles in the editor will disappear after configure.

You can also run the **CMake: Select Configure Preset** command from the VS Code
Command Palette to trigger configure from inside the editor.

---

### Windows

Tested on **Windows 10 22H2** and **Windows 11** with **Visual Studio 2022**.

#### 1 — Prerequisites

Install the following in order:

1. [Visual Studio 2022](https://visualstudio.microsoft.com/) — select the
   **"Desktop development with C++"** workload.  The MSVC toolchain, CMake, and
   Ninja are bundled with this workload.

2. [Git for Windows](https://git-scm.com/download/win) — needed to clone vcpkg.

#### 2 — vcpkg

Open **Developer PowerShell for VS 2022** (or any PowerShell with VS env loaded):

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

Set a **permanent user environment variable** (run once in PowerShell as administrator,
or set via *System Properties → Advanced → Environment Variables*):

```powershell
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
```

Restart your terminal / VS Code after setting the variable.

#### 3 — Configure and build

All dependencies (libvips, nlohmann/json, Qt 6) are fetched and built automatically
by vcpkg on the first configure run.  **Expect the first configure to take 30–60 min**
on a fast machine; subsequent builds are incremental and fast.

**Open a Developer PowerShell for VS 2022**, then:

```powershell
cd C:\path\to\PlateMaker

# Release build
cmake --preset windows-msvc
cmake --build --preset windows-msvc

# Debug build
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

Build artefacts are written to `build\windows-msvc\bin\` and `build\windows-msvc\lib\`.

**Run tests:**
```powershell
ctest --preset windows-msvc
```

#### 4 — VS Code (Remote-WSL)

The source tree lives on the **Windows filesystem** (`C:\`) so that both the native
Windows MSVC build and the WSL2 GCC build share the same files without copying.
Open the folder in VS Code using **Remote-WSL** (`code .` from inside WSL, or
*File → Open Folder* from the Remote-WSL window pointing to the Windows path).

CMake Tools extension preset: select **"Linux · GCC · Debug"** for day-to-day
development inside WSL, and switch to **"Windows · MSVC · Release"** for release
verification.

---

## Build presets summary

| Preset name | Platform | Compiler | Qt | Use case |
|---|---|---|---|---|
| `linux-gcc` | Linux / WSL2 | GCC | Yes | Standard development build |
| `linux-gcc-debug` | Linux / WSL2 | GCC | Yes | Debug with AddressSanitizer + UBSan |
| `linux-cli-only` | Linux / WSL2 | GCC | **No** | Headless server / CI / no-Qt check |
| `windows-msvc` | Windows | MSVC | Yes | Release verification |
| `windows-msvc-debug` | Windows | MSVC | Yes | Windows debug build |

---

## Third-party dependencies

| Library | Licence | How obtained |
|---|---|---|
| [libvips](https://www.libvips.org/) | LGPL 2.1 | vcpkg (`vips[cpp]`) or `apt install libvips-dev` |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | vcpkg (`nlohmann-json`) |
| [Qt 6](https://www.qt.io/) | LGPL 3 | vcpkg (`qtbase`, `qtconcurrent`) or `apt install qt6-base-dev` |

Qt must be **dynamically linked** in all builds to comply with LGPL terms (see
[SPECIFICATION.md §1](docs/SPECIFICATION.md) for the licensing rationale).

---

## Coding style

See **[docs/CODING_STYLE.md](docs/CODING_STYLE.md)** for naming conventions, Doxygen
comment format, namespace-to-directory mapping rules, and file layout requirements.
