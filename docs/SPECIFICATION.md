# Platemaker — Project Specification

**Status:** Architecture planning phase — no code written yet.  
**Last updated:** 2026-05-31  
**Audience:** Human developer + AI coding assistant (GitHub Copilot, Cline, Claude, etc.)

> **Predecessor:** Clip2l (Python/Tkinter prototype) — https://github.com/ShadobaDev/Clip2l  
> This document supersedes the prototype. Where behaviour differs, this spec takes precedence.

---

## 1. Project Overview

**Platemaker** is a cross-platform desktop application (with CLI) for comic artists. It handles both pre-processing (generating canvas templates with margin guides) and post-processing (scaling and slicing finished artwork for distribution platforms).

The name draws from the printing trade: a *platemaker* crafts the printing plates from which pages are born. Platemaker does the same — it shapes canvases before drawing and prepares finished pages for publication.

**Primary use case (v1):** Processing comic pages for Webtoon-style vertical scroll format. The application treats an ordered set of source images as a single virtual strip, scales them to a target width, and slices the strip into fixed-height output panels.

**Future scope (not in current requirements):** Template generation and margin-aware processing for traditional comic formats — American (6.625" × 10.25"), European (various), manga (B4/A4). The architecture is designed to support multiple `CanvasProfile` presets without code changes.

The project is **open-source** (license TBD, likely MIT or GPL). Monetization via donations may be added in the future — this is why Qt's LGPL terms were chosen deliberately (LGPL permits monetization as long as Qt itself remains dynamically linked and unmodified).

---

## 2. Core Use Case — The Virtual Strip Model

The artist works on one or more canvases in Procreate and exports them as PNG files. These files arrive at Platemaker pre-sliced at arbitrary heights (e.g. 2560 px, 2000 px, 1800 px) — they are already "pieces of a strip" from the artist's perspective.

**Platemaker treats the entire ordered file list as a single continuous virtual strip.** Slice boundaries in the output are determined solely by the target slice height and are completely independent of source file boundaries.

**Pipeline overview:**
1. Load an ordered list of image files (directory or explicit list).
2. Allow reordering of pages (drag & drop or manual sort).
3. Scale each file to the **target width** (Webtoon standard: **800 px**), preserving aspect ratio.
4. Concatenate scaled images conceptually into one virtual strip.
5. Slice the strip at every N pixels (Webtoon standard: **1280 px**).
6. Export output slices as sequentially numbered PNG files.
7. One tail slice at the end (shorter than sliceHeight) — handled per LastSlicePolicy.

**Goal: minimise the number of output files.** Webtoon does not respect upload order reliably, so fewer files = less manual reordering after upload.

> **Note:** "Sequence mode" in the Clip2l prototype (`--sequence` / `-s` flag) corresponds to this pipeline. In Platemaker this is the only and default mode — there is no per-file-independent processing mode.

**Canvas math example:**
```
Input files (after scaling to 800 px wide):
  page_01.png  →  800 × 5120 px
  page_02.png  →  800 × 4000 px
  page_03.png  →  800 × 3600 px

Virtual strip total height:  12720 px
Slices at 1280 px:           9 full slices (11520 px) + tail (1200 px)
Output files:                output_001.png … output_010.png

output_003.png may contain the bottom N px of page_01 and top (1280-N) px of page_02.
Slice boundaries never need to align with source file boundaries.
```

---

## 3. New Features (Planned)

### 3.1 Canvas Template Generator

Artists want to work on a larger canvas with visible margin guides so they can safely import layout templates into Procreate before drawing.

The application should generate a PNG template image that shows:
- The full canvas area (with margins included)
- Margin zones highlighted in a configurable colour (e.g. pink/magenta)
- Safe-area boundary lines
- Slice cut lines at correct vertical intervals

**Workflow:**
1. Artist defines a `CanvasProfile` (see §6) with canvas size, margin sizes, and visual colour.
2. App generates a template PNG → artist imports it into Procreate as a reference layer.
3. Artist draws within the safe area on top of the template.

