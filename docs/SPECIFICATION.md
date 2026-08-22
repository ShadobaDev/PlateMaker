# Platemaker — Project Specification

**Status:** Architecture planning phase — no code written yet.  
**Last updated:** 2026-06-02  
**Audience:** Human developer + AI coding assistant (GitHub Copilot, Cline, Claude, etc.)

> **Predecessor:** Clip2l (Python/Tkinter prototype) — https://github.com/ShadobaDev/Clip2l  
> This document supersedes the prototype. Where behaviour differs, this spec takes precedence.

---

## 1. Project Overview

**Platemaker** is a cross-platform desktop application (with CLI) for comic artists. It handles both pre-processing (generating canvas templates with margin guides) and post-processing (scaling and slicing finished artwork for distribution platforms).

The name draws from the printing trade: a *platemaker* crafts the printing plates from which pages are born. Platemaker does the same — it shapes canvases before drawing and prepares finished pages for publication.

**Primary use case (v1):** Processing comic pages for Webtoon-style vertical scroll format. The application treats an ordered set of source images as a single virtual strip, scales them to a target width, and slices the strip into fixed-height output panels.

**Future scope (not in current requirements):** Template generation and margin-aware processing for traditional comic formats — American (6.625" × 10.25"), European (various), manga (B4/A4). The architecture is designed to support multiple `CanvasProfile` presets without code changes.

The project is **open-source** (LGPL 3.0). Monetization via donations may be added in the future — LGPL 3.0 permits this as long as `libplatemaker` remains dynamically linked and unmodified by the end user.

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
│        Qt 6 GUI (separate repository)        │  Tabs/panels, preview, drag&drop
├──────────────────────────────────────────────┤
│        CLI binary  (`platemaker`)            │  Standalone tool; future web backend
├──────────────────────────────────────────────┤
│      Core Library  (`libplatemaker`)         │  Pure C++, zero Qt dependency
│  Scaler │ Strip │ Cropper │ TplGen  │ Match  │
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
| Image processing | libvips | Multi-threaded, lazy/streaming pipeline, Lanczos3 resampling, low RAM |
| JSON serialisation | nlohmann/json | Header-only, MIT licence, clean API |
| Build system | CMake + CMakePresets.json | Standard for C++ cross-platform; preset per platform |
| Package manager | vcpkg (manifest mode) | vcpkg.json in repo; reproducible on every machine |
| File integrity | SHA-256 | Per-input-file hashing for incremental processing |
| Library distribution | LGPL 3.0 shared library | End users can relink; GUI repo links `.dll`/`.so` via `find_package` |

### 4.3 Critical Design Rules

> **`libplatemaker` must have zero Qt dependencies.**

This keeps the Core testable without a display, usable from CLI without Qt, and portable to future backends (web server, Python bindings, etc.). Qt types (`QImage`, `QPixmap`, etc.) must only appear in the GUI layer.

> **The strip must never be fully materialised in RAM.**

`ScaledStrip` processes files one at a time and keeps in memory only the minimum number of scaled images needed to complete the current output slice. For example: if a slice boundary falls mid-file, two files may be in memory simultaneously (tail of file N + head of file N+1). As soon as a file's contribution to all remaining slices is exhausted, it is released. This is critical for large projects where the total input may be several gigabytes — the file list and thumbnails are metadata only; raw pixel data is loaded on demand.

> **Every path in the API is UTF-8, and the conversion to `std::filesystem` is explicit.**

Workspace JSON stores UTF-8 and the GUI hands over UTF-8, but what a *narrow* `std::string`
means is not agreed upon across the standard library: on Windows `std::ifstream` passes the
bytes to `fopen()`, which reads them in the ANSI code page, while libstdc++'s
`std::filesystem::path` reads the same bytes as UTF-8 (MSVC reads them as ANSI in both
places). A program mixing the two conventions is not portable and not even self-consistent.

Nothing in the library may construct a `std::filesystem::path` from a narrow string, or call
`path::string()`. Use `utf8ToPath()` / `pathToUtf8()` from
`platemaker/infrastructure/file/path_utf8.hpp`, which go through `std::u8string` and are
therefore defined by C++20 rather than by the toolchain or the machine's code page.
`path::string()` is additionally unsafe on MSVC: it throws on a character the ANSI page
cannot represent.

This was not hypothetical. Under a path such as `G:/Mój dysk/…` — the folder name Google
Drive creates on a Polish system — hashing an input silently returned nothing, so inputs
stayed `Pending` after a successful render and every render redid all the work; and a
workspace saved through an `fs::path` could never be reopened through a `std::string`. See
`tests/lib-unit-tests/test_path_encoding.cpp`.

### 4.4 CLI as First-Class Citizen

CLI is not a wrapper added after the fact — it is a primary interface and the intended backend for a future web application. A web server can call `platemaker process --json` as a subprocess and parse its stdout. The layer hierarchy is:

```
libplatemaker  (shared library, pure C++)
  ├── platemaker      (CLI binary — links libplatemaker)
  └── platemaker-gui  (Qt GUI binary — separate repo, links libplatemaker .dll/.so)
```

---

## 5. Component Descriptions

### 5.1 Core Library — `libplatemaker`

#### `Scaler`
- Input: file path + target width (px)
- Output: `ScaledImage` (pixel buffer + original file path for provenance)
- Algorithm: **Lanczos3** via `vips_resize()` — aspect-preserving, anti-aliased
- Stateless and thread-safe
- Loads with `VIPS_ACCESS_RANDOM`, which caches the decoded source in RAM; the resize on top of it stays lazy. `vips_thumbnail()` (sequential access) is deliberately **not** used — it makes downstream `vips_extract_area` + tiled encoders read out of order. See the rationale block in `scaler.cpp`.
- Consequence: the full-resolution source is what occupies memory, not the scaled result. `ScaledStrip` is what bounds how many are alive at once.

