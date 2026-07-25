# Changelog

## [Unreleased] — 0.2.2

Additive API only: nothing removed or changed, so code written against 0.2.1 still compiles.

### Added

- **`Infrastructure::buildInfo()` and `BuildInfo`**
  (`platemaker/infrastructure/build_info/build_info.hpp`) — the library's own version, compiler and
  target read back **at runtime** from the loaded DLL. The runtime twin of the compile-time
  `version.hpp`: replacing only the shared library no longer leaves a consumer reporting a stale
  version.
- **`Infrastructure::runtimeMatchesHeader()`** — inline consistency check comparing the compile-time
  `version_string` against the loaded `buildInfo().version`, so a consumer can detect a DLL that
  does not match the header it built against (zlib's `ZLIB_VERSION` / `zlibVersion()` idiom).
- **`Infrastructure::linkedComponents()` and `LinkedComponent`** — the third-party components this
  build links, with versions and SPDX licences (libvips, LGPL-2.1-or-later, at its runtime version;
  nlohmann/json, MIT). Lets a consumer name what is linked, and under what terms, without asserting
  facts about the lib's dependencies it cannot know.

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