### 3.2 Margin-Aware Import Pipeline

When the artist exports the finished work, the app performs an extended pipeline:

```
Load source image
  → Crop margins (using stored CanvasProfile margins)
  → Scale to target width
  → [feed into virtual strip as normal]
```

The margin crop step is deterministic and driven entirely by the saved `CanvasProfile` — no manual input required at export time.

---

## 4. Architecture

### 4.1 Layer Diagram

```
┌──────────────────────────────────────────────┐
│              Qt 6 GUI (optional)             │  Tabs/panels, preview, drag&drop
├──────────────────────────────────────────────┤
│        CLI binary  (`platemaker`)            │  Standalone tool; future web backend
├──────────────────────────────────────────────┤
│      Core Library  (`libplatemaker`)         │  Pure C++, zero Qt dependency
│  Scaler │ Strip │ Slicer │ Cropper │ TplGen  │
├──────────────────────────────────────────────┤
│       Infrastructure / IO                    │  WorkspaceSerializer, ImageIO, Cache
├──────────────────────────────────────────────┤
│   libvips  │  nlohmann/json  │  std::        │  Third-party dependencies
└──────────────────────────────────────────────┘
```

### 4.2 Key Architectural Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Language | C++ 17/20 | Developer is expert; best performance for image processing |
| GUI framework | Qt 6 (LGPL) | Cross-platform, mature, GPU-backed rendering |
| Image processing | libvips | Multi-threaded, lazy/streaming pipeline, Lanczos3 resampling, low RAM |
| JSON serialisation | nlohmann/json | Header-only, MIT licence, clean API |
| Build system | CMake + CMakePresets.json | Standard for C++ cross-platform; preset per platform |
| Package manager | vcpkg (manifest mode) | vcpkg.json in repo; reproducible on every machine |
| File integrity | SHA-256 | Per-input-file hashing for incremental processing |

### 4.3 Critical Design Rules

> **`libplatemaker` must have zero Qt dependencies.**

This keeps the Core testable without a display, usable from CLI without Qt, and portable to future backends (web server, Python bindings, etc.). Qt types (`QImage`, `QPixmap`, etc.) must only appear in the GUI layer.

> **The strip must never be fully materialised in RAM.**

`ScaledStrip` processes files one at a time and keeps in memory only the minimum number of scaled images needed to complete the current output slice. For example: if a slice boundary falls mid-file, two files may be in memory simultaneously (tail of file N + head of file N+1). As soon as a file's contribution to all remaining slices is exhausted, it is released. This is critical for large projects where the total input may be several gigabytes — the file list and thumbnails are metadata only; raw pixel data is loaded on demand.

### 4.4 CLI as First-Class Citizen

CLI is not a wrapper added after the fact — it is a primary interface and the intended backend for a future web application. A web server can call `platemaker process --json` as a subprocess and parse its stdout. The layer hierarchy is:

```
libplatemaker  (static library, pure C++)
  ├── platemaker      (CLI binary — links libplatemaker, no Qt)
  └── platemaker-gui  (Qt GUI binary — links libplatemaker + Qt)
```

---

## 5. Component Descriptions

### 5.1 Core Library — `libplatemaker`

#### `Scaler`
- Input: file path + target width (px)
- Output: `ScaledImage` (pixel buffer + original file path for provenance)
- Algorithm: **Lanczos3** via `vips_thumbnail()` — aspect-preserving, anti-aliased
- Stateless and thread-safe
- Uses libvips sequential access — does not load the full source image to RAM

#### `ScaledStrip`
- Abstraction of the virtual tape
- Accepts `ScaledImage` objects appended one at a time via `append()`
- **Memory policy:** retains only the minimum number of scaled images required to assemble the next output slice. Once a source image's pixels are no longer needed by any future slice, it is freed immediately. At most 2 source images are in memory simultaneously (when a slice boundary splits across a file boundary).
- Tracks `SourceMap` — which input file and Y-range contributed to each output slice