#### `ScaledStrip`
- Abstraction of the virtual tape
- Accepts `ScaledImage` objects appended one at a time via `append()`
- Produces exactly `floor(totalHeight / sliceHeight)` full slices + 1 tail (if remainder > 0)
- LastSlicePolicy: `CROP` | `PAD_WHITE` | `KEEP_AS_IS`
- Tracks `SourceMap` — which input file and Y-range contributed to each output slice
- **Memory policy:** only the sources overlapping the slice currently being built hold decoded pixels — the minimum the slice cannot be assembled without. This is deliberately not a fixed number:
  - Sources taller than `sliceHeight` (the usual case): 1, or 2 when the slice straddles a source boundary.
  - Sources shorter than `sliceHeight`: as many as it takes to fill the slice. A 200px leftover tail + a 500px extra panel + a 100px spacer + 480px off the next page all feed one 1280px slice → 4 live sources. Inputs are **not** required to be at least one slice tall; this is how a small panel or some breathing room between panels can be inserted without redrawing the surrounding pages.
  
  It rests on two mechanisms, both required:
  1. *Decoding is lazy* — an appended source is decoded only when the first slice overlapping it is computed, not when it is appended.
  2. *`sliceAll()` releases as it advances* — slices go in increasing Y, so a source lying entirely above the current slice can never contribute again and its buffer is dropped.
- `sliceAll()` therefore **consumes** the strip (single-use) and streams slices to a callback rather than returning them: collecting them would be harmless in itself (slices are lazy graphs, not pixels), but saving must interleave with slicing for the release to bound anything.

```cpp
class ScaledStrip {
public:
    using SliceFn = std::function<bool(SliceResult&&)>;  // false → stop

    void append(ScaledImage image);
    int totalHeight() const;
    void sliceAll(int sliceHeight, LastSlicePolicy policy,
                  const CancellationToken& cancel, const SliceFn& onSlice);
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
platemaker [--help | -h | help]
platemaker --version

platemaker workspace create               [--output FILE] [--target-width N] [--slice-height N]

# Canvas profiles (selected by name):
platemaker workspace add-canvas-profile    --workspace FILE --name NAME --canvas WxH --margins T,R,B,L
platemaker workspace mod-canvas-profile    --workspace FILE --name NAME [--canvas WxH] [--margins T,R,B,L]
platemaker workspace rm-canvas-profile     --workspace FILE --name NAME
platemaker workspace list-canvas-profiles  --workspace FILE

# Output profiles (selected by id — names may repeat, ids do not):
platemaker workspace list-presets          --workspace FILE
platemaker workspace add-output-profile    --workspace FILE --name NAME
                      { --from-preset PRESET_ID | [--target-width N] [--slice-height N] [--format png|jpg|webp] }
platemaker workspace mod-output-profile    --workspace FILE --output-profile ID [--name N] [--target-width N] [--slice-height N] [--format png|jpg|webp]
platemaker workspace rm-output-profile     --workspace FILE --output-profile ID
platemaker workspace list-output-profiles  --workspace FILE

platemaker workspace list-all-profiles     --workspace FILE   # alias: list-profiles — canvas + output
platemaker workspace list-projects         --workspace FILE
platemaker project create  --workspace FILE --name NAME [--input DIR] [--output DIR]
platemaker project mod     --workspace FILE --name NAME [--new-name N] [--input DIR] [--output DIR]
                      [--add-canvas-profile NAME] [--rm-canvas-profile NAME] [--output-profile ID]
platemaker project rm      --workspace FILE --name NAME
platemaker project status  --workspace FILE --name NAME
platemaker process --workspace FILE
                      { --input DIR | --project NAME }
                      [--output DIR] [--output-profile ID] [--format png|jpg|webp] [--start-index N]
                      [--target-width N] [--slice-height N]
                      [--no-profile] [--json]
platemaker template --workspace FILE --profile NAME --output FILE
                      [--margins-tpl-color R,G,B[,A]] [--background-tpl-color R,G,B[,A]]
```

**workspace create** — Creates an empty workspace.  No output profiles are stored: the
"Webtoon Standard" preset (800 px target, 1280 px slice) is always available from the catalogue,
so it need not be written to the file.  `--target-width` / `--slice-height` that differ from the
preset store a custom profile instead.  No canvas profiles are created at this point.  `--output`
is optional (default: `./project.platemaker.json`).

**workspace add-canvas-profile** — Adds a `CanvasProfile` entry that describes one physical
canvas size + margins used by the artist.  A workspace can hold multiple profiles (e.g.
one per canvas height variant).  Files are matched to profiles at processing time by
their pixel width.  Canvas profiles are selected by name.

**workspace mod-canvas-profile / rm-canvas-profile** — Modify or remove a named canvas profile.
Use `list-canvas-profiles` to discover profile names.

**workspace output-profile family** — `add-/mod-/rm-/list-output-profiles` and `list-presets`
manage the output profiles.  Output profiles are selected **by id** (names may repeat, ids do not).
`add-output-profile` either copies a preset (`--from-preset PRESET_ID`) or builds one from scratch;
it prints the new id.  Presets are read-only and never persisted — `mod-/rm-output-profile` on a
preset id is refused with a hint to duplicate it.  `list-output-profiles` shows the user's own
profiles `(yours)` and the built-in presets `(preset)`.

**workspace list-all-profiles** (alias `list-profiles`) — Prints the canvas-profile and
output-profile listings together in one view; `list-canvas-profiles` and `list-output-profiles`
each show only their own family.

