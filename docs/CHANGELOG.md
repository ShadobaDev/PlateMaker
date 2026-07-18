# Changelog

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
  workspace profile list, so the canvas baseline is recorded with the render
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