```cpp
class ScaledStrip {
public:
    void append(ScaledImage image);
    int totalHeight() const;
    std::vector<SliceResult> sliceAll(int sliceHeight, LastSlicePolicy policy);
};
```

#### `SliceResult`
```cpp
struct SourceSegment {
    std::string inputFilePath;
    int srcY;       // Y offset within the scaled source image
    int height;     // number of px taken from this source
};

struct SliceResult {
    PixelBuffer image;
    int index;                            // 0-based output index
    std::vector<SourceSegment> sourceMap; // provenance — used for incremental hashing
};
```

#### `Slicer`
- Operates on `ScaledStrip`, not on individual files
- Produces exactly `floor(totalHeight / sliceHeight)` full slices + 1 tail (if remainder > 0)
- LastSlicePolicy: `CROP` | `PAD_WHITE` | `KEEP_AS_IS`

#### `MarginCropper`
- Input: pixel buffer + `Margins` struct (top/right/bottom/left in px)
- Output: cropped pixel buffer (safe area only)
- Pure crop, no resampling — runs before Scaler in the margin-aware pipeline

#### `TemplateGenerator`
- Input: `CanvasProfile` + `OutputProfile`
- Output: PNG image file
- Renders: white background, semi-transparent margin zone overlay, safe-area border, horizontal slice lines
- Uses libvips draw operations (no Qt dependency)

### 5.2 Infrastructure

#### `ImageIO`
- Wraps libvips for reading/writing PNG, JPEG, TIFF
- Provides `PixelBuffer` type that abstracts `VipsImage*` with RAII
- Handles colour profile normalisation on load
- **Supported output formats** (carried over from Clip2l prototype):
  - PNG (default)
  - JPEG — with configurable quality (1–95, default 90), subsampling (4:4:4 / 4:2:2 / 4:2:0), optimize flag, progressive flag
  - WebP
- Per-file error handling: if a single input file fails to load, log the error and skip it rather than aborting the entire batch. Report skipped files in the final summary.

#### `WorkspaceSerializer`
- Reads and writes `Workspace` to/from JSON via nlohmann/json
- File includes a `"version"` field (start at `1`) for future migration
- On load: if version < current, run migration chain
- Persists `processedFiles` map (SHA-256 + sourceMap) for incremental processing

#### `ThumbnailCache`
- Generates small preview images (200 px wide) on demand
- Stored in `.platemaker-cache/` sibling to the `.platemaker.json` workspace file
- Safe to delete at any time — regenerated transparently on next access
- **Zero Qt dependency** — provides only synchronous, blocking methods

**Usage policy:** `ThumbnailCache` is **caller-driven** and is never invoked by the
CLI binary.  Thumbnails are a pure GUI concern.  When the Qt GUI needs to display
a thumbnail for a `PageItem` it calls `ThumbnailCache::getOrGenerate(page.filePath)`.
For non-blocking behaviour the GUI wraps that call in `QtConcurrent::run()` itself —
`libplatemaker` makes no threading decisions.

> **Design note:** `PageItem` intentionally does **not** carry a `thumbnailPath` field.
> The thumbnail path is deterministic (derived from `filePath` via a path digest) so
> it never needs to be persisted in the workspace JSON.  The GUI computes it on the
> fly with `ThumbnailCache::thumbnailPath(page.filePath)`.

### 5.3 CLI — `platemaker`

Standalone binary. All commands operate on a workspace JSON file.

```
platemaker [--help]
platemaker help
platemaker process   --workspace project.platemaker.json [--input ./pages] [--output ./out]
                     [--format png|jpg|webp] [--start-index 1] [--json]
platemaker template  --workspace project.platemaker.json --profile "webtoon-standard" --output template.png
platemaker workspace create --canvas 1600x10240 --margins 100,100,100,100 --output project.platemaker.json
platemaker workspace list-profiles --workspace project.platemaker.json
```

