# Changelog

## [Unreleased] — 0.3.0

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