**Canvas profile matching during `process`:**  
See §7.5 for the full algorithm.  In summary: for each input image the library searches
the project's `canvasProfileIds` list first, then falls back to the workspace-wide
`canvasProfiles` palette.  The library never silently selects a profile that is not
listed in the project — it always reports an actionable error with a suggested CLI
command.  If `canvasProfileIds` is empty, all files are processed without margin
cropping (standard pipeline).

**Exit codes:** `0` success · `1` usage error · `2` IO error · `3` processing error

**`--json` flag:** machine-readable JSON summary to stdout (for web backend subprocess use).

**`--start-index N`:** output numbering starts at N (default 1), e.g. `--start-index 5` → `output_005.png, output_006.png …`

Human-readable progress always goes to **stderr**.

### 5.4 Qt GUI — `platemaker-gui` *(separate repository)*

The Qt GUI is developed in a separate repository. It consumes `libplatemaker` as a shared library via `find_package(platemaker CONFIG REQUIRED)`. See that repository for GUI-specific documentation.

---

## 6. Data Models

> **`id` / `uid` are local identifiers, not RFC 4122 UUIDs** — random `<prefix>-<hex>` strings minted by
> `Infrastructure::id_generator` (profiles use `id`; projects/inputs/outputs use `uid`). The `: uid`
> type annotation below means exactly that.

### `CanvasProfile`
```
id            : uid       // stable identifier — never changes after creation
name          : string    // e.g. "Webtoon 4-page" or "Marvel Standard" (future)
canvasSize    : Size      // full canvas including margins, e.g. 1600×10240
margins       : Margins   // top/right/bottom/left in px
safeArea      : Size      // computed: canvasSize minus margins (read-only)
visualColour  : RGBA      // template overlay colour, e.g. #FF69B4 at 50% alpha
```

### `OutputProfile`
```
id              : uid         // stable identifier — never changes after creation
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

### Per-format options

Format and its encoding options belong to the **`OutputProfile`** (the reusable,
per-project-selected output config referenced by `ProjectItem.outputProfileId`) —
not a separate per-project field. `OutputProfile` carries one option struct per
format; `ImageIO::save()` applies the one matching `outputFormat`.

### `PngOptions`
```
compression  : int   // 0–9 zlib level, default 6
interlaced   : bool  // Adam7 interlacing, default false
```

### `WebpOptions`
```
quality      : int   // 0–100, default 80 (ignored when lossless)
lossless     : bool  // default false
effort       : int   // 0–6 compression/method effort, default 4
```

The GUI Output tab edits these on the project's selected profile inline
(`groupBoxPNG`/`groupBoxJPG`/`groupBoxWebP`); the same profile is also editable via
Manage Output Profiles.

### Output size estimation / platform limits (proposed)

Distribution platforms cap output size — e.g. Webtoon: **≤ 2 MB per slice**,
**≤ 25 MB per chapter**. The pipeline should be able to **estimate** average and
maximum slice size and total batch size *before* a run (from canvas/scaling/slice
geometry and the chosen encoder settings) and/or **report** actual sizes *after*.
The lib is the natural owner of the estimate (it knows geometry + encoding); the
GUI surfaces it and warns when a cap would be exceeded. Tracked in both
`docs/TODO.md` files.

### `PageItem`
```
uid          : uid       // local id (e.g. "file-<hex>"), minted if missing/duplicate on load
filePath     : string    // absolute path on disk
order        : int       // 0-indexed strip position — the render builds the virtual strip in this
                         // order (via ProjectItem::inputsInOrder()), not in the stored-vector order.
                         // Reordering rewrites only this field (Infrastructure::ProjectEditor); the
                         // input vector is never physically moved.
status       : enum      // PENDING | PROCESSED | SKIPPED | ERROR
errorMessage : string    // empty unless status == ERROR or SKIPPED
```

> **Note:** `thumbnailPath` is not persisted. The GUI derives it on demand via
> `ThumbnailCache::thumbnailPath(page.filePath)` — the path is deterministic and
> does not need to be stored in the workspace JSON.

### `ProjectItem`

A **project** is an ordered set of source images that are processed together as a
single virtual strip, sharing one output profile and a curated list of canvas profiles.

```
uid               : uid         // stable local identifier
name              : string      // user-facing label, e.g. "Chapter 01"
inputDirectory    : string      // folder inputs were last scanned from (not authoritative — each
                                //   input carries its own path); CLI project-match key + GUI dialog default
outputDirectory   : string      // where output slices are written
outputProfileId   : uid         // references OutputProfile.id in the parent Workspace
                                // exactly one output profile per project
canvasProfileIds  : uid[]       // ordered list of CanvasProfile.id values
                                // order = priority (first match wins)
                                // invariant: no two entries may refer to profiles
                                // whose canvasSize is identical (conflict guard)