- Exit codes: `0` success, `1` usage error, `2` IO error, `3` processing error
- `--json` flag: machine-readable JSON summary to stdout (for web backend subprocess use)
- `--start-index N`: output numbering starts at N (default 1), e.g. `--start-index 5` → `output_005.png, output_006.png …`
- Human-readable progress always goes to stderr
- no options -> assume help

### 5.4 Qt GUI — `platemaker-gui`

#### Tab Structure
```
[📁 Project]  [🖼 Canvas Profiles]  [⚙ Output Settings]  [▶ Process]
```

**Project tab**
- File list with thumbnails (`QListWidget`, drag & drop reordering)
- "Open folder" / "Add files" / "Remove" buttons
- Thumbnails generated asynchronously (no UI block on large directories)
- **Sort options** (from Clip2l prototype): by filename, by modified date, by created date
- **Reverse order** button
- Files loaded lazily — adding a directory enqueues paths immediately; thumbnails generate in background

**Canvas Profiles tab**
- List of saved `CanvasProfile` entries
- Create / Edit / Delete form (canvas size, margins, safe area preview, colour picker)
- "Generate Template PNG" button

**Output Settings tab**
- Active canvas profile selector
- Active output profile selector (target width, slice height, output format)
- JPEG options sub-panel (visible only when format = jpg): quality, subsampling, optimize, progressive
- Output directory picker
- Last-slice policy selector (Crop / Pad white / Keep)
- Start index spinner (default 1)

**Process tab**
- Summary: file count, expected output slice count
- "Run" button → `QThread` running `libplatemaker` pipeline
- "Cancel" button — properly implemented via `std::atomic<bool>` cancellation token passed to pipeline (unlike the Clip2l prototype where cancel was a stub)
- `QProgressBar` + log (`QPlainTextEdit`)
- On completion: output file count, skipped file count, "Open folder" button

#### Extensibility Pattern

```cpp
class ToolPanel : public QWidget {
public:
    virtual bool isReady() const = 0;
    virtual void onWorkspaceChanged(const Workspace&) = 0;
};
```

`MainWindow` holds a `QTabWidget` and a registry of `ToolPanel*`. Adding a new feature = new `ToolPanel` subclass registered in `MainWindow::initPanels()`. No other changes required.

---

## 6. Data Models

### `CanvasProfile`
```
name          : string    // e.g. "Webtoon 4-page" or "Marvel Standard" (future)
canvasSize    : Size      // full canvas including margins, e.g. 1600×10240
margins       : Margins   // top/right/bottom/left in px
safeArea      : Size      // computed: canvasSize minus margins (read-only)
visualColour  : RGBA      // template overlay colour, e.g. #FF69B4 at 50% alpha
```

### `OutputProfile`
```
name            : string      // e.g. "Webtoon Standard"
targetWidth     : int         // e.g. 800
sliceHeight     : int         // e.g. 1280
lastSlicePolicy : enum        // CROP | PAD_WHITE | KEEP_AS_IS
outputFormat    : enum        // PNG | JPEG | WEBP
jpegOptions     : JpegOptions // only used when outputFormat == JPEG
startIndex      : int         // output file numbering start, default 1
```

### `JpegOptions`
```
quality       : int   // 1–95, default 90
subsampling   : enum  // YUV_444 | YUV_422 | YUV_420
optimize      : bool  // default true
progressive   : bool  // default false
```

### `PageItem`
```
id           : uuid (string)
filePath     : string    // absolute path on disk
order        : int       // 0-indexed display order (not filesystem order)
status       : enum      // PENDING | PROCESSED | SKIPPED | ERROR
errorMessage : string    // empty unless status == ERROR or SKIPPED
```

> **Note:** `thumbnailPath` is not persisted. The GUI derives it on demand via
> `ThumbnailCache::thumbnailPath(page.filePath)` — the path is deterministic and
> does not need to be stored in the workspace JSON.

