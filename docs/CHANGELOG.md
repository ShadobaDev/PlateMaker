# Changelog

## [0.6.0] — Unreleased

Adds two optional, non-destructive **render-time processing steps** — project-wide **colour correction**
(page domain) and text/bubble **overlays** (strip domain) — behind a small typed-step framework, and
reshapes the pipeline's public API around a `RenderRequest`. Breaking (a MINOR in the 0.x shifted
scale), so the GUI pins the version in lockstep (exactly as 0.5.0 did). Output is **byte-identical for
any project that uses neither step**, and older workspaces load unchanged (the new config is additive,
guarded fields). Also carries the additive profile-portability work originally staged for 0.5.2 (never
released), which re-derives onto this baseline per the cascade rule.

### Changed

- **`ProcessingPipeline::run()` becomes `render(RenderRequest, cancel, callbacks)`.** The eleven
  positional arguments — five of them optional and defaulted — are now named fields on a
  `Core::RenderRequest`, so a call site no longer reads as a column of values whose meaning comes from
  counting commas, and adding a future processing step will not extend the signature again. The
  request holds everything **by value**, which is what makes handing it to a worker thread safe while
  the caller keeps editing the workspace; `onlySlices` is correspondingly a
  `std::optional<std::unordered_set<std::string>>` rather than a borrowed pointer.
- **The page-domain entry points are renamed for how they differ.** `previewLayout()` →
  `layoutPagesFromHeaders()`, `previewPageRgba()` → `decodePageToRgba()`. Both used to say "preview"
  and neither said which one reads *headers* for *every* page and which *decodes* *one* — the only
  thing worth knowing when choosing between them. Neither was ever approximate: the geometry is the
  render's own, which the old name obscured.
- **`ImageIO::load()` gains an optional trailing `convertToSRGB` (default `true`).**
  `true` keeps the historical always-sRGB-on-load behaviour; `false` leaves the source colour space
  alone, which is what a load standing in for the margin-less pipeline needs (that path scales straight
  from the file with no colour transform). Defaulted (source-compatible); the mangled symbol changes.
- **`ProjectItem::stripOverlays` is now a lib-owned inventory, not a public field.** It is private,
  read through `getStripOverlays()` (const + mutable, like `getInputImages()`); mutation goes through
  `addOverlay()` / `removeOverlay()`.
- **`sanitize()`, `mergeFileScan()`, `applyProcessingResults()` and `applyPartialResults()` move from
  `ProjectItem` to `Infrastructure::ProjectEditor`** — where `project_editor.hpp` always said they
  belonged (*"it is the natural home for input add / remove / rescan (currently
  `ProjectItem::mergeFileScan`) as those migrate here"*). Each is driven by state the entity cannot
  see — the filesystem, the workspace's canvas palette, a render's output — rather than being a
  question a project answers about itself, and between them they were **440 of `project_item.cpp`'s
  940 lines**. Call sites become `ProjectEditor{project}.sanitize(...)`; the editor is a transient
  bound to the project for one edit, so this is a one-line change per site.
  `ProjectItem` keeps its containers, its identity and its `const` queries — still ~23 public
  declarations, which `docs/CODING_STYLE.md` now records as a deliberate exception rather than an
  oversight: the method-count rule governs behaviour classes, not aggregates.
- **Headers hold one significant type each.** Five headers were describing more than one thing, and
  the largest made you scroll past 200 lines of data types to reach the class they belonged to.
  `project_item.hpp` (755 lines) sheds its seven structs into `project_files.hpp` (the tracked
  inputs/outputs), `processing_results.hpp` (what a render reports back) and `project_reports.hpp`
  (what the staleness and re-scan queries answer with); `processing_steps.hpp` splits into
  `colour_correction.hpp` and `strip_overlay.hpp`, keeping only the framework that enumerates both —
  the two features shared nothing but the word "step"; `output_profile.hpp` sheds its ~160-line preset
  catalogue into `output_presets.hpp`; `OutputLockedError` and `WorkspaceRepairReport` get their own
  headers, the latter because `WorkspaceEditor` returns it too and living in the serializer's header
  made that look like borrowing. Each original still includes its offspring, so existing includes keep
  working — **except** code naming a preset symbol, which now needs
  `#include <platemaker/models/output_presets.hpp>`.