pages             : PageItem[]  // ordered input images
processedFiles    : ProcessedFileRecord[]
stripDirty        : bool
```

**Why a list, not a single id?**  
An artist may work with multiple canvas heights within one chapter (e.g. a normal
page at 10 240 px and a splash at 20 480 px, both 1 600 px wide).  The list lets
the project accept both, while the order encodes which profile takes precedence when
two profiles match the same width but could theoretically both match.  The conflict
guard (§7.5) prevents two profiles with identical dimensions from being registered,
so priority only matters for width-only ambiguity during the search (see §7.5).

**`outputProfileId`** is a direct, stable reference.  If the referenced profile is
deleted from the workspace the project is considered misconfigured and the CLI / GUI
must report an error before processing begins.

### `ProcessedFileRecord`
```
inputFilePath : string    // absolute path
sha256        : string    // hex digest at time of last processing
lastProcessed : datetime  // ISO 8601
contributesTo : string[]  // output filenames that contain pixels from this input
```

### `Workspace`
```
version        : int              // schema version, start at 1
canvasProfiles : CanvasProfile[]  // global palette — all defined profiles
outputProfiles : OutputProfile[]  // global palette — all defined profiles
projects       : ProjectItem[]    // each project has its own profile selection
outputDirectory: string           // workspace-level default (overridden per project)
```

**Workspace file format:** UTF-8 JSON, saved as `<project-name>.platemaker.json`.

> **Migration note (schema v1 → v2):** Earlier builds stored `activeCanvasProfileName`
> and `activeOutputProfileName` at workspace level, plus a flat `pages: PageItem[]` list
> and a top-level `stripDirty`/`processedFiles`.  These fields are replaced by
> `ProjectItem` in schema v2.  The `WorkspaceSerializer` migration chain must convert
> old documents by wrapping the flat page list into a single `ProjectItem` and resolving
> the active profile names to profile ids.

`ProjectItem.stripDirty` is set to `true` whenever: page order changes, a page is
added/removed, or the project's `outputProfileId` changes.  When `true`, the entire
strip for that project is reprocessed regardless of individual file hashes.

---

## 7. Image Processing Pipeline Details

### 7.0 Render output contract (lib ↔ consumer)

The pipeline **produces** output slice files; a consumer (the Qt GUI, the CLI, a future web backend)
**reacts** to them. This is the boundary that keeps a consumer from racing the writer — e.g. reading a
slice to build a thumbnail while a re-render overwrites it (which on Windows fails the write with
`unable to open for write`). The split:

**The library guarantees**
- **G1 — Atomic publish.** `ImageIO::save()` encodes to a temp sibling and renames it over the
  destination, so `onSliceSaved(path)` fires only once the file is complete and closed. A reader opening
  `path` after that signal sees the whole slice or nothing — never a partially-written frame.
- **G2 — No hidden threads or lingering handles.** `ProcessingCallbacks` fire **synchronously on the
  caller's thread**; the pipeline spawns no threads and holds no output handle after `onSliceSaved`.
- **G3 — (opt-in) Live previews without a re-read.** When `ProcessingPipeline::run(..., thumbnailCacheDir)`
  is given a cache dir, the pipeline warms `ThumbnailCache` from the **in-RAM** slice — via
  `ThumbnailCache::generate(outputPath, pixelBuffer)`, sharing the one shrink+write path with the
  file-reading `generate` — *before* `onSliceSaved`. A consumer's later `getOrGenerate(outputPath)` is
  then a cache hit that never opens the output. Empty dir → no thumbnails (CLI default).
- **G4 — Locked destination → typed error, no poll.** If another process holds the destination so the
  publish cannot complete, `save()` throws `OutputLockedError`, which the pipeline reports as
  `ProcessingErrorCode::OutputLocked`. The library **does not** retry — retry policy belongs to the
  consumer.

**The consumer is responsible for**
- **C1** — Treating `onSliceSaved(path)` as the single "ready" signal: never read `path` before it, or
  during a run that may rewrite it.
- **C2** — All threading/marshalling (run the pipeline on a worker thread; marshal callbacks to the UI).
- **C3** — Not building live previews by reading output files: use the warmed cache (G3) during a run, and
  the file-reading `ThumbnailCache::getOrGenerate` only **at rest** (no active run), where reading is safe.
- **C4** — Serialising its own reads against re-renders, and applying per-slice UI updates in order.
- **C5** — Deciding what `OutputLocked` means to the user (warn "close the program holding the file", or
  retry).

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

### Orientation & output metadata

Both pipelines **normalise to display orientation on load**: `Scaler::scale(filePath)` (standard) and
`ImageIO::load()` (margin‑aware) apply `vips_autorot`, which rotates a camera JPEG's pixels per its EXIF
`Orientation` tag and drops the tag. It is **idempotent** for the untagged / `Orientation 1` case, so
Procreate‑style exports (the main workflow) are unaffected; only rotated photos change. So matching sees
the same size the render produces, `headerGeometry()` reports **display** dimensions (width/height
transposed for the 90°/270° tags 5–8). On save, `ImageIO::save()` strips source EXIF/XMP/IPTC (keeping the
ICC colour profile), so a rendered slice never carries the source's orientation, camera fields, GPS or
embedded thumbnail — a viewer shows it exactly as built. A genuinely portrait page needs no special
handling: once autorotated it simply scales to `targetWidth` and contributes its taller height to the
vertical strip.

### Band-count normalisation

`vips_join`, which `ScaledStrip::buildSlice()` uses to stack the parts of a multi-source slice, requires all
inputs to share a band count. A strip mixing RGB (3-band) and RGBA (4-band) sources — e.g. photos next to a
screenshot — would otherwise abort the whole render at the first slice straddling that boundary. Before any
slice is built, `ScaledStrip::sliceAll()` normalises the strip **by promotion, never by flattening**: any
non-RGB colourspace (grayscale, CMYK) is converted to sRGB (lossless for grayscale; already-RGB/RGBA pixels
untouched), then any entry short of the strip's widest band count gains a fully-opaque alpha channel. The
user's pixels are therefore never composited onto a background during assembly, and a uniform strip is left
exactly as-is. Alpha is dropped only at **save**, and only for **JPEG**, which cannot represent it
(`ImageIO::save()` flattens over white); PNG and WebP preserve the alpha end-to-end.

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
On run (per project):
  if project.stripDirty:
    → full reprocess, rebuild project.processedFiles map
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

### Canvas Profile Matching Algorithm (§7.5)

**Goal: maximum determinism.**  The workspace may hold many profiles, but an image
must resolve to exactly one profile (or an unambiguous error) before processing
begins.  The algorithm is deterministic given the same project state — it never makes
silent guesses.

#### 7.5.1 Per-image resolution

**Pre-run (once per project), inside `CanvasProfileMatcher` constructor:**
```
Partition workspace.canvasProfiles() into:
  subA = profiles whose id ∈ project.canvasProfileIds   (preserves priority order)
  subB = all remaining workspace profiles               (fallback pool)