### `ProcessedFileRecord`
```
inputFilePath : string    // absolute path
sha256        : string    // hex digest at time of last processing
lastProcessed : datetime  // ISO 8601
contributesTo : string[]  // output filenames that contain pixels from this input
```

### `Workspace`
```
version                 : int                   // schema version, start at 1
canvasProfiles          : CanvasProfile[]
outputProfiles          : OutputProfile[]
activeCanvasProfileName : string
activeOutputProfileName : string
pages                   : PageItem[]
outputDirectory         : string
processedFiles          : ProcessedFileRecord[]  // incremental processing cache
stripDirty              : bool                   // true = full reprocess required
```

**Workspace file format:** UTF-8 JSON, saved as `<project-name>.platemaker.json`.

`stripDirty` is set to `true` whenever: page order changes, a page is added/removed, or OutputProfile changes. When `true`, the entire strip is reprocessed regardless of individual file hashes.

---

## 7. Image Processing Pipeline Details

### Standard pipeline (no margins)
```
For each PageItem in order:
  load(filePath)                             // lazy, sequential access via libvips
    → scale(targetWidth, LANCZOS3)           // Scaler
    → strip.append(scaledImage)             // ScaledStrip — frees memory as it goes

strip.sliceAll(sliceHeight, lastSlicePolicy)
  → saveEach(outputDir, "output_NNN.png", startIndex)
```

### Margin-aware pipeline
```
For each PageItem in order:
  load(filePath)
    → cropMargins(canvasProfile.margins)    // MarginCropper
    → scale(targetWidth, LANCZOS3)          // Scaler
    → strip.append(scaledImage)            // ScaledStrip

strip.sliceAll(sliceHeight, lastSlicePolicy)
  → saveEach(outputDir, "output_NNN.png", startIndex)
```

### Template generation pipeline
```
createBlank(canvasProfile.canvasSize, white)
  → drawMarginOverlay(margins, visualColour)
  → drawSafeAreaBorder()
  → drawSliceLines(outputProfile.sliceHeight, scaleFactor)
  → save(outputPath, PNG)
```

### Incremental processing logic
```
On run:
  if workspace.stripDirty:
    → full reprocess, rebuild processedFiles map
  else:
    for each PageItem:
      currentHash = sha256(filePath)
      if currentHash != processedFiles[filePath].sha256:
        mark all entries in contributesTo as dirty
    if any dirty outputs:
      reprocess only the affected strip segments
      rebuild sourceMap for changed outputs
      update processedFiles records
```

### Cancellation
Processing is cancellable at any point via a `std::atomic<bool>` token. The pipeline checks the token between slices. On cancellation: already-written output files are kept (partial output), the workspace is not updated, and the log reports how many slices were completed before cancellation.

---

## 8. Performance Strategy

| Bottleneck | Solution |
|---|---|
| Loading large input directory | File paths and thumbnails only — no pixel data loaded until processing starts |
| Per-file RAM usage | libvips sequential access — source image pixels loaded on demand, not upfront |
| Strip RAM footprint | `ScaledStrip` retains only the minimum scaled images needed for the current output slice; typically ≤ 2 images in memory at any time (e.g. when a slice boundary falls mid-file) |
| Scaling | `vips_thumbnail()` Lanczos3 — multi-threaded, no intermediate full-size buffers |
| Batch processing | `QtConcurrent::map` (GUI) or `std::async` pool (CLI) — saturates all CPU cores |
| Thumbnail generation | Async via `QtConcurrent::run`, cached to disk |
| UI responsiveness | `QThread` worker + signal/slot progress updates |
| Incremental reprocessing | SHA-256 + sourceMap — skip unchanged outputs entirely |

**Expected gain vs Clip2l prototype (Python/Pillow):** 10–20× on a multi-core machine (e.g. Intel i9-14900 with 24 threads).

---

## 9. Cross-Platform Targets

| Platform | Status | Notes |
|---|---|---|
| Windows 10/11 | Primary | Developer's main OS |
| Linux | Supported | CMake + vcpkg, standard Qt6 packages |
| macOS | Supported | Qt renders via Metal |
| iOS (iPad) | Not planned | Web app approach preferred (see §10) |