- **Names that did not say what they do.** The library had four `load()`s, three `save()`s, two
  unrelated `apply()`s and a public/private `generate()` overload pair, so a call site's verb told you
  nothing without chasing the receiver's type. Renamed: `ImageIO::load` / `save` →
  `decode` / `encode` (a pixel codec, now distinct from the JSON serializers, which keep `load`/`save`
  and are thereby unambiguous); `ColourCorrector::apply` → `applyToBuffer` (pairs with the existing
  `applyToRgba`); `StripOverlayCompositor::load` / `apply` → `decodeBitmaps` / `composite`;
  `CanvasProfileMatcher::resolve(w, h)` → `resolveForSize(width, height)`;
  `TemplateGenerator::signature` → `canvasSignature`; `ThumbnailCache::generate` →
  `generateFromImage` / `generateByDecoding` — the overload pair had the same name for opposite
  sources; `PixelBuffer::get` → `vipsImage`; `CanvasConfigChange::any` → `anyChanged` and
  `WorkspaceRepairReport::any` → `anyRepairs` ("any *what*"). Members: `LoadedOverlay::w` / `h` →
  `width` / `height` (the rest of the library spells them out), `ColourCurves::r` / `g` / `b` →
  `red` / `green` / `blue` (beside a `master` that was already spelled out, and easy to read as
  `RGBA`'s components), `SourceSegment::srcY` → `sourceY`, and `ProjectItem`'s snake_case
  `m_input_images` / `m_output_images` / `m_output_directory`, which sat beside camelCase siblings in
  the same class.
  **The on-disk format is untouched:** the JSON keys stay `"r"`, `"g"`, `"b"` and `"srcY"`, since
  renaming those would make every existing workspace lose its curves and provenance records.

### Added

- **`ProjectItem::detectStaleness(canvasProfiles, outputProfile)` — one answer to "does this need
  rendering, and why".** The returned `Models::StalenessReport` carries a field per axis: content
  (from the last `sanitize()`), output-profile signature, output format, canvas config, input
  composition, and the processing steps. Previously each consumer assembled that five-term
  disjunction itself, and both did — the CLI and the GUI carried near-identical copies while only
  three of the five axes were library code at all. Two copies of one rule in two repositories is one
  place for a sixth axis to be added on one side and silently forgotten on the other. The axes stay
  separate rather than collapsing to a flag because consumers need more than the verdict: the CLI
  prints *which* setting moved.

- **Project-wide colour correction (page domain).** A `Models::ColourCorrection` on each `ProjectItem`
  — per-channel tone curves (control points → a 256-entry LUT via `vips_maplut`),
  brightness/contrast/saturation, and per-page exclusions by input uid — applied to each input page
  before scale by a new stateless `Core::ColourCorrector`. Because these are point operations, a
  project-wide grade equals a per-page one while keeping exclusions. Curves are 8-bit/3-band in 
  this release (cubic interpolation + 16-bit deferred).
  `ColourCorrector::applyToRgba()` grades an in-memory RGBA8888 buffer in place (reusing `apply()`), so a
  consumer with no libvips dependency — a GUI — can drive a live grade *preview* of already-decoded output
  slices; being a point grade, that preview equals the committed render.
- **Text/bubble overlays (strip domain).** A `Models::StripOverlay` list on each `ProjectItem` — a
  consumer-rasterised RGBA bitmap with a blend mode — composited onto each output slice by a new
  `Core::StripOverlayCompositor`. An overlay straddling a slice cut lands on **both** adjacent slices
  (libvips clips it), and the library is format-agnostic: it composites bytes, never renders text.
  Curated `Models::BlendMode` set (Over / Multiply / Screen / Overlay / Darken / Lighten).
- **Overlays are anchored to a page, not to a strip offset.** `StripOverlay::anchorInputUid` names the
  input page an overlay rides on (by `InputFile::uid`, the identity the grade's exclusions already use),
  and its `y` is then measured from that page's top edge. `Models::resolveOverlayAnchors()` turns the
  pair into an absolute strip-Y once a layout is known, and `run()` calls it with the strip it just
  built. This is what survives editing a chapter: a bubble stored at an absolute strip-Y drifts onto
  different artwork the moment anything above it changes height — a page inserted, reordered, dropped as
  unreadable, or a canvas profile's margins edited — and drifts silently. An overlay whose anchor page
  is not in the render is logged and skipped rather than falling through to an unrelated page; the
  record survives in the project, so it reappears when its page does. An empty anchor keeps the
  absolute placement, which is what an older workspace loads as.
- **Lib-owned overlay inventory.** `ProjectItem::addOverlay(path, x, y, blend, anchorInputUid)` mints the `ovl-…` uid,
  hashes the bitmap and **dedups identical content** (the SHA mechanic input renames use);
  `removeOverlay(uid)` drops one. The bitmap files are created and owned by the consumer and referenced
  by path (like input files) — the library never copies them.
- **Processing-step staleness axis.** `Models::processingConfigSignature(colourCorrection, overlays)`
  fingerprints the colour/overlay config — **empty when nothing is configured**, so a pre-feature
  project is never falsely invalidated — stored per project as `ProjectItem::processingSignature` and
  folded into the render's full-re-render decision, so a grade or overlay edit re-renders even though no
  input or output file changed.
- **Page-domain preview API — `ProcessingPipeline::previewLayout()` + `previewPageRgba()`.** The same
  page domain the render uses, stopped before the strip: `previewLayout()` returns where every input
  page lands (width, height, matched profile, and the status `run()` would report) decoding no pixels,
  and `previewPageRgba()` writes one page's **ungraded** pixels at strip scale into a caller-owned
  RGBA8888 buffer. Each entry carries its `inputUid`, so a consumer stacking the layout can build the
  uid → strip-Y map `resolveOverlayAnchors()` takes and place overlays exactly where the render will. Together they let a consumer show a chapter's strip **before any render exists**, at
  a cost that tracks the viewport rather than the chapter: layout every page once (a header read each),
  fetch pixels only for what is on screen, and grade those with `ColourCorrector::applyToRgba()` — the
  same engine the render uses, so the preview matches, and a slider move never re-decodes a page. The
  grade is deliberately not baked in, and needs no colour argument at all: because the colour step never
  influences how a page is read, a page fetched once stays a valid baseline for every grade tried on it.
  Both go through the same private page-domain helpers as `run()`, so preview geometry cannot drift
  from the render's — a regression test asserts the layout's total height
  equals the strip a real render builds. Additive: `run()` is unchanged.
- **`Memory` diagnostic trace channel (`Log::Memory`, `--trace=0x4000`).** Reports decoded-pixel
  residency — what libvips is holding right now, the run's high-water mark, and which source pages the
  strip still references — at each append, each slice, and each release. The strip's streaming contract
  ("only the sources overlapping the current slice stay decoded") was previously unobservable from
  outside, because libvips decodes lazily inside its own operations; this reads it back out of the
  libvips allocator instead of requiring a profiler. Off by default, zero cost when disabled.
- **Typed-step descriptor table.** `Models::k_processingStepDefs` enumerates the built-in steps (id,
  name, kind, domain) — the enumerable contract a GUI renders a step stack from; adding a future step is
  a new config struct + a stateless Core applier + one table row, with no change to the existing steps.
- **Portable profile bundles (`.platemaker.profiles.json`).** A new
  `Infrastructure::ProfileBundleSerializer` reads and writes a standalone set of canvas + output
  profiles (a `ProfileBundle`), independent of any workspace, so profiles can move between workspaces
  or be shared. On write it strips `CanvasTemplateInfo` (a template path is relative to a specific
  workspace) and drops any output **preset** (presets are code-defined and resolved from the
  catalogue), so a bundle file always satisfies those invariants. Writes are atomic (temp + rename),
  like `WorkspaceSerializer`. The library never chooses where a bundle lives — that is the consumer's
  concern (a user-picked path, or a GUI-managed library in the OS app-data directory).
- **`WorkspaceEditor::importProfiles(canvas, output)`** — the single cross-workspace transfer rule,
  shared by every consumer: each imported profile is added with a **fresh id**, canvas `templateInfo`
  is **cleared**, and preset-id output profiles are **skipped**. Imports are additive copies, so the
  target workspace stays self-contained. Returns an `ImportProfilesReport` of the minted ids.
- **CLI `workspace export-profiles` / `import-profiles`.** Export the workspace's profiles to a bundle
  (`--out FILE`, optional `--only NAME,…` by name); import into a workspace from either a bundle or
  another `.platemaker.json` workspace (`--from SOURCE`, optional `--only`).

## [0.5.1] — 23.08.2026

Packaging only — no source, API, or behaviour change; code built against 0.5.0 is unaffected. Windows now
also ships an **MSVC** build of the dev and CLI packages alongside the existing MinGW ones. Prompted by a
Microsoft Defender ML false positive (`Trojan:Win32/Wacatac.B!ml`) that flags MinGW-compiled, unsigned
binaries (including `libplatemaker.dll` itself); an MSVC build of the same sources is not flagged.

### Changed

- **Windows releases now publish both MinGW and MSVC packages.** The release CI builds Windows twice
  (`dist-mingw-release` + `dist-msvc-release`), so a consumer built with either toolchain can pull a
  matching, ABI-compatible dev package (`…-windows-msvc-…` vs `…-windows-mingw-…` — the package config's
  guard rejects a cross-toolchain link, and only checks MSVC-vs-MinGW, so the toolchains must stay in step).
  The MSVC job is pinned to the **VS 2022** runner because that is the compiler Qt 6.11 supports, so the
  shipped MSVC lib links into the Qt GUI. A `dist-msvc-debug` preset was added so MSVC has the same
  debug/release dist coverage as MinGW.

## [0.5.0] — 22.08.2026

Defines a **render output contract** (SPECIFICATION §7.0) that keeps a consumer from racing the
pipeline's writes — the root cause of the GUI's intermittent `unable to open for write` render failures —
and folds in the diagnostic logger and the multi-source slice fix from the interim commits. Breaking only
because `ProcessingPipeline::run()` gained a trailing parameter: source-compatible via its default, but
the mangled symbol changes, so the GUI pins the version in lockstep.

### Changed

- **Build requirement: libvips ≥ 8.15 on Linux.** `ImageIO::save()` now uses `VIPS_FOREIGN_KEEP_ICC`
  (added in libvips 8.15) to drop source EXIF/XMP/IPTC while keeping the ICC profile. Windows is unaffected
  (the bundled web-zip is pinned to 8.18); on Linux the build consumes the system libvips, so the CMake
  `pkg_check_modules` now requires `vips-cpp>=8.15` and fails at configure with a clear version error on
  anything older. Distros that ship <8.15 (e.g. Ubuntu 22.04 → 8.12) need a newer libvips or distro
  (Ubuntu 24.04+ ships 8.15, matching CI).
- **`ProcessingPipeline::run()` gains an optional trailing `thumbnailCacheDir`.** When non-empty, the
  pipeline warms a `ThumbnailCache` rooted there from each slice's **in-RAM** pixels — *before*
  `onSliceSaved` fires — so a consumer's `getOrGenerate(outputPath)` is a cache hit and never re-reads a
  freshly-written output during a run. Empty (default) → no thumbnails; the CLI and headless callers pay
  nothing. Defaulted, so existing source compiles unchanged.

### Added

- **Component-gated diagnostic logger** (`Infrastructure::Log`). A runtime `uint64` bitmask, one bit per
  component (ProcessingPipeline / Scaler / ScaledStrip / …), all-off by default, with a swappable sink
  (stderr by default) and no severity levels; `PLATEMAKER_LOG(component, msg)` builds the message only
  when the component is enabled (`setEnabledComponents` / `enable` / `disable` / `isEnabled`). The logger
  is wired into Scaler, ScaledStrip and ProcessingPipeline (each logs its geometry). The CLI gains a
  `--trace=0xMASK` shadow argument to flip components on.
- **Atomic output publish + `OutputLockedError`.** `ImageIO::save()` now encodes to a temp sibling and
  renames it over the destination (whole-or-nothing; a reader never sees a partial slice). If another
  process holds the destination, it throws `OutputLockedError`, which the pipeline surfaces as the new
  `ProcessingErrorCode::OutputLocked` — the library does **not** poll/retry; the consumer owns that
  policy. Fixes intermittent `unable to open for write … Invalid argument` render aborts on Windows.
- **`ThumbnailCache::generate(sourceFilePath, const Core::PixelBuffer&)`** — an in-RAM overload that
  shrinks a caller-supplied image (no file read) and files it under the source path's digest, sharing the
  atomic shrink+write core with the file-reading `generate`. Lets the pipeline pre-warm the cache from
  pixels it already holds. Thumbnail writes are now atomic too.

### Fixed

- **Multi-source slices no longer get a black band.** `ScaledStrip::buildSlice()` folds `vips_join`
  (vertical) over the same-width parts instead of `vips_arrayjoin`, which sized every grid cell to the
  tallest part and black-filled the rest — so any slice combining sources of unequal contributed height
  (short camera photos, a page boundary falling mid-slice) came out padded to `N × maxHeight`. Asserts
  `built.height == the requested slice height` as a post-condition, so it can never silently regress.
- **Mixed RGB/RGBA (and grayscale/CMYK) inputs no longer abort a render.** A folder combining 3-band
  (RGB) and 4-band (RGBA) sources — e.g. camera photos next to a stray screenshot — killed the whole job
  the first time a slice straddled the boundary, because `vips_join` requires equal band counts.
  `ScaledStrip::sliceAll()` now normalises the strip up front by **promoting to the widest band layout**
  (any non-RGB colourspace → sRGB, then a fully-opaque alpha added where missing) — promote-only, so the
  user's pixels are never composited onto a background. A uniform strip is untouched. Alpha is dropped
  only later, at save, and only for **JPEG**, which cannot carry it (flattened over white); PNG/WebP keep
  the alpha end-to-end.
- **EXIF `Orientation` is now honoured on load (both pipelines).** Camera JPEGs store landscape pixels
  plus an Orientation tag; the pipeline ignored it, so a portrait photo rendered on its side, `180°`
  shots rendered upside‑down, and outputs inherited the source tag so viewers re‑rotated them.
  `Scaler::scale(filePath)` and `ImageIO::load()` now `vips_autorot` on load (idempotent for the
  untagged/`1` case — a no‑op for Procreate exports), `headerGeometry()` reports the display dimensions
  (transposed for Orientation 5–8) so canvas‑profile matching agrees with the rendered pixels, and
  `ImageIO::save()` drops source EXIF/XMP/IPTC (keeping the ICC colour profile) so a rendered slice
  carries no stray camera metadata or orientation. (`headerDim()` was renamed `headerGeometry()`.)
- **Input thumbnail previews now follow EXIF orientation too — even for photos already in the cache.**
  `ThumbnailCache::generate` was switched to auto-rotate (dropped `no_rotate`) alongside the load fix
  above, but the on-disk cache is keyed on the source *path* and invalidated by *mtime* only: an
  old-dated camera JPEG always has an older mtime than its cached thumbnail, so a preview written by the
  earlier (sideways) code was judged fresh forever and re-served after the fix. The thumbnail filename now
  carries a **cache-generation version token** (`<digest>.vN.png`, `kThumbnailCacheVersion`); bumping it on
  any generation-logic change orphans the stale thumbnails so they regenerate. The mtime check is kept and
  still catches in-place *content* changes (re-rendered `output_00N` slices) — the token is additive.
- **Adding a canvas profile no longer marks a whole project out of date when the profile matches no
  page.** Canvas profiles match purely by canvas W×H, but a page's dimensions were never recorded, so
  `detectCanvasConfigChange()` could only fall back to a blanket "the effective profile list changed →
  every page is suspect" — creating one profile turned an entire chapter's tiles amber and raised a scary
  "state cannot be confirmed" prompt even when nothing it matched existed in the project. Each
  `InputFile` now records the display **W×H** the render resolved against (added additively to the
  workspace JSON, `0` = unknown), and `detectCanvasConfigChange()` re-matches every page against the
  profiles now in effect — flagging only pages whose applied profile actually changed (a new profile that
  now matches a previously-unmatched page, a removed/reordered one, or an in-place margin edit). The match
  is scoped to the project's **assigned** profiles only (mirroring `CanvasProfileMatcher`'s subA), so an
  unlinked workspace profile of the same size never desyncs a project. A legacy page with no recorded
  size keeps the honest coarse fallback until its first re-render records the size. The single W×H match
  rule is now `Models::canvasSizeMatches()`, shared by the matcher and the staleness re-match.
- Internal: `log.hpp` include guard renamed (`…LOGGING…` → `…LOG…`) to match the header path.

### Tests

- Logger gating + macro laziness; the in-RAM `ThumbnailCache` overload and its warm-→-cache-hit guarantee;
  `ImageIO::save` reporting `OutputLocked` on a held destination (no poll); a deterministic reproduction
  of the output read/write race; `Scaler` autorotating an Orientation‑6 image to portrait and leaving an
  untagged image unchanged. CLI tests assert `--trace` toggles the per-component log tags, and render the
  three real EXIF photos (`fixtures/real_photos/`) to pin no‑black‑band geometry and portrait autorotation.
  `ThumbnailCache` gains two cases: an EXIF‑Orientation‑6 input previews upright (portrait), and the
  thumbnail filename carries a `.vN` cache‑generation token.
- Band-count normalisation: a strip mixing RGB and RGBA sources slices without aborting and every slice is
  promoted to RGBA, while a uniform RGB strip stays 3-band; `ImageIO::save` flattens alpha to RGB for JPEG
  and preserves it for PNG. CLI tests render an interleaved RGB/RGBA folder to both PNG (alpha kept) and
  JPEG (no abort).
- Precise canvas-profile staleness: adding a profile that matches no page keeps the project up to date;
  one that matches a previously-unmatched page flags only that page; an unlinked same-size workspace
  profile never desyncs; duplicate-W×H profiles resolve by effective order; and the coarse fallback still
  fires for legacy pages with no recorded size. `sanitize()` marks only the affected tiles, and the
  per-input W×H round-trips through the workspace serializer. A CLI test renders a folder, confirms the
  dimensions are recorded, then adds a non-matching canvas profile and asserts the re-run is a no-op.

## [0.4.1] — 2026-08-16

### Added

- **Windows version metadata on `libplatemaker.dll` and `platemaker-cli.exe`.** Both now carry a
  `VERSIONINFO` resource (CompanyName, ProductName, File/ProductVersion from the project version,
  description, copyright), so Explorer's *Details* tab and tools like Process Explorer show proper
  identity instead of blanks. Generated from `PROJECT_VERSION` via `lib/cmake/version.rc.in` and
  `cli/version.rc.in` (MinGW/windres, `WIN32`-only). The CLI resource declares its GPL-3.0-or-later
  licence, the DLL its LGPL-3.0-or-later.

- **Embedded version marker (all platforms).** `libplatemaker.so`/`.dll` and `platemaker-cli` now carry
  a single `@(#)platemaker <version> (component)` string literal, so the version is discoverable
  *without running the binary* on platforms that have no Windows `VERSIONINFO` resource (Linux/macOS):
  `what libplatemaker.so.<ver>` or `strings … | grep '@(#)'`. This is the portable companion to the
  Windows metadata above (and to the shared library's `SOVERSION`/versioned filename). Generated from
  `PROJECT_VERSION` via a build-tree-only `lib/cmake/ident.hpp.in` (never installed, not public API);
  the symbol is kept from being stripped with `__attribute__((used))` on GCC/Clang.

## [0.4.0] — 2026-08-05

### Changed

- **Breaking — the processing error channel is now typed.** `ProcessingOutcome::errorMessage` (a free
  string) is replaced by `std::optional<Models::ProcessingError> error`, and
  `ProjectItem::applyProcessingResults()` now **returns** `std::vector<Models::ProcessingError>`
  (previously `void`). Consumers read `outcome.error->message` instead of `outcome.errorMessage`, and
  should surface the vector returned by `applyProcessingResults()`. `outcome.failed` /
  `outcome.cancelled` are unchanged (`failed == error.has_value()`).
- **`Core::InputResult` gains `errorCode` / `errorCategory`** — the per-input skip reason
  (`SkippedError`) now carries a stable machine tag alongside the existing `detail` string, so a
  consumer can group/localise skips without parsing English. The file and message are still
  `inputPath` / `detail` (no duplication).

### Added

- **`Models::ProcessingError` — one typed error vocabulary** (`code` + `category` + `message` + `file`
  + `slice`) shared wherever a failure is consumed as a *result*: `ProcessingOutcome::error`, the
  `applyProcessingResults()` return, and the `InputResult` tag. `ProcessingErrorCategory`
  (`Load` / `ProfileMatch` / `Slice` / `Encode` / `Io`) and `ProcessingErrorCode` cover the pipeline's
  fatal sites and the post-render hash failure. Deliberately **no** live `onError` callback: for a fatal
  error the run returns immediately (read `outcome.error`), and per-input skips already flow through
  `onInput`; `onLog(Error, …)` remains the human transcript.
- **`FileStatus::Error`** — a new, sticky input status for a page that was rendered but whose content
  hash could not be computed afterwards.
- **`WorkspaceEditor::duplicateProject(source, newName)`** — seeds a new project from an existing one,
  copying its **input files** and **profile links** (`canvasProfileIds` + `outputProfileId`) only. The
  output directory, the output slice list and all render state are deliberately dropped, so the copy
  starts with `Pending` inputs and renders into its own folder. Mints a fresh workspace-unique project
  uid (the source keeps its own) and fresh project-local input uids — no identifier collides. Enables
  the GUI's "New from this…" action for the multi-publisher workflow.
- **Complete third-party notices for the bundled DLL graph.** The Windows packages now ship
  `credits/THIRD-PARTY-NOTICES.txt`, `credits/licenses/` (18 canonical/upstream licence texts) and a
  `credits/sbom.spdx.json` covering all 32 components (the 3 direct deps + the ~29 libvips runtime /
  compiler-runtime DLLs — glib, libpng, libjpeg/mozjpeg, freetype, harfbuzz, cairo, pango, webp, …),
  instead of only libvips + nlohmann/json. Generated at configure time (`cmake/gen_credits.cmake`) from
  the web-build's authoritative `versions.json` + a curated `cmake/third_party.json`; a `Third-party
  coverage` CI workflow fails if a bundled DLL is unmapped. The committed `sbom/` snapshot (GitHub
  dependency graph) was refreshed to the full graph. (Legal note: `third_party.json` SPDX ids are
  curated best-effort and pending a final review; libimagequant is disclosed as GPL-3.0-or-later.)
- **Safety net for unforeseen faults.** `ProcessingPipeline::run()` now wraps its whole body so any
  exception that escapes the inline handling (a bug, out-of-memory, a non-`std::exception` throw) becomes
  a typed `Unexpected` / `Internal` failure on `outcome.error` instead of unwinding out of `run()` and
  terminating the caller's worker thread; the CLI gained an equivalent top-level guard that prints a
  reportable diagnostic. New `ProcessingErrorCode::Unexpected` + `ProcessingErrorCategory::Internal`.
  (This handles C++ exceptions only — a hardware fault such as a segfault or null dereference is an OS
  signal / SEH, not a C++ exception, and is out of scope; catching those needs a dedicated crash handler.)
  The CLI also installs `std::set_terminate` to print the in-flight exception on the `terminate` paths
  (uncaught exception / `noexcept` violation / pure-virtual call) instead of aborting silently.

### Fixed

- **An unreadable-after-render input no longer loops forever, silently.** When `computeFileSha256()`
  returned `""` (indistinguishable for "absent" and "locked / denied / offline"),
  `applyProcessingResults()` left a successfully-rendered input at `Pending`; `sanitize()` re-confirmed
  `Pending`, so every subsequent render redid all the work and overwrote the output indefinitely with
  nothing reported. Such an input is now set to `FileStatus::Error` (sticky, non-forcing) and returned
  as a typed `InputHashFailed` failure; it recovers to `Processed` once the file can be read again.

### CLI

- **`project duplicate --workspace F --name SRC --new-name COPY [--output DIR]`** — seeds a new project
  from `SRC` (input files + profile links only; no outputs / output dir / render state), the CLI face of
  `WorkspaceEditor::duplicateProject()`. Refuses a missing source or an already-taken name; without
  `--output` it notes that the copy needs an output directory before rendering.
- **`process` gained structured, TTY-aware output** — a per-input tally (with the typed skip reason),
  a strip-assembly marker, an in-place progress bar (plain lines when piped), timing, and dedicated
  error sections that render `ProcessingError` fields (category / slice / file) plus the post-render
  "unverified input(s)" list. `--json` is unchanged apart from a new `unverifiedInputs` array. Symbols
  only, no colour, so redirected output stays clean.

## [0.3.1] — 2026-08-02

### Added

- **`ProjectEditor` and `WorkspaceEditor` now expose component snapshot/restore** for undo/redo (or any
  save-and-revert-a-single-part need), so a consumer never has to serialise or parse workspace JSON
  itself — the editors, already the validated-mutation authorities, own it:
  - `ProjectEditor::snapshot()` / `restore(str)` — a compact serialisation of *one* project (inputs,
    outputs, profile links, output-profile selection, output dir, render baselines). `restore` reinstalls
    the content in place, rebuilds the lookup tables, and applies the private profile-link fields through
    the friend path; the project `name` is **preserved** (it belongs to the workspace scope).
  - `WorkspaceEditor::snapshotMeta()` / `restoreMeta(str)` — workspace-level metadata only: the canvas
    and output palettes plus the project `(uid, name)` roster, **without** project contents. `restoreMeta`
    reinstalls the palettes through the validated setters (ids preserved, presets stripped, `templateInfo`
    restored exactly) and restores project names by uid, leaving inputs/outputs untouched.

  The two are scoped so a workspace-level restore can never resurrect project content reverted on a
  project's own timeline. (Internally, the Models JSON codec moved from `workspace_serializer.cpp` to a
  shared `model_json` unit both the serializer and the editors use; `WorkspaceSerializer`'s public API
  is unchanged.)
- **Five new platform output presets.** Alongside "Webtoon Standard", the catalogue now ships presets
  for Tapas, NAMICOMI, GlobalComix, Popjoy and ComicFury/indie-web. Design rule: **a preset must always
  produce an uploadable file** for its platform — slice heights are kept conservative (≈1.4–3.3 MP) and
  compressed formats preferred; users who want to push size/quality do it in a custom profile.
  - **Tapas** — `940 × 1504`, JPEG. 940px width is required, with a 2 MB per-file limit; the height
    keeps Webtoon's `1:1.6` ratio (940 × 1.6 = 1504) and a q90 JPEG stays well under 2 MB.
  - **NAMICOMI** — `1200 × 1600`, **PNG**. The platform allows a 250 MB per-chapter budget, so lossless
    is free and its creators prefer it — the one non-lossy preset.
  - **GlobalComix (HD)** — `1280 × 2560`, **WebP**. The platform promotes HD and fully supports WebP,
    which keeps a tall HD slice uploadable (smaller than the equivalent JPEG).
  - **Popjoy** — `1000 × 2000`, JPEG. Targets high-DPI phone screens; 2 MP q90 stays well under a
    typical per-file limit.
  - **ComicFury / Indie** — `950 × 1500`, JPEG. A layout-safe width for classic web CMS templates.

  All resolve from the compile-time catalogue like "Webtoon Standard" (stable ids `op-preset-tapas`,
  `op-preset-namicomi`, `op-preset-globalcomix`, `op-preset-popjoy`, `op-preset-comicfury`; never
  persisted); consumers that list `outputProfilePresets()` — the CLI and the GUI — pick them up
  automatically.

### Packaging

- **The MinGW build no longer depends on MSYS2's libvips (or on an MSVC build).** Both Windows
  toolchains now fetch the same pinned prebuilt "web" libvips via `FetchContent` (each into its own
  build tree, so the branches are independent). MinGW previously used MSYS2's `mingw-w64-x86_64-libvips`
  through pkg-config — the full build (~92 DLLs / ~34 MB, plus `vips-modules` "unable to load …"
  warnings). The slim web build cuts the bundled third-party DLLs to ~40 (~25 MB with the MinGW
  runtime) and drops the module warnings. MinGW links the **C** `libvips` only (the web zip's C++
  runtime is LLVM libc++, ABI-incompatible with libstdc++, but Platemaker uses only the vips C API,
  whose ABI is stable across runtimes) — linking directly against the DLL, so no `.lib`/`.def`/`dlltool`
  step is needed. Building on MinGW now requires only the MSYS2 toolchain (`gcc`, `ninja`); no
  `mingw-w64-x86_64-libvips` package and no `PKG_CONFIG_PATH`.

## [0.3.0] — 2026-07-29

**Breaking** (pre-1.0 shifted scale: a breaking change bumps the minor). The output-profile **preset
model changed** and some symbols were removed. Workspace files stay readable and are migrated on load;
a consumer that used the removed preset internals must adapt.

### Removed

- **`Models::isOutputProfilePresetId()` and `Models::k_outputPresetPrefix`** — preset-ness is no longer
  derived from an id prefix. Use `Models::outputPresetDefById(id)` (returns a definition pointer, or
  `nullptr`) instead.
- **`WorkspaceSerializer`'s adopt/fork/presence preset handling** (`enforceOutputProfilePresets`), the
  "a preset is always present in the workspace" invariant, and `WorkspaceRepairReport`'s
  preset-collision fields. Presets are no longer written into a workspace.

### Changed (breaking)

- **Unmatched input pages are now rendered implicitly instead of being dropped.** When a project has
  canvas profiles and an input's `W×H` matches none of them, the pipeline used to skip the page (it
  never joined the strip, only a summary count noted it — and because slice numbering is continuous,
  the missing page left no visible gap). It is now **scaled to `targetWidth` with no margin crop** (the
  same path a project with no profiles uses) and reported via `onInput` as `AppendedWithoutProfile`, or
  `AppendedProfileNotLinked` (with a *Warning* log naming the ids) when a same-size profile exists in
  the workspace but is not linked to the project. Such a page is marked `FileStatus::Processed` with an
  empty `canvasProfileId`. `ProcessingOutcome::skippedPages` now lists only missing / load-error inputs.
  Two new `InputStatus` values are added; `SkippedNoProfile` / `SkippedProfileNotLinked` are retained
  but unemitted (reserved for a future opt-in "drop unmatched pages" mode). This deviates from the old
  SPECIFICATION §7.5.1 steps 4a/4b (now amended): determinism is preserved by *visibility* (the input
  is flagged) rather than by omission, and quick-start renders and late-added profile-less frames no
  longer lose pages. See §7.5.1.
- **Presets are code-defined and never persisted.** A preset is a baked-in template — a compile-time
  catalogue (`Models::k_outputPresetDefs` / `OutputPresetDef`) materialised on demand — never
  serialised into `outputProfiles`. Preset-ness is **provenance**, tested zero-copy by
  `Models::outputPresetDefById(id)`; there is no "is a preset" field and no reserved id prefix.
  Immutability is the library's own guarantee: the serializer refuses to write a preset-id profile, and
  `load()` migrates older files (a persisted copy of a preset is dropped and its references relinked to
  the canonical id; a diverged preset-id profile is given a fresh id; a user profile that merely
  resembles a preset is untouched). Customising a preset means **duplicating** it. A future change to a
  preset's content therefore no longer desyncs stored workspaces.
- **`Models::resolveOutputProfile(workspace, id)`** — new shared resolver returning
  `std::optional<OutputProfile>`, unioning the workspace's own profiles with the preset catalogue
  (presets take precedence). Replaces per-consumer resolution.
- **`Models::outputProfilePresets()`** now returns `const std::vector<OutputProfile>&` (built once) and
  **`Models::outputProfilePresetById()`** materialises from the compile-time catalogue.
- **`ProcessingPipeline::run()` groups its callbacks into a `Core::ProcessingCallbacks` struct.** The
  three loose trailing callback parameters (`onProgress`, `onLog`, `onSliceSaved`) are replaced by a
  single `const ProcessingCallbacks& callbacks = {}` (before `onlySlices`); each field is an optional
  `std::function`, so a caller wires only what it needs. The per-slice `onSliceSaved` now reports a
  `SliceSaved{sliceIndex, name, fullPath}` (was `(name, fullPath)`) — the absolute 0-based slice index
  lets a consumer address the output by position. `ProcessingProgress` and `ProcessingLogLevel` move
  from `processing_pipeline.hpp` to the new `processing_callbacks.hpp` (a transitive include, so existing
  users are unaffected). `cancel` stays a separate parameter. `run()` is also now **`static`** — the
  pipeline is stateless, so call `ProcessingPipeline::run(...)` without constructing an instance.
- **`Models::Workspace`'s profile palettes are now encapsulated.** `canvasProfiles` and
  `outputProfiles` are no longer public vectors: they are read through `ws.canvasProfiles()` /
  `ws.outputProfiles()` (const accessors) and mutated **only** through the new
  `Infrastructure::WorkspaceEditor`. Direct writes (`ws.canvasProfiles.assign(...)`, `push_back`, …) no
  longer compile — a consumer that edited the vectors by hand must go through the editor. Reading is
  unchanged apart from the `()`. This closes a real hole: an edit made in a running session is now put
  through the *same* invariant checks (unique ids, no persisted presets, `templateInfo` preserved) that
  a loaded file is, instead of only being validated on the next open.
- **`ProjectItem`'s profile-link fields are now encapsulated too.** `canvasProfileIds` and
  `outputProfileId` are no longer public: they are read through `pi.canvasProfileIds()` /
  `pi.outputProfileId()` (const accessors) and written **only** through `ProjectItem::addCanvasProfile()`
  (the dimension guard) and `WorkspaceEditor::setProjectOutputProfile()` / `removeCanvasProfileFromProject()`.
  This closes the last bypass: a raw `pi.outputProfileId = "garbage"` (skipping the id validation) or a
  raw `pi.canvasProfileIds.push_back()` (skipping the "one profile per canvas W×H" guard,
  SPECIFICATION.md §7.5.2) no longer compiles. `from_json(ProjectItem)` is intentionally partial (like
  `Workspace`); `load()` installs the links through the friend path. Other `ProjectItem` fields
  (`name`/`uid`/`inputDirectory`/`outputSignature`/`canvasProfileIdsAtRender`) stay public — they carry
  no cross-cutting invariant.
- **`uuid` → `uid` across `InputFile` / `OutputFile` / `ProjectItem`, and the ids are now genuinely
  unique.** The field and its JSON key are renamed (these are short *local* ids like `file-<hex>` /
  `out-N` / `proj-<hex>`, never RFC 4122 UUIDs — the name over-promised). **No back-compat**: the old
  `"uuid"` key is not read, so a workspace written by an older build loads with empty file/project ids
  that are minted fresh on load (and the old key is dropped on the next save). Input ids are no longer
  derived from the list position (`"file-" + index`), which could hand two files the same id across
  re-scans; `ProjectItem::ensureUniqueFileUids()` and `WorkspaceSerializer::load()` mint via
  `Infrastructure::makeUniqueId` and de-duplicate. The GUI's project id (previously a timestamp — also
  collision-prone) now goes through `makeUniqueId` too.

### Added

- **`Infrastructure::ProjectEditor`** (`platemaker/infrastructure/project_editor/project_editor.hpp`)
  — the authority for a single project's content, twin of `WorkspaceEditor`. Owns input ordering:
  `setInputOrder(orderedUids)` (validates a permutation, rewrites each `InputFile::order`) and
  `moveInput(uid, ±1)`. It **never physically reorders** the stored input vector — only the `order`
  field — so a reorder does not churn the project structure.
- **The render now follows `InputFile::order`, not the stored-vector order.** New
  `ProjectItem::inputsInOrder()` returns the inputs sorted by `order`; the CLI passes it to
  `ProcessingPipeline::run` (the pipeline itself is unchanged — it still renders the sequence it is
  handed, with no knowledge of `order`). Previously the strip was built in raw vector order, so a
  reorder that only touched `order` never reached the output.
- **Input-composition staleness axis** — `ProjectItem::inputOrderAtRender` (the input uid sequence, in
  `order`, captured at render, next to `canvasProfileIdsAtRender`) and
  `ProjectItem::detectInputCompositionChange()`. `sanitize()` marks every output `Desynchronized` when
  the current order/composition differs from the baseline: reordering / adding / removing an input
  shifts the continuous strip so every downstream slice changes while each file stays byte-identical —
  no hash could notice. `WorkspaceSerializer::load()` **backfills** the baseline of a project rendered
  before this field existed from the outputs' `sourceMap` provenance, so an old reorder is still caught
  without a spurious re-render.
- **`WorkspaceEditor::addProject(name)`** — creates a project with a workspace-unique `proj-` uid and
  appends it. Minting the project uid is now the lib's job: the CLI (and GUI) no longer hand-roll it
  with `makeUniqueId` (the last identifier a consumer still generated itself).
- **`Models::outputPresetDefById()`, `OutputPresetDef`, `k_outputPresetDefs`** — the compile-time preset
  catalogue (single source of truth) and its zero-copy membership test.
- **`Core::ProcessingCallbacks` and its event payloads** (`core/processing_callbacks/processing_callbacks.hpp`)
  — the pipeline now reports, in addition to progress/log/slice-saved: **`onInput(InputResult)`** once per
  input (`Appended`; `AppendedWithoutProfile` / `AppendedProfileNotLinked` for an implicitly-rendered page,
  the latter carrying the matching-but-unlinked workspace profile ids; or `SkippedMissing` / `SkippedError`.
  The `SkippedNoProfile` / `SkippedProfileNotLinked` values remain in the enum but are no longer emitted —
  reserved for a future opt-in skip mode); **`onSlicingStarted(SlicingStarted)`** at the phase-1→phase-2
  boundary with the expected slice count;
  and **`onSliceSkipped(SliceSkipped)`** for a clean slice a partial re-render leaves untouched. All are
  optional. Callbacks fire synchronously on the calling thread; the library remains thread- and
  GUI-agnostic.
- **`Models::FileStatus::Skipped`** — a new lifecycle value for an input the last render did not
  include: a **missing** file or one that **failed to load**. (A page that merely matches no canvas
  profile is no longer skipped — it is rendered implicitly and ends up `Processed`; see the
  implicit-render entry under *Changed*.) Distinct from `Missing` and `Processed`. Set by
  `applyProcessingResults()` (above) and reported live during a render via the `onInput` callback;
  a consumer switching over `FileStatus` must handle the new value.
- **`Infrastructure::WorkspaceEditor`** (`platemaker/infrastructure/workspace_editor/workspace_editor.hpp`)
  — the single authority that mutates a workspace's profile palettes while enforcing its invariants.
  Intent-level ops: `addCanvasProfile` / `removeCanvasProfile` / `replaceCanvasProfiles` (carries
  `templateInfo`, mints ids, dedups) and the output-profile equivalents (`replaceOutputProfiles` also
  strips persisted presets); the project-link pair `addCanvasProfileToProject` /
  `removeCanvasProfileFromProject` (the symmetric remove the model lacked); and `setProjectOutputProfile`
  (validates the id resolves, presets included). The identifier-repair rules that used to live inside
  `WorkspaceSerializer::load()` now live here as one copy — `load()` runs them via
  `WorkspaceEditor::installLoaded()`, so an in-memory edit and a loaded file obey the same rules. Lives
  in Infrastructure (not Models) so the invariant logic can mint ids without dragging `id_generator`
  into the model layer.
- **CLI output-profile family, id-selected** — `workspace list-presets`, `add-output-profile`
  (`--from-preset ID`, or from scratch with `--target-width/--slice-height/--format`),
  `mod-/rm-/list-output-profiles`, and `--output-profile ID` on `process` and `project`. The canvas
  commands are renamed `*-canvas-profile`; the old `add-/mod-/rm-/list-profiles` names are removed
  (**breaking**). `list-canvas-profiles` and `list-output-profiles` each list only their own family;
  the combined view is `list-all-profiles` (alias `list-profiles`). `process` uses a selected or
  project-assigned profile exactly as stored, or builds an **ad-hoc** profile from the inline options
  when none is selected — the two never mix, so an override cannot silently edit a stored profile or a
  preset.
- **`Infrastructure::buildInfo()` and `BuildInfo`**
  (`platemaker/infrastructure/build_info/build_info.hpp`) — the library's own version, SPDX licence,
  compiler and target read back **at runtime** from the loaded DLL. The runtime twin of the
  compile-time `version.hpp`: replacing only the shared library no longer leaves a consumer
  reporting a stale version, and a consumer can name the lib's licence without hardcoding it.
- **`Infrastructure::runtimeMatchesHeader()`** — inline consistency check comparing the compile-time
  `version_string` against the loaded `buildInfo().version`, so a consumer can detect a DLL that
  does not match the header it built against (zlib's `ZLIB_VERSION` / `zlibVersion()` idiom).
- **`Infrastructure::linkedComponents()` and `LinkedComponent`** — the third-party components this
  build links, with versions, SPDX licences and upstream GitHub URLs (libvips, LGPL-2.1-or-later, at
  its runtime version; nlohmann/json, MIT). Lets a consumer name what is linked, and under what terms,
  without asserting facts about the lib's dependencies it cannot know. `LinkedComponent::url` is
  guaranteed to be a `https://github.com/` link.
- **`credits/` in the package** — an SPDX 2.3 SBOM (`sbom.spdx.json`) describing libplatemaker and its
  bundled dependencies (versions, SPDX licences, `pkg:github/...` purls), plus each dependency's
  licence text under `credits/licenses/`. The SBOM is the machine-readable inventory expected by the
  EU Cyber Resilience Act and by commercial integrators; the texts satisfy the LGPL requirement to
  distribute a copy of the licence. A consumer locates the directory via the `platemaker_CREDITS_DIR`
  package-config variable.

### Fixed

- **`ProjectItem::sanitize()` no longer discards a skipped page's status.** A page the last render
  skipped (no matching / linked canvas profile) is now **sticky**: `sanitize()` keeps `FileStatus::Skipped`
  as long as the file is unchanged, instead of "un-skipping" it from the disk pass (a stale-but-matching
  hash used to flip it to `Processed`, a never-rendered one to `Pending`). Because a skipped page is
  terminal — nothing can be rendered for it without a profile change — it no longer sets the project
  out of date, so a project with a permanently-skipped page (all outputs `Done`) is correctly reported
  up to date instead of re-rendering on every run. An actual profile-list change is still caught by
  `detectCanvasConfigChange()`, and editing a skipped file re-opens it (`Modified`). `inputsAllProcessed()`
  likewise treats `Skipped` as settled. Fixes the GUI reopening a project with all-`Done` outputs yet
  always re-rendering, and skipped input tiles reverting on reopen.

### Packaging

- **The `find_package(platemaker)` version pin now actually holds pre-1.0.** The config-version file
  switched from `SameMajorVersion` to `SameMinorVersion`: with major `0`, `SameMajorVersion` treats
  every `0.y` as compatible, so a consumer pinned to `0.3.0` would silently accept an incompatible
  `0.4.0` — but under semver a `0.MINOR` bump is exactly where a pre-1.0 breaking change lives (as
  `0.2.0` and this `0.3.0` both were). The package now accepts only the same `0.MINOR.*`.
- **The package rejects an ABI-incompatible toolchain at `find_package` time.** The generated
  `platemaker-config.cmake` records the build compiler and fails with an actionable `FATAL_ERROR`
  when a MinGW/GCC consumer picks up an MSVC-built package (or the reverse) — the two are
  ABI-incompatible (name mangling, STL layout, CRT) and cannot be linked. Previously `find_package`
  succeeded and the mismatch surfaced much later as a baffling link or load error. The message names
  both compilers and points at the matching per-toolchain dev package (`…-windows-mingw-…` vs
  `…-windows-msvc-…`).

## [0.2.1] — 2026-07-20

Additive API only: nothing was removed or changed, so code written against 0.2.0 still
compiles. The reverse does not hold — code using anything added below needs 0.2.1.

Workspace files stay compatible in both directions, but **`load()` behaves differently**: it
now repairs colliding identifiers and appends any missing output preset, so a workspace can
come back with one more profile than was saved. Nothing is lost or overwritten.

### Added

- **`Infrastructure::makeId()` and the `makeUnique*Id()` helpers**
  (`platemaker/infrastructure/id_generator/id_generator.hpp`) — random 128-bit identifiers
  that are checked against the ids already in use, so a collision is impossible rather than
  merely unlikely.
- **Output profile presets** (`platemaker/models/output_profile.hpp`) —
  `webtoonStandardPreset()`, the `outputProfilePresets()` lookup table,
  `isOutputProfilePresetId()` and `outputProfilePresetById()`. Preset identifiers are the
  same in every workspace, so a preset stays recognisable across files and across app
  updates
- **`WorkspaceSerializer::load()` overload taking a `WorkspaceRepairReport`** — reports
  identifier collisions the load had to repair, so a GUI can explain them to the user

### Fixed

- **Profiles could share an identifier, making one of them unreachable** — ids were a
  millisecond timestamp, so every profile created in one pass of the "manage profiles"
  dialog got the same one. The visible symptom was a canvas profile that could not be
  assigned to a project: it counted as *already* assigned and vanished from the assign
  list. Existing workspaces are repaired on load — the first profile keeps the id, later
  duplicates get a new one, and project assignments are preserved
- **Identifiers derived from profile names are gone** (`"cp-" + name`) — a second identity
  scheme that was not unique either, since two profiles with the same name collided.
  Profiles saved without an id now get a random one, and the legacy references that relied
  on the derived form are relinked on load
- **Paths containing non-ASCII characters did not work on Windows** — one cause behind two
  symptoms that looked unrelated:
  - **inputs stayed Pending after a successful render**, so every render redid all the work
    and overwrote the existing output. Hashing an input opened the file through a narrow
    string, which Windows reads in the ANSI code page while the library's paths are UTF-8,
    so the hash silently came back empty and the status was never updated
  - **a workspace could be saved but not reopened.** `save()` opened through a
    `std::filesystem::path` and `load()` through a `std::string`, so the two disagreed about
    the same path. Easy to mistake for a Google Drive limitation, because Drive creates a
    localised folder name (Polish "Mój dysk") — the same file under an ASCII path was fine

  Every filesystem boundary now converts explicitly (`utf8ToPath()` / `pathToUtf8()` in
  `platemaker/infrastructure/file/path_utf8.hpp`), so behaviour no longer depends on the
  toolchain or on the machine's code page. Rendering itself was never affected: libvips
  takes UTF-8 and lets GLib convert it
- **CLI could create two projects with the same identifier** — project ids came from a
  second-resolution timestamp, so a single command creating two projects collided
- **The "Webtoon Standard" profile was defined twice**, once in the CLI and once in the GUI,
  agreeing only because both happened to match the struct's field defaults. Both now seed
  from the library's preset table, so a workspace created either way carries the same
  preset with the same id
- **Presets are kept consistent on load** — a profile that is the preset takes the canonical
  id, one that carries a preset id but has been edited is given an id of its own (its
  settings untouched), and any missing preset is added back. The passes compose: an edited
  profile is separated out, which frees the canonical id, and the genuine preset returns
  beside it

## [0.2.0] — 2026-07-18

Breaking API changes; consumers must be rebuilt. Workspace files stay compatible in both
directions — the new fields are additive, so older builds still open newer workspaces.

### Added

- **Canvas profile change tracking** — each page records the profile it was rendered with
  (`canvasRenderFingerprint()`), so a later edit to that profile is detectable
- **`ProjectItem::detectCanvasConfigChange()`** — reports which pages a profile edit
  invalidated, or that the profile list itself changed
- **`ProjectItem::effectiveCanvasProfileIds()`** — the profiles a project actually renders
  with, mirroring `CanvasProfileMatcher`'s rule
- **`ProcessingOutcome::appliedProfiles`** — which canvas profile the pipeline applied to
  each input

### Changed

- **Breaking — `ProjectItem::sanitize()`** now takes the workspace canvas profiles, and
  flags config-stale files as `Desynchronized` alongside the existing on-disk checks
- **Breaking — `ProjectItem::applyProcessingResults()`** takes the applied profiles and the
  workspace profile list, so the canvas baseline is recorded with the render; it now **also takes
  `skippedInputPaths`** (`ProcessingOutcome::skippedPages`) and marks those inputs
  `FileStatus::Skipped` instead of `Processed`, so a page the render left out (no matching/linked
  canvas profile, or a load error) no longer masquerades as done
- **Breaking — `ScaledStrip::sliceAll()`** streams each slice to a callback instead of
  returning them all; non-const and single-use
- **Breaking — `Slicer` removed** — it wrapped two ints and forwarded to `ScaledStrip`,
  which `ProcessingPipeline` already drove directly

### Fixed

- **`ScaledStrip` memory contract** — sources are released as slicing advances, as the class
  always documented but never did. Peak memory is now flat instead of growing with page
  count (24-page chapter: 534 MB → 213 MB). Output is byte-for-byte identical
- **Canvas profile edits were ignored** — changing margins or canvas size left a project
  reporting itself up to date while its rendered output no longer matched
- **Windows CLI hang on exit** — an intermittent deadlock in the loader's DLL teardown
  (`LdrShutdownProcess`), reproducible under both MinGW and MSVC. The CLI now flushes its
  output and terminates instead of unwinding the dependency graph

## [0.1.1] — 2026-07-14

### Added

- **Processing pipeline** — core image scaling, slicing, and saving in a single pass
- **Partial re-rendering** — only tiles whose configuration changed are reprocessed (tracked via output signature)
- **Incremental processing** — SHA-256 content checks skip unchanged source images
- **Per-format output options** — `PngOptions` and `WebpOptions`; output profile drives format selection and compression settings
- **`CanvasProfileMatcher`** — automatically resolves the best canvas profile based on image dimensions
- **Per-project profile assignment** — each project carries its own canvas and output profile
- **Canvas profile UUIDs** — stable identifiers; workspace serializer is backwards-compatible with 0.1.0 workspaces
- **CLI `--no-profile`** — bypass canvas profile matching and process with explicit parameters
- **CLI template generation** — configurable background colour
- **CLI integration tests** (pytest)
- **Linux `install.sh`** — bundled in the CLI and dev tarballs; detects package manager and prompts for `libvips` if missing
- **Linux portable RPATH** — `$ORIGIN`-relative; binaries run from the build tree and from installed tarballs without `LD_LIBRARY_PATH`
- **Windows DLL pruning (MinGW)** — closure computed from actual PE imports; only required DLLs are bundled
- **SPDX license identifiers** in all source files

### Changed

- Windows (MSVC): libvips download switched from `all` to `web` variant — removes unused Python/Tcl bindings, smaller bundle
- CMake minimum version raised to 3.25 (required for `cmake --workflow`)

### Fixed

- `ProjectItem`: missing move constructor and assignment operator (affected `canvasProfileIds` and `outputProfileId`)
- `OutputProfile`: missing profile ID is relinked to an available profile on load instead of silently failing

## [0.1.0] — initial release