```

The conflict guard (§7.5.2) guarantees subA contains at most one profile per
W×H pair, so the first match in subA is always final — no tie-breaking needed.

**Per image (called for each `PageItem` in the project):**
```
1. Read image width W from the file header (no full pixel decode).

2. Read image height H from the file header.

3. Search subA for first cp where cp.canvasSize.width == W
                                and cp.canvasSize.height == H
   → if found: Matched — use cp (margin-aware pipeline). Done.

4. Search subB for first cp where cp.canvasSize.width == W
                                and cp.canvasSize.height == H

   4a. If not found in subB either:
         → NotFoundAnywhere
         → page is **rendered implicitly**: scaled to the output profile's
           `targetWidth` with no margin crop (the same path a project with no
           canvas profiles uses). `appliedProfiles` records an empty profile id.
         → reported live via `onInput` as `AppendedWithoutProfile` and logged
           (Info): "No canvas profile matches <W>×<H> — rendering <file> without margins".

   4b. If found in subB:
         → FoundInWorkspaceOnly
         → page is **rendered implicitly** exactly as in 4a (no margins), but loudly:
         → reported live via `onInput` as `AppendedProfileNotLinked` carrying the
           matching workspace-profile ids, and logged (Warning):
           "No linked canvas profile matches <W>×<H> — rendering <file> without
            margins; profile '<name>' (<W>×<H>) exists in the workspace but is not
            linked to this project. Link it to apply its margins."
         → linking the profile (`platemaker project add-profile --project <NAME>
           --profile <ID>`) is a one-click fix that applies its margins on the next run.