**No platform-specific code** in `libplatemaker`. Platform differences isolated to CMake configuration.

---

## 10. Future: Web Application

The CLI binary (`platemaker`) is designed to serve as the backend for a future web interface. A web server (in any language) can:
- Call `platemaker process --workspace project.platemaker.json --json` as a subprocess
- Parse the JSON summary from stdout
- Stream stderr progress to the browser via WebSocket

This makes the web frontend entirely independent of the C++ codebase. iPad / mobile access is achieved through the browser — no native iOS app required, no Apple Developer Program fee.

---

## 11. Development Roadmap

### Stage 1 — Core Library (no UI, no CLI)
- [x] Set up CMake project with vcpkg manifest (`vcpkg.json`) and `CMakePresets.json`
- [x] Two build presets from day one: `windows-msvc` and `linux-gcc`
- [x] Integrate libvips, nlohmann/json
- [x] Implement `ImageIO` with PNG/JPEG/WebP support and per-file error handling
- [x] Implement `PixelBuffer` abstraction (RAII wrapper for `VipsImage*`)
- [x] Implement `Scaler` (Lanczos3, aspect-preserving, sequential access)
- [x] Implement `ScaledStrip` (streaming, minimum-RAM policy, sourceMap tracking)
- [x] Implement `Slicer` (all three LastSlicePolicy values)
- [x] Implement `MarginCropper`
- [x] Implement `WorkspaceSerializer` (read + write + version field + processedFiles)
- [x] Implement cancellation token (`std::atomic<bool>`)
- [-] Unit tests for all Core components (mock ImageIO — no real files required)

### Stage 2 — CLI (`platemaker`)
- [x] `platemaker workspace create` command
- [ ] `platemaker process` command (standard pipeline)
- [ ] `platemaker process` with margin-aware pipeline
- [ ] `--start-index` flag
- [ ] `--format` flag with JPEG options passthrough
- [ ] Incremental processing (SHA-256 check, dirty propagation)
- [ ] `--json` output mode
- [ ] Drop-in replacement for the Clip2l prototype

### Stage 3 — Template Generator
- [ ] Implement `TemplateGenerator` in Core
- [ ] `platemaker template` CLI command
- [ ] Verify template PNG imports correctly into Procreate

### Stage 4 — Qt GUI (MVP)
- [ ] `MainWindow` + `ToolPanel` base class + tab registry
- [ ] Project tab: file list, lazy thumbnails, drag & drop reordering, sort/reverse
- [ ] Output Settings tab: profiles, format options, start index
- [ ] Process tab: progress bar, log, Run + Cancel buttons (Cancel properly implemented)
- [ ] Workspace save/load from GUI

### Stage 5 — Canvas Profiles in GUI
- [ ] Canvas Profiles tab
- [ ] Visual safe-area preview in profile editor
- [ ] "Generate Template" button wired to `TemplateGenerator`

### Stage 6 — Polish & Future
- [ ] Lightbox / full-page strip preview
- [ ] Batch processing of multiple chapters
- [ ] Web backend integration testing (subprocess JSON protocol)
- [ ] Additional CanvasProfile presets (manga, American, European formats)

---

## 12. Development Environment