```

> **Design rationale:** Neither 4a nor 4b drops the page. Dropping is silent — because
> slice numbering is continuous, a missing page leaves no visible gap in the output, so
> a published chapter can lose a page unnoticed. Rendering the page implicitly and
> flagging the input preserves determinism by **visibility** rather than by omission:
> adding a workspace profile still cannot silently change a project's margins (4b renders
> without them and says so loudly until the user consciously links it), but a page is never
> lost. This covers quick-start (render right after install, before any profile exists) and
> late-added, profile-less frames. The `SkippedNoProfile` / `SkippedProfileNotLinked`
> `InputStatus` values are retained (currently unemitted) so a future opt-in "drop unmatched
> pages" mode can restore the strict behaviour without a breaking change.

**Model effect.** An implicitly-rendered page is a normal rendered input: `applyProcessingResults()`
marks it `FileStatus::Processed` with an **empty** `canvasProfileId` — the empty id being the honest
record that no profile was applied (and the reason a later link of a matching profile registers as a
staleness change). `FileStatus::Skipped` now covers only a **missing** file or a **load error** — the
run's `skippedPages` (`ProcessingOutcome::skippedPages`) lists exactly those, and
`ProjectItem::applyProcessingResults()` marks them, so a genuinely-skipped page is flagged instead of
masquerading as done. The pipeline reports each input **live** in phase 1 through
`ProcessingCallbacks::onInput(InputResult)` — `Appended` (matched a profile, or no profiles in use),
`AppendedWithoutProfile` / `AppendedProfileNotLinked` (rendered implicitly, the latter carrying the
unlinked candidate ids), or `SkippedMissing` / `SkippedError` — which the GUI uses to colour input
tiles as the strip is built.

##### Input order & composition (a third staleness axis)

The virtual strip is a **continuous concatenation** built in `InputFile::order` sequence (via
`ProjectItem::inputsInOrder()`), independent of the stored-vector order. Reordering an input — or adding
or removing one — shifts pixels across **every downstream slice** while leaving each input *and* output
file byte-identical, so neither the SHA-256 pass nor the canvas/output-signature axes can see it.

`ProjectItem::applyProcessingResults()` therefore records `inputOrderAtRender` (the input uid sequence,
in `order`, at render — the sibling of `canvasProfileIdsAtRender`, keyed by `uid` so a rename does not
false-invalidate). `sanitize()` compares it via `detectInputCompositionChange()` and, on a mismatch,
marks **every** output `Desynchronized` (the whole strip moved); callers fold the same signal into their
"config changed" decision so the *full* render path runs and refreshes the baseline. A project rendered
before this field existed has its baseline **backfilled** by `WorkspaceSerializer::load()` from the
outputs' `sourceMap` provenance, so a pre-existing reorder is caught without a spurious re-render.

Reordering goes through `Infrastructure::ProjectEditor` (the project-content twin of `WorkspaceEditor`),
which rewrites only the `order` field — a reorder never physically moves `m_input_images`.

#### 7.5.2 Conflict guard (ProjectItem invariant)

Within a single project, `canvasProfileIds` must never contain two profiles whose
`canvasSize` (width × height) is identical.  If it did, the algorithm above would
always select the first one and the second would be unreachable — a silently wasted
entry.

The conflict is detected **at the moment a profile is added to a project**, not
deferred to serialisation.  The library exposes a dedicated mutation method that
checks for a collision and returns a structured error identifying the conflicting
profile — so the CLI can suggest a removal command and the GUI can highlight the
offending entry.

#### 7.5.3 Staleness detection — per-input dimensions and precise re-match

Editing a canvas profile — or adding, removing, or reordering one — changes neither the input files
nor the output files, so no SHA-256 pass can notice that a page's output went stale.
`ProjectItem::detectCanvasConfigChange()` is what notices, and it must be **precise**: marking a whole
project stale whenever the profile list merely grows turns every tile amber and raises an alarming prompt
even when the new profile matches no page in the project.

Because matching is purely by canvas **W×H** (§7.5.1), the question "which profile would this page match
now?" is answerable offline *only if the page's dimensions are known*. Each `InputFile` therefore records
the display **`width`/`height`** the render resolved against — the same post-autorot dimensions the pipeline
fed to `CanvasProfileMatcher::resolve()`, captured via `AppliedCanvasProfile` and stored by
`applyProcessingResults()` (serialised additively; `0` = unknown).

`detectCanvasConfigChange()` then, for each input with known dimensions, resolves the profile it would
match now — the **first profile of that W×H in the project's effective list** (`effectiveCanvasProfileIds()`,
identical to the matcher's subA order) — and flags the page only if that profile's **id or
`canvasRenderFingerprint()`** differs from what was recorded. This catches every case precisely: a new
profile that now matches a previously-unmatched page (`"" → id`), a removed or reordered one, and an
in-place margin edit (same id, different fingerprint). Crucially the re-match considers **only the
project's assigned (effective) profiles**, never workspace-only ones — an unlinked same-size profile
returns `FoundInWorkspaceOnly` at render (the page is rendered without margins), so adding it must not, and
does not, desync the project. `sanitize()` marks exactly the affected inputs and the slices they fed
(`Desynchronized`).

The single W×H rule is `Models::canvasSizeMatches()`, shared by the matcher and this re-match so the two
can never drift. A page with **no recorded dimensions** (a legacy record rendered before dimensions were
tracked) cannot be re-matched, so it falls back to the coarse `listChanged` comparison
(`effectiveCanvasProfileIds() != canvasProfileIdsAtRender`) — the honest outcome, degrading to one full
re-render that records the dimensions and makes the project precise thereafter.

##### Profile identity

`CanvasProfile::id` and `OutputProfile::id` are **random and unique within a workspace**.
They are minted by `Infrastructure::makeUniqueCanvasProfileId()` / `makeUniqueOutputProfileId()`
(`platemaker/infrastructure/id_generator/id_generator.hpp`), which draw 128 random bits and
re-draw on collision with an existing id.  An identifier is opaque: nothing parses it or derives
meaning from it.  Presets carry fixed, well-known ids so a project can reference one across
sessions, but preset-ness is not derived from the id — it is provenance (catalogue membership),
see below.

The generator lives in **Infrastructure**, not Models or Core.  It is not part of the data
model — it produces values rather than describing them — and it is not Core either, because
Core is deterministic domain logic (`CanvasProfileMatcher`, `MarginCropper`, `Scaler` all
return the same answer for the same input).  Drawing on `std::random_device` makes it a
platform service in the same sense as the clock or the filesystem; `CancellationToken` sets
the precedent that Infrastructure here means platform-facing plumbing, not file I/O.

Two rules were dropped in 0.2.1 and must not be reintroduced:

- **Timestamps as identifiers.**  Ids used to be a millisecond timestamp, so profiles minted
  inside one loop shared an id.  A shared id makes the second profile unreachable — every
  lookup resolves to the first — which surfaces as a profile that cannot be assigned to a
  project (it counts as *already* assigned) and silently disappears from the assign list.
- **Deriving an id from the name** (`"cp-" + name`).  A second identity scheme, and not
  unique either: two profiles with the same name collide.

`WorkspaceSerializer::load()` repairs both on the way in.  Profiles with no id are given one
and the legacy `"cp-<name>"` / `"op-<name>"` references are relinked; profiles sharing an id
are separated, with the **first keeping it** so existing project references stay valid and
resolve to the same profile as before.  The second overload reports the collisions it fixed
(`WorkspaceRepairReport`) so a GUI can explain the change; minting an absent id is not
reported, being unambiguous.

After a separation, which of the two profiles a project actually rendered with is unknowable
from the id alone.  It is not guessed: `ProjectItem::sanitize()` settles it exactly, by
comparing the `canvasRenderFingerprint()` recorded per input at render time.

##### Output profile presets

Presets are **baked into the build and never written to a workspace**.  The single source of
truth is a compile-time table, `Models::k_outputPresetDefs` in
`platemaker/models/output_profile.hpp`, from which full `OutputProfile` objects are materialised
on demand (`webtoonStandardPreset()`, `outputProfilePresets()`).  Each preset has a fixed,
well-known id (e.g. `op-preset-webtoon-standard`) so a `ProjectItem::outputProfileId` can
reference one across sessions and app updates; the id is resolved against the catalogue at
runtime by `Models::resolveOutputProfile()`, which unions the workspace's own profiles with the
presets.

A preset *is* an `OutputProfile` — not a distinct type, and carrying **no "is a preset" field**.
Preset-ness is *provenance*: a profile is a preset exactly when it comes from the catalogue.  A
consumer holding a bare id asks `Models::outputPresetDefById(id)` (a zero-copy membership test
over the compile-time table); a consumer listing profiles knows it from which source it read them
(`outputProfilePresets()` vs `Workspace::outputProfiles`).  That is the deliberate reason the two
are **kept separate** rather than merged into one list, and why no flag or reserved id-prefix is
needed to tell them apart.

Because presets are code, not data:

- **Immutability is the library's own guarantee**, not consumer discipline.  There is no shared
  mutable preset state to corrupt (the catalogue is rebuilt from code every run), and the write
  path refuses preset identity — `WorkspaceSerializer` never serialises an `outputProfiles` entry
  whose id is a preset id, so a preset can be neither redefined nor smuggled into a workspace
  regardless of how a consumer drives the library.  The GUI additionally disables Edit/Delete on a
  preset row and offers **Duplicate**; the CLI refuses `mod`/`rm` on a preset id.
- **The developer may change a preset's (experimental) content** in a later release without
  affecting a user's own profiles — those are separate persisted data.  A project that referenced
  a preset directly renders with the new definition, and the existing `outputProfileSignature()`
  staleness check flags its on-disk output as out of date; nothing changes silently.
- **Growth is a pure code change**: add a row to `k_outputPresetDefs`.  No migration, no
  stored-workspace impact.

Customising a preset is a **duplicate** into an ordinary, user-owned profile (a fresh random id).

`load()` migrates workspaces written by older builds that *did* persist presets: a stored profile
carrying a preset id (or the legacy `"op-<name>"` form) that still matches the preset is dropped,
and any project referencing it is relinked to the canonical id (resolved from the catalogue); a
profile that kept a preset id but has diverged is given a fresh id so no user profile can
masquerade as a preset.  A user profile whose settings merely coincide with a preset is left
untouched.  This migration is silent — unambiguous bookkeeping, not a collision — so it is **not**
reported in `WorkspaceRepairReport`.

**Implemented API — and the invariant is now *enforced*, not merely guarded.** The "future
WorkspaceEditor / ProjectEditor type" anticipated here exists as `Infrastructure::WorkspaceEditor`, the
single authority that mutates workspace profile state.  The invariant-bearing fields are **private in the
model**, read through const accessors and written only through the editor / the guarded `ProjectItem`
methods:

- `Workspace::canvasProfiles()` / `outputProfiles()` — the palettes (unique ids, no persisted presets);
- `ProjectItem::canvasProfileIds()` / `outputProfileId()` — the project links.

So a raw `pi.canvasProfileIds.push_back()` (bypassing the dimension guard) or `pi.outputProfileId =
"garbage"` (bypassing id validation) **no longer compiles**.  Linking is
`WorkspaceEditor::addCanvasProfileToProject(project, profileId)`, which delegates to
`ProjectItem::addCanvasProfile()` and applies the dimension-collision guard; `setProjectOutputProfile()`
validates the id resolves (user profile or preset).  `WorkspaceSerializer` (a friend) installs both
palettes and the project links at load time, running the same repair rules an in-session edit obeys.
(`Workspace::projectItems` and `ProjectItem::name`/`uid`/`inputDirectory`/`outputSignature` stay public
— they carry no cross-cutting invariant.)

Linking currently returns `bool` (linked / rejected); the richer result below — naming the specific
conflicting profile — remains a possible enhancement rather than a shipped shape:
```cpp
struct AddCanvasProfileResult {          // possible future enrichment of the bool return
    enum class Status { Ok, Conflict };
    Status status;
    // Non-null only when status == Conflict.
    const CanvasProfile* conflictingProfile = nullptr;
};