- **IDE:** VS Code with CMake Tools, C/C++, Remote-WSL extensions
- **AI assistant:** Cline (VS Code extension) with Anthropic API key (BYOK, pay-per-token)
- **Model:** claude-sonnet-4-6 (best price/quality ratio for coding tasks)
- **Windows build:** MSVC toolchain, CMake preset `windows-msvc`
- **Linux build:** GCC in WSL2, CMake preset `linux-gcc` (same source tree, same `C:\` path)
- **Source location:** Windows filesystem (`C:\`), NOT inside WSL `/home/` — required for Remote-WSL to share files correctly

---

## 13. File & Directory Conventions

```
platemaker/
├── CMakeLists.txt
├── CMakePresets.json              # windows-msvc, linux-gcc, windows-debug presets
├── vcpkg.json                     # manifest: vips, nlohmann-json, qtbase
├── lib/                           # libplatemaker — Core + Infrastructure (zero Qt)
│   ├── include/platemaker/
│   └── src/
├── cli/                           # platemaker binary
├── gui/                           # platemaker-gui binary (Qt)
│   ├── panels/                    # ToolPanel subclasses (one file per tab)
│   └── main.cpp
├── tests/                         # Unit tests (mock ImageIO)
└── docs/
    └── SPECIFICATION.md
```

Output files:
```
<outputDirectory>/
  output_001.png
  output_002.png
  ...
```

Workspace: `<project-name>.platemaker.json` (user-chosen location).  
Cache: `.platemaker-cache/` sibling to `.platemaker.json` (auto-created, safe to delete).

---

## 14. Third-Party Dependencies

| Library | Version | Licence | Purpose |
|---|---|---|---|
| Qt 6 | 6.x latest stable | LGPL v3 | GUI, threading, platform abstraction |
| libvips | 8.x | LGPL v2.1 | Image loading, scaling (Lanczos3), saving |
| nlohmann/json | 3.x | MIT | Workspace JSON serialisation |
| CMake | 3.21+ | BSD | Build system |
| vcpkg | latest | MIT | Package management |

**Qt LGPL note:** Dynamic linking required. Distribute Qt DLLs/SOs alongside the binary. Never statically link Qt in release builds. LGPL permits monetisation (donations, ads) — no commercial licence needed as long as Qt itself is unmodified and dynamically linked.

---

## 15. Distribution

| Platform | Format | Tool |
|---|---|---|
| Windows | ZIP (folder + DLLs) or NSIS installer | `windeployqt.exe` |
| Linux | AppImage (single file, no install needed) | `linuxdeploy --plugin qt` |

**GitHub Releases** hosts the binary artefacts. **GitHub Actions** builds them automatically on every version tag push (`v*`). Public repository = Actions is free.

```
git tag v1.0.0 && git push --tags
  → GitHub Actions builds Windows + Linux in parallel
  → Attaches platemaker-windows-x64.zip and platemaker-linux-x86_64.AppImage to the Release
```

---

## 16. Lessons from the Clip2l Prototype

Key decisions and edge cases discovered during Clip2l development that Platemaker should carry forward:

**Sequence mode is the right default.** The prototype offered both per-file and sequence modes. In practice sequence mode is always the correct choice for Webtoon output — it should be the only mode in Platemaker.

**File ordering is critical and non-obvious.** The prototype supports three sort strategies (name, modified date, created date) plus manual reordering and reverse. All three sort modes should be preserved in Platemaker. Manual drag-and-drop reordering with up/down buttons is essential — filename-based sorting is unreliable across different export tools.

**Thumbnail loading must be non-blocking.** The prototype learned this the hard way with large directories. Loading thumbnails in a background thread while keeping the UI responsive is mandatory.

**JPEG output parameters matter for file size.** The prototype exposes quality, subsampling, optimize, and progressive flags — these are meaningful for artists publishing online where file size affects upload speed. All four should be preserved in Platemaker's JPEG output profile.

**Cancel is not trivial.** The prototype has a cancel stub that warns the user it is unimplemented. In Platemaker, cancellation must be properly implemented from day one via a cancellation token. Partial output (slices written before cancel) should be kept, not deleted.

**Per-file error resilience.** The prototype logs errors per file and continues. This is the correct behaviour — a single corrupt file should not abort a 50-file batch. Skipped files must be reported in the final summary.

**Start index / output numbering.** The prototype exposes `start_postfix` for CLI. This is useful when a chapter is processed in multiple runs (e.g. first half and second half separately). The `--start-index` flag should be preserved.

---

*End of specification. Update this document whenever a significant architectural decision is made or a planned feature changes scope.*