// Today: bool WorkspaceEditor::addCanvasProfileToProject(ProjectItem&, const std::string& profileId);
//   Appends profileId to project.canvasProfileIds only when there is no dimension
//   collision with any already-linked profile.
```

**CLI response on `Conflict`:**
```
Error: canvas profile '<new-name>' (WxH) conflicts with already-linked
       profile '<existing-name>' (WxH) [id: <existing-id>].
Hint:  to replace it, run:
         platemaker project rm-profile --project <NAME> --profile <existing-id>
       then re-run the add command.
```

**GUI response on `Conflict`:** display an error and highlight the conflicting
profile entry in the project's canvas-profile list.

`WorkspaceSerializer::save()` may additionally assert the invariant as a safety
net (debug builds), but the primary enforcement is at add-time — save must never
be the *first* place a conflict is discovered.

Profiles at **workspace** level are allowed to share dimensions — the workspace is
a palette, not a processing list.  The invariant only applies inside
`ProjectItem.canvasProfileIds`.

#### 7.5.3 Library API (planned — `Core::CanvasProfileMatcher`)

`CanvasProfileMatcher` is an **object**, not a bag of static functions.  The
constructor performs the workspace partition once per project run; `resolve()` is
then called once per input image in O(N_project_ids) with no further allocation.

```cpp
namespace Platemaker::Core {

struct ProfileMatchResult {
    enum class Status {
        Matched,              // profile found in project list — use it
        NotFoundAnywhere,     // no matching profile in project or workspace
        FoundInWorkspaceOnly, // found in workspace but not linked to project
    };
    Status status;
    const CanvasProfile* profile = nullptr;             // non-null only when Matched
    std::vector<const CanvasProfile*> workspaceCandidates; // non-empty for FoundInWorkspaceOnly
};

class PLATEMAKER_EXPORT CanvasProfileMatcher {
public:
    /// Partitions allWorkspaceProfiles into two ordered subsets once:
    ///   m_projectProfiles      — profiles whose id ∈ projectProfileIds (priority order)
    ///   m_workspaceOnlyProfiles — the remainder
    /// Construction is O(N_workspace × N_project_ids).
    CanvasProfileMatcher(
        const std::vector<CanvasProfile>& allWorkspaceProfiles,
        const std::vector<std::string>&   projectProfileIds); // ordered profile ids

    /// Resolves the canvas profile for one input image of size \p w × \p h.
    ///
    /// Search order:
    ///   1. m_projectProfiles      — O(N_project_ids); conflict guard guarantees
    ///                               at most one match, so the first hit is final.
    ///   2. m_workspaceOnlyProfiles — O(N_workspace - N_project_ids); only reached
    ///                               when step 1 finds nothing.
    ///
    /// Returns Matched / FoundInWorkspaceOnly / NotFoundAnywhere.
    [[nodiscard]] ProfileMatchResult resolve(int w, int h) const;

private:
    std::vector<const CanvasProfile*> m_projectProfiles;        // subA — priority order
    std::vector<const CanvasProfile*> m_workspaceOnlyProfiles;  // subB — fallback
};

} // namespace Platemaker::Core
```

**Typical usage per processing run:**
```cpp
// Constructed once per project, before the page loop.
CanvasProfileMatcher matcher(workspace.canvasProfiles(), project.canvasProfileIds());

for (const auto& page : project.pages) {
    auto [w, h] = readImageDimensions(page.filePath);
    auto result = matcher.resolve(w, h);
    // translate result.status into error / pipeline selection
}
```

The CLI and GUI translate `ProfileMatchResult::status` into the user-facing messages
and suggested commands described in §7.5.1.

---

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
| Batch processing | `std::async` pool (CLI) — saturates all CPU cores |
| Thumbnail generation | Synchronous, blocking (`ThumbnailCache`); GUI wraps in background thread |
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
- [x] Implement `Scaler` (Lanczos3, aspect-preserving, random access)
- [x] Implement `ScaledStrip` (streaming, minimum-RAM policy, sourceMap tracking, all three LastSlicePolicy values)
- [x] Implement `MarginCropper`
- [x] Implement `WorkspaceSerializer` (read + write + version field + processedFiles)
- [x] Implement cancellation token (`std::atomic<bool>`)
- [-] Unit tests for all Core components (mock ImageIO — no real files required)

### Stage 2 — CLI (`platemaker`)
- [x] `platemaker workspace create` command
- [x] `platemaker process` command (standard pipeline)
- [x] `platemaker process` with margin-aware pipeline
- [x] `--start-index` flag
- [x] `--format` flag with JPEG options passthrough
- [x] Incremental processing (SHA-256 check, dirty propagation)
- [x] `--json` output mode
- [-] Drop-in replacement for the Clip2l prototype (deferred — scope unclear)

### Stage 3 — Template Generator
- [x] `platemaker template` with margins, defined profile name as option
- [x] Implement `TemplateGenerator` in Core
- [x] Verify template PNG imports correctly into Procreate

### Stage 4 — Qt GUI *(separate repository — `platemaker-qt`)*

See the `platemaker-qt` repository for the GUI roadmap.

### Stage 5 — Polish & Future
- [ ] Lightbox / full-page strip preview
- [ ] Batch processing of multiple chapters
- [ ] Web backend integration testing (subprocess JSON protocol)
- [ ] Additional CanvasProfile presets (manga, American, European formats)

---

## 12. Development Environment

- **IDE:** VS Code with CMake Tools, C/C++, Remote-WSL extensions
- **AI assistant:** Claude Code (VS Code extension) with Anthropic API key
- **Windows build:** MSVC toolchain, CMake preset `windows-msvc`; MinGW via `windows-mingw`
- **Linux build:** GCC in WSL2, CMake preset `linux-gcc` (same source tree, same `C:\` path)
- **Source location:** Windows filesystem (`C:\`), NOT inside WSL `/home/` — required for Remote-WSL to share files correctly

---

## 13. File & Directory Conventions

```
platemaker/
├── CMakeLists.txt
├── CMakePresets.json              # windows-msvc, linux-gcc, windows-mingw presets
├── vcpkg.json                     # manifest: vips, nlohmann-json, gtest
├── lib/                           # libplatemaker — Core + Infrastructure (zero Qt)
│   ├── cmake/                     # CMake package config template
│   ├── include/platemaker/
│   └── src/
├── cli/                           # platemaker binary
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
| libvips | ≥ 8.15 | LGPL v2.1 | Image loading, scaling (Lanczos3), saving. 8.15 floor: `ImageIO::save` uses `VIPS_FOREIGN_KEEP_ICC`. Windows bundles 8.18; Linux uses system libvips (Ubuntu 24.04+). |
| nlohmann/json | 3.x | MIT | Workspace JSON serialisation |
| CMake | 3.21+ | BSD | Build system |
| vcpkg | latest | MIT | Package management |

**libplatemaker LGPL note:** `libplatemaker` itself is LGPL 3.0. Dynamic linking required — end users must be able to relink against a modified version. Distribute as `platemaker.dll` / `libplatemaker.so`. LGPL permits monetisation (donations) as long as the library remains dynamically linked and unmodified.

---

## 15. Distribution

### CLI binary

| Platform | Format | Notes |
|---|---|---|
| Windows | ZIP (exe + DLLs) | Bundle `platemaker.exe` + `libvips*.dll` |
| Linux | tarball or AppImage | `libplatemaker.so` + `platemaker` binary |

### libplatemaker dev package

Published as a GitHub Release asset alongside the CLI. The Qt GUI project fetches and unpacks it to consume `find_package(platemaker CONFIG REQUIRED)`.

| Platform | Format | Contents |
|---|---|---|
| Windows (MSVC) | ZIP | `bin/platemaker.dll`, `lib/platemaker.lib`, `lib/cmake/platemaker/`, `include/` |
| Windows (MinGW) | ZIP | `bin/platemaker.dll`, `lib/libplatemaker.dll.a`, `lib/cmake/platemaker/`, `include/` |
| Linux | tarball | `lib/libplatemaker.so`, `lib/cmake/platemaker/`, `include/` |

**GitHub Actions** builds all artefacts automatically on every version tag push (`v*`):

```
git tag v1.0.0 && git push --tags
  → GitHub Actions builds Windows (MSVC + MinGW) + Linux in parallel
  → Attaches CLI ZIPs and dev package archives to the Release
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

