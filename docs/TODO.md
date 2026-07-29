# TODO

Roadmap grouped by **which semver position a change forces**, not by priority or by the order
work happens in.

**How to read this**
- The version numbers are provisional, non-binding hints — a *kind of bump*, not a schedule.
- A section is the **minimum bump the change forces**. This lib is pre-1.0, so the shifted scale
  applies (recorded in `temp/SEMVER.md`): a **breaking** change bumps the **MINOR**, additions and
  fixes bump the **PATCH**. "Breaking" means **removing or modifying** an existing public symbol
  — a changed signature, argument, or semantics — not only deleting it, and it is judged over
  the *whole* change: a new class whose purpose is to take away existing public access is breaking,
  not additive.
- A change that only forces a patch may still be **bundled into a higher release** if it ships
  alongside bigger work. The header is the floor, not an assignment.
- **Cascade.** Whichever section releases first takes its slot; the rest re-derive from the new
  baseline — ship a MINOR before the pending PATCH and that patch becomes `0.3.1`, the next MINOR
  becomes `0.4.0`.

Baseline: **0.2.1 released, 0.3.0 in progress** (`CMakeLists.txt`). The additive `0.2.2` work
(buildInfo, SBOM) was folded into `0.3.0`, which also makes breaking preset/CLI changes — so the
release is a minor, and `0.2.2` never ships on its own.

---

## PATCH — next: 0.3.1

Fixes and additive changes — nothing a consumer must react to. Code built against 0.3.0 keeps
compiling. (These ride `0.3.1` now that `0.3.0` takes the minor slot; per the cascade rule above,
anything finished in time may still be bundled into `0.3.0` before it ships.)

### Persist last render log

The GUI render log is in-memory only (cleared on exit). Optionally persist the
last run's log (and the slice/skip summary) next to the workspace so a user can
review what the previous render did.

### Dynamic thread spawning for processing

`ProcessingPipeline` runs single-threaded because the virtual strip is built
incrementally and a single slice may span more than one input. A future
pre-process could split the strip at input boundaries into segments that each
yield a whole number of slices; independent segments could then be scaled/sliced
on separate threads and the slice files numbered deterministically afterwards.

### Package version compatibility is wrong for a 0.x library

`lib/CMakeLists.txt:200` uses `COMPATIBILITY SameMajorVersion`. With major `0`, that treats
**every** `0.y` as compatible — so a GUI pinned to `0.3.0` would happily accept `0.4.0`, even
though under semver a `0.x` minor bump is exactly where breaking changes live (as 0.1.1 → 0.2.0
just was).

Switch to `SameMinorVersion` while the lib is pre-1.0: it accepts only the same `0.MINOR.*`
and still rejects anything older than requested. Cheap now, and it is the difference between
the pin actually holding and only appearing to.

### The config package does not check the toolchain — mismatches fail late and cryptically

`lib/cmake/platemaker-config.cmake.in` is four lines and records **nothing** about the compiler
the package was built with. So `find_package(platemaker)` **succeeds** when a MinGW consumer
picks up an MSVC-built package (or the reverse), and the mismatch only surfaces later — as a
link error, or at load time with a message that points nowhere useful. This has already bitten
once: a lib built with MSVC could not be used from a MinGW-built GUI.

Why it cannot work in the first place: C++ has no standardised ABI. MSVC and GCC/MinGW mangle
names differently, and even if the names matched, `std::string` / `std::vector` have different
layouts between MSVC STL and libstdc++, exceptions cannot cross the boundary, and memory
allocated in the DLL cannot be freed in an exe linked to a different CRT.

**Fix — record the toolchain and verify it at `find_package` time.** Pass the build-time
`CMAKE_CXX_COMPILER_ID` (plus, on Windows, the MSVC/MinGW distinction) into the generated
config, then compare it against the consumer's compiler and `message(FATAL_ERROR …)` naming
both sides and pointing at the matching dev package. Roughly 20 lines in
`lib/cmake/platemaker-config.cmake.in` and `lib/CMakeLists.txt`.

This changes nothing about what is shippable — per-toolchain dev packages already exist
(`platemaker-dev-…-windows-mingw-release.zip` vs `…-windows-msvc-release.zip`). It just makes
picking the wrong one fail immediately, with an actionable message, instead of much later with
a baffling one. Same file/area as the `SameMinorVersion` item above, so the two are naturally
done together.

### Third-party notices: extend the SBOM to the full bundled DLL graph

**Foundation shipped in 0.3.0.** The package now emits `credits/sbom.spdx.json` (an SPDX 2.3 SBOM)
plus `credits/licenses/` — but only for the **direct** dependencies (libvips, nlohmann/json). The
Windows packages still ship the whole libvips dependency graph (~90 DLLs: glib, libpng, libjpeg,
zlib, expat, …), several LGPL, and distributing them carries notice obligations that listing only
"libvips" does not discharge.

What remains is to enumerate that closure into the same SBOM (more `packages[]` entries + their
licence texts). The DLL closure is already computed at install time
(`_pm_install_mingw_dll_closure()`), so the list can be derived rather than maintained by hand; a
scanner such as `syft` over the packaged `bin/` is the likely tool. Each DLL needs mapping to its
SPDX licence and text — note MSYS2 does **not** ship a licence file for every package (libvips
itself has none under `share/licenses/`), so some canonical texts must be vendored the way
`lib/cmake/licenses/` already does for LGPL-2.1 and MIT.

### Slim the MinGW package by switching to the `vips-dev-x64-web` variant

The MinGW build ships a much larger libvips than MSVC — **not** because of the compiler,
but because the two branches source different libvips builds
([CMakeLists.txt](../CMakeLists.txt#L54-L95)):

- **MSVC** FetchContents libvips' official `vips-dev-x64-web-8.18.2` zip — a deliberately
  minimal build: **38 DLLs / 22.7 MB**, and no `vips-modules-*` dir (so the "unable to load
  vips-heif/jxl/magick/…" warnings don't appear either).
- **MinGW** uses MSYS2's `mingw-w64-x86_64-libvips` via pkg-config — the **full** build
  (every loader + poppler/openslide/magick/raw/OpenEXR/…), ~92 DLLs / 34 MB even after the
  existing DLL pruning. MSYS2 offers no minimal/web libvips package, so pacman can't provide
  the web variant.

**Plan: point the MinGW branch at the same web zip the MSVC branch already downloads.**
Verified feasible:

- **Formats cover our needs.** The web zip ships PNG, JPEG, WebP, TIFF (+ GIF/HEIF). Platemaker
  saves PNG/JPEG/WebP and loads PNG/JPEG, so nothing we use is missing; only heavy formats we
  never touch (PDF/poppler, openslide, magick, matio, cfitsio, OpenEXR, raw) are gone.
- **ABI is clean because we use the vips C API only** — `grep` confirms zero `VImage` / `vips::`
  / `<vips/vips8>` usage. The web zip's C++ runtime is LLVM libc++ (`libc++.dll`/`libunwind.dll`),
  so linking `libvips-cpp` would clash with our libstdc++ — but we must link the **C** `libvips`
  (not `vips-cpp`), and the C boundary is ABI-stable regardless of the DLL's C++ runtime. Note
  CMake currently links `VIPS::vips-cpp`; switch the alias to the C `libvips`.
- **MinGW can link the web zip's import libs.** It ships a `.def` for every library, so
  `dlltool -d libvips.def -l libvips.dll.a -D libvips-42.dll` produces GNU import libs; modern
  `ld` also often links the MS `.lib` directly.

Side benefits beyond size: fewer DLLs means a smaller `DLL_PROCESS_DETACH` surface at exit —
i.e. less exposure to the loader-shutdown deadlock recorded in
[ISSUES.md](ISSUES.md) (already fixed via fast-exit, but a smaller graph lowers the odds
regardless), and unifies MSVC + MinGW on one libvips source of truth.

**Only real unknown — resolve with a ~30 min spike first:** does MinGW `ld` link the web zip's
libs directly, or is the `dlltool`-from-`.def` step required? Point the MinGW build at the
downloaded `build/msvc-release/_deps/vips_binaries-src`, link the C `libvips`, build the CLI and
run ctest. If green, plan the full switch (CMake branch, DLL-closure/deploy code, README,
CMakePresets).

---

## MINOR — next: 0.3.0

Breaking changes — a consumer must rebuild or adapt. These ship together, and the GUI pins the
version in lockstep.

### Input reorder is not honored by the render, and never invalidates outputs — ✅ **lib SHIPPED (0.3.0)**

> **Done in the lib + CLI.** The render now builds the strip in `InputFile::order` sequence
> (`ProjectItem::inputsInOrder()`), so a reorder that touches only `order` finally reaches the output.
> Reordering is a first-class content op on the new `Infrastructure::ProjectEditor` facade
> (`setInputOrder` / `moveInput`) — it rewrites only the `order` field, never the stored vector. A
> persistent baseline `inputOrderAtRender` (+ `detectInputCompositionChange()`) makes `sanitize()` mark
> affected outputs `Desynchronized` on a reorder / add / remove, surviving reopen; `load()` backfills it
> from output provenance for pre-existing projects. The composition change is folded into the CLI's
> `configChanged` so the full path runs and refreshes the baseline (no re-render loop). Project uids are
> now minted by `WorkspaceEditor::addProject()`. Tests: `test_project_editor.cpp` (+ addProject).
> See CHANGELOG 0.3.0 and SPECIFICATION §7.5.1.
>
> **Remaining — GUI adoption (1.2.0).** The reorder handlers (`onTileMoveUp/Down`, `onRowsMoved`,
> `moveByOrder`) must call `ProjectEditor` instead of writing `order` directly; `projects.cpp` must
> create projects via `WorkspaceEditor::addProject`; `render.cpp` must fold
> `detectInputCompositionChange()` into its `configChanged`. GUI wiki `Manual-Projects` needs a note.
>
> The confirmed root-cause analysis is kept below for reference.

**Symptom (user).** A project already rendered (outputs `Done`); reordering inputs (▲/▼ or drag) and
then Render or Refresh files does not catch the change — outputs stay `Done`, a render reproduces the
same output.

**Root cause (confirmed).**
- The manual reorder handlers change only the `InputFile::order` **field**, never the `m_input_images`
  vector: GUI `Project::onTileMoveUp/Down` → `moveByOrder` (swaps two `order` values via sorted
  pointers) and drag `Project::onRowsMoved` (writes `order = listRow`), in `widgets/project/input.cpp`.
- The render builds the strip from the **raw vector** — `getInputImages()` is passed straight to
  `ProcessingPipeline::run` (GUI `mainwindow/render.cpp:283`, CLI `cli/main.cpp:1223`) and the pipeline
  iterates it in vector order; nothing sorts by `order`. The serializer saves/loads `inputFiles` in
  array order and never re-sorts by `order` (`workspace_serializer.cpp:299/314`).
- So after a manual reorder the **display** (populate sorts a pointer copy by `order`) diverges from the
  **render** (vector order). The reorder is effectively a no-op on output, even across reopen.
- Even if the render honored order, `ProjectItem::sanitize()` only compares input/output file **hashes**
  and **canvas config** — never input order/composition — so it cannot flag stale outputs.
  `mergeFileScan()` *does* set every output `Desynchronized` on a structural change (including
  `inf.order != newOrder`, `project_item.cpp:536/599-603`), but the next `sanitize()` recomputes those
  outputs back to `Done` from disk (bytes unchanged), wiping it. There is no persistent signal.
- **Latent sibling gap:** pure input **removal** has the same problem — the remaining outputs are
  byte-identical, so `sanitize()` resets them to `Done`; the `mergeFileScan` Desync doesn't survive.

**Design to review next session.**
- **Make reordering a first-class model operation** — e.g. `Models::ProjectItem::reorderInputs(ordered
  uids/paths)` as the single authority, the way removal already routes through `mergeFileScan`. Reorder
  should not be GUI-side ad-hoc `order`-field twiddling. Implies a **GUI refactor**: `onTileMoveUp/Down`,
  `onRowsMoved`, `moveByOrder` call the model method (consistent with the WorkspaceEditor round).
- **Canonical order axis (pick one):** (A) the pipeline sorts inputs by `order` — makes `order`
  authoritative, matches its documented meaning ("0-based position in the virtual strip"), zero GUI
  handler change; or (B) keep the vector canonical and have `reorderInputs` physically rebuild it,
  preserving the `vector position == order == strip position` invariant `mergeFileScan` already keeps.
  A first-class model API argues for **B** (model keeps its own invariant); A is smaller.
- **Persistent staleness baseline:** capture the render-time input composition/order as a baseline — the
  analog of `canvasProfileIdsAtRender` — e.g. `inputUidsAtRender` (ordered by `order`; keyed by **uid**
  so a rename does not false-invalidate). Set it in `applyProcessingResults()`, compare it in
  `sanitize()`; a mismatch marks all outputs `Desynchronized` (a reorder cascades through every slice →
  full re-render). This folds add / remove / reorder into one sanitize-level check that survives reopen
  (unlike the transient `mergeFileScan` Desync), and closes the removal gap above.
- **Pre-existing rendered projects (no baseline yet):** backfill the baseline from the outputs' existing
  `sourceMap` provenance on load (it already records which input fed each slice, in order), so there is
  no spurious re-render and a pre-upgrade reorder is still caught. *(User's chosen approach.)*
- **Touch points:** `lib/src/core/processing_pipeline/processing_pipeline.cpp` (strip order),
  `lib/src/models/project_item.cpp` (`sanitize`, `applyProcessingResults`, new `reorderInputs`,
  baseline), `lib/include/platemaker/models/project_item.hpp` (field + method),
  `lib/src/infrastructure/workspace_serializer/workspace_serializer.cpp` (serialize baseline),
  GUI `widgets/project/input.cpp` (reorder handlers), lib tests + `SPECIFICATION.md` §7.5 / pipeline.

### Structured error system

The pipeline currently reports failures as ad-hoc strings via the
`ProcessingPipeline` log/`errorMessage` callbacks (the GUI shows them verbatim).
Replace with structured error codes/categories (load / profile-match / slice /
encode / io) carrying a stable code + message, so the GUI can localise, group and
react (e.g. offer "open output folder", "re-scan inputs"). `ProcessingOutcome`
would carry typed errors instead of a plain string.

**Concrete case to fold in — an unreadable file fails silently and loops forever.**
`FileMetaData::computeFileSha256()` returns an empty string both when the file is absent and
when it exists but cannot be opened; the two are indistinguishable. `applyProcessingResults()`
then skips that input's update entirely (`if (!h.empty())` in
`lib/src/models/project_item.cpp`), leaving `status`, `sha256` and `lastProcessed` untouched.
An input that was never processed therefore stays `Pending` even after a successful render,
`sanitize()` agrees, and the next render redoes all the work and overwrites the output —
indefinitely, with nothing reported anywhere.

This is exactly how the non-ASCII path bug stayed invisible (fixed in 0.2.1). The cause is
gone, the *mechanism* is not: a locked file, missing permissions, or an offline network drive
all produce the same silent loop. Whatever shape the structured errors take, a hash that
fails after a successful render has to surface — at minimum the input must not be left
claiming a state the render did not reach.

**Deliver it through the `ProcessingCallbacks` structure — a typed `onError`.** The `run()`
callbacks already exist (`onProgress` / `onLog` / `onInput` / `onSlicingStarted` / `onSliceSaved`
/ `onSliceSkipped`), so the live half of this system is a new field: `onError(ProcessingError)`
carrying the **typed** error (stable code + category `load`/`profile-match`/`slice`/`encode`/`io`,
the file/slice it happened on, maybe the exception type), fired from the catch sites. It is the
live twin of the typed errors `ProcessingOutcome` would then carry.

Deliberately **not** a string `onError` "like `onLog` but stronger": that was considered and
rejected because it duplicates what already exists — `onLog(Error)`, `outcome.errorMessage`, and
`onInput(InputResult{SkippedError, detail})` — and no consumer needs it. Keep the fatal/non-fatal
line the current code already draws: a **fatal** error also sets `outcome.failed` (the return is
the authoritative "did it fail"); a **non-fatal** per-input problem stays on `onInput`, so `onError`
is not spammed for benign skips. The typed payload is also far cleaner across a future C boundary
than string-scraping the log.

### Unmatched pages: render them implicitly instead of dropping them — ✅ **SHIPPED (0.3.0)**

> **Done in the lib + CLI.** A page whose `W×H` matches no profile is no longer dropped — the
> pipeline renders it implicitly (scaled to `targetWidth`, no margin crop, empty `appliedProfiles`
> id), so it ends up `FileStatus::Processed` with an empty `canvasProfileId`. `onInput` reports the
> new `AppendedWithoutProfile`, or `AppendedProfileNotLinked` (Warning log naming the ids) when a
> same-size profile exists in the workspace unlinked. `outcome.skippedPages` now carries only
> missing / load-error inputs. Matched pages also log which profile was applied. SPECIFICATION §7.5.1
> (steps 4a/4b, rationale, model effect) rewritten; CHANGELOG 0.3.0 has the breaking bullet.
>
> **Decisions taken:** (1) both `NotFoundAnywhere` and `FoundInWorkspaceOnly` render — the latter
> loudly (per user) rather than staying skipped. (2) No new `FileStatus` — the empty `canvasProfileId`
> already records "no profile applied" (and makes a later profile-link register as staleness). (3) The
> `SkippedNoProfile` / `SkippedProfileNotLinked` `InputStatus` values are **retained but unemitted**,
> reserved for a future opt-in "drop unmatched pages" mode, so restoring strict behaviour needs no
> breaking enum change.
>
> **Remaining — GUI round.** Persistent tile colouring (amber for implicitly-rendered inputs) and
> surfacing the "unlinked candidate" nudge across reopens; the GUI wiki (`Manual-Canvas-Profiles`,
> `Manual-Rendering`, `Manual-Troubleshooting`) needs revisiting for the new behaviour.

### WorkspaceEditor — enforce workspace invariants in one place — ✅ **SHIPPED (0.3.0)**

> **Done in the lib + GUI.** `Infrastructure::WorkspaceEditor` is the sole mutation authority;
> `Workspace::canvasProfiles` / `outputProfiles` are private with const accessors; the identifier-repair
> rules live in the editor and `load()` runs them via `installLoaded()`. The CLI is migrated; lib tests
> cover it (`test_workspace_editor.cpp`). The GUI adopted it in 1.2.0 (no raw `m_workspace` profile
> writes remain). See CHANGELOG 0.3.0.
>
> **Tier 2 enforcement — ✅ DONE (0.3.0).** `ProjectItem::canvasProfileIds` / `outputProfileId` are now
> **private with const accessors** (`pi.canvasProfileIds()` / `pi.outputProfileId()`), written only via
> `ProjectItem::addCanvasProfile()` (dimension guard) and `WorkspaceEditor::setProjectOutputProfile()` /
> `removeCanvasProfileFromProject()`. A raw `pi.outputProfileId = "garbage"` or `canvasProfileIds.push_back()`
> no longer compiles. `from_json(ProjectItem)` is partial; `load()` installs the links through the friend
> path (serializer + CLI + GUI + tests migrated). `Workspace::projectItems` and the
> dir/`stripDirty`/`version` fields stay public by design (Tier 3, no cross-cutting invariant) — that is
> the intended boundary, so the WorkspaceEditor topic is now **fully closed**.

**Problem.** `Models::Workspace` is a bare struct — public vectors, no mutating methods. Every
invariant (unique profile ids, no duplicate canvas dimensions, no persisted presets, `templateInfo`
preserved) lives only in `WorkspaceSerializer::load()`, in the anonymous namespace of
`workspace_serializer.cpp` (`mintMissingProfileIds`, `deduplicateIds`,
`migrateOutputProfilePresets`, `relinkProfileId`), reachable **only through a save→load round
trip**. There is no in-memory editing API, so the GUI has no choice but to mutate the vectors
directly and then re-establish the invariants by hand — which means the same edit made in a
running session is *not* validated the way a loaded file is. A duplicate id or duplicate
dimension introduced through a dialog surfaces only on the next open.

The boundary was intended, never built: the `Workspace` class doc references a "workspace
management layer" that does not exist, and `SPECIFICATION.md` §7.5.2 already anticipates a
"future WorkspaceEditor / ProjectEditor type".

**Audit — where the GUI reaches deeper than is healthy** (paths are GUI-repo relative):

*Tier 1 — GUI enforcing lib invariants (the real smell):*
- Wholesale `canvasProfiles.assign(dialogResult)` then hand-minting ids —
  `mainwindow/profiles.cpp:115` + `120-122`; the output twin at `230` + `235-237`. No
  validation / dedup / conflict-guard runs.
- Id minting scattered across five sites — the two above plus new-profile `157` / `266` and
  the edit fallback `323`. The rule "every profile has a unique id" lives in the GUI in
  parallel with the lib.
- `templateInfo` snapshot-and-reattach by id — `profiles.cpp:125-128`, compensating for
  `ManageCanvasProfilesDialog` dropping the field on its round trip.

*Tier 2 — asymmetry and raw setters:*
- Removing a linked profile with a raw `std::remove` — `widgets/project/input.cpp:112` —
  while *adding* goes through `ProjectItem::addCanvasProfile()` with its conflict guard. No
  `removeCanvasProfile()` exists.
- Unvalidated field writes: `outputProfileId =` (`widgets/project/output.cpp:80`, no check the
  id exists in `outputProfiles`), `inputDirectory =` (`input.cpp:262`),
  `getOutputDirectory().clear()` (`output.cpp:139`).
- Input reorder algorithm living in the GUI (`moveByOrder` over the mutable `getInputImages()`
  ref — `input.cpp:412`/`421`).

*Tier 3 — fine, listed so a fix does not over-reach:* indexing `projectItems[idx]`, iterating
for display, `.front()/.size()/.empty()`, `push_back`/`erase` of a whole project. A GUI must
read the model; wrapping every read in an accessor would be cargo cult.

**Direction — a `WorkspaceEditor` facade in the lib**, holding a `Workspace&`, exposing
intent-level operations that enforce invariants with the *same* code `load()` uses:
- `replaceCanvasProfiles(...)` / `replaceOutputProfiles(...)` — take the dialog result, mint
  missing ids, dedup, **carry `templateInfo`** (deletes the GUI's snapshot/restore), keep
  presets. Replaces the `.assign()` + repair loops.
- `addCanvasProfileToProject()` / `removeCanvasProfileFromProject()` — the symmetric pair,
  both through the conflict guard.
- `setProjectOutputProfile()` — validates the id exists.
- (consider at implementation) input reorder, directory setters.

**Necessary precondition, not cosmetics:** lift `mintMissingProfileIds` / `deduplicateIds` /
`migrateOutputProfilePresets` / `relinkProfileId` out of the serializer's anonymous namespace
into a shared internal header, so `load()` and `WorkspaceEditor` call **one** copy of the
rules. Without that the facade merely duplicates what the GUI duplicates today — the problem is
moved, not solved.

**Related, separate:** `ManageCanvasProfilesDialog` losing `templateInfo` is the other half of
the hand-patch — either the dialog stops dropping it, or `replaceCanvasProfiles` carries it.

The **class** is additive, but its purpose is to take away the GUI's direct access to
`m_workspace` — removing/hiding today-public state — so the change **as a whole is breaking**,
hence a MINOR (0.3.0), not a patch. It ships alongside `ProcessingCallbacks`; the GUI adopts it
in **1.2.0**, dropping its direct `m_workspace` mutations. See the GUI TODO for the call-site map.

### The "Webtoon Standard" preset is PNG, which cannot meet the platform it is named after — ✅ **SHIPPED (0.3.0)**

the Webtoon Standard row in `k_outputPresetDefs` (`output_profile.hpp`) sets `outputFormat = PNG`. The preset
is named after a platform whose published limits are roughly **2 MB per slice and ~20 MB per
chapter** — and a chapter is easily 80+ slices at 800×1280. PNG of comic artwork will not fit
that budget in the general case. The first chapter actually published from this project was
shipped as **JPEG for exactly that reason**, which is the strongest evidence available that the
preset does not describe the workflow it claims to.

So the preset asserts "this is what that platform wants" while specifying a format that will
usually violate the platform's cap. Either the format is wrong or the name is.

Now that presets are code-defined templates that are never persisted (0.3.0), changing a preset's
content is safe: it no longer redefines an `op-preset-*` id sitting in saved workspaces, and any
project referencing it is covered by the ordinary `outputProfileSignature()` staleness signal, not a
special migration. What remains here is therefore a
pure **content decision**:

- **Change the preset to JPEG** — decide the quality/subsampling (the real-world profile in use is
  quality 90, 4:4:4, optimise on).
- **Add a second preset** — keep the PNG one, add a JPEG one, and let the names carry the difference.
- **Rename rather than re-format** — if PNG is genuinely the intended "lossless default", stop naming
  it after a platform whose limits it cannot meet, and give it a format-neutral name.

Also worth settling as part of this: whether a preset should encode a **size ceiling** at all, so
the planned output-size warning has something to check against rather than a hardcoded number.

Documented in the GUI wiki as currently-PNG (`Manual-Output-Profiles`); that page needs revisiting
whichever way this goes.

### `ProjectItem::inputDirectory` is dead state — remove it or give it a purpose

The model carries `ProjectItem::inputDirectory`, but nothing in the library reads it: inputs
are tracked as full absolute paths per `InputFile`, and the pipeline, serializer and matcher
never consult it. The only writer is the GUI (`onAddFromDirectory()` stores the last-picked
folder there), and even the GUI never reads it back — it is a leftover from Clip2l's
flat-single-directory model, which Platemaker deliberately moved away from.

It is serialized, so it is dead weight in every workspace file and a small trap for anyone who
assumes it is authoritative. Two ways out:

- **Remove it** — drop the field and its serialization (a workspace-format change; harmless
  since nothing consumes it, but note it in the changelog). Old files with the key still load
  if the reader ignores unknown fields.
- **Give it a defined meaning** — e.g. "the folder to re-open in the add-from-directory
  dialog", which is the one use the GUI might want. If kept, document what it means so it stops
  being ambiguous.

Pairs with the GUI-side entry in `Platemaker-qt/Platemaker/docs/TODO.md`. Decide in one place;
the field lives here, so the removal or the contract is defined here.

---

## MAJOR — next: 1.0.0

The stability commitment, or a change that strands the user (e.g. a workspace format older
versions cannot read). Nothing here is scheduled.

### C facade / stable ABI — evaluated, deliberately deferred

**Idea:** expose the library through a C ABI, so any compiler on a given platform could consume
one binary regardless of how the lib was built — and, optionally, so the lib could be loaded
dynamically at runtime.

**The diagnosis behind it is correct.** C++ has no standardised ABI; a C++ DLL built with MSVC
is not consumable from MinGW. The proof that the C-ABI answer works is already in our own
dependency tree: **libvips** is usable from both toolchains precisely because it is a C library.

**But name mangling is the smallest part of it.** Measured against the current public API:

| obstacle | scale |
|---|---|
| `std::string` / `std::vector` layouts differ between MSVC STL and libstdc++ | **137 lines** of public headers use them |
| exceptions cannot cross a C ABI at all | **41** `throw std::` sites — the entire error strategy |
| allocate in the DLL, free in the exe with a different CRT = UB | every returned container |
| C has no move semantics | `Workspace`, `ProjectItem`, `PixelBuffer`, `ScaledStrip` are move-only |
| `std::function` callbacks | 4 types → function pointer + `void* userdata` |

So this is not "wrap it in `extern "C"`". It is designing a **second, parallel API** — opaque
handles, out-params, error codes, explicit ownership and free functions — and keeping it in
sync forever. With 37 public classes, a 1:1 mirror would be a maintenance disaster.

**If it were ever built**, the sane shape is a narrow *task-level* API (open/save a workspace,
list and edit profiles, run a render with callbacks, query status) — on the order of 20–30
functions — with the C++ API kept for in-tree consumers. Not a mechanical mirror of the models.

**Why not now:** the consumers are the Qt GUI and the CLI, both built from source with the same
compiler, and the planned web backend calls the **CLI as a subprocess**, where no ABI is
involved at all. Per-toolchain dev packages already cover the distribution case, and the
config-package guard (see the *Library* entry above) closes the practical pain — a mismatch
failing late and cryptically — for a fraction of the cost.

**Triggers that would justify revisiting** (concrete, not vibes):
- shipping a **prebuilt** lib to third parties whose compiler we do not control;
- Python or other language bindings via **ctypes/cffi** — note **pybind11 would not need this**,
  as it compiles against the C++ API with the same compiler;
- a plugin system, or runtime `LoadLibrary` / `dlopen` loading;
- wanting a **single** Windows binary that serves both MinGW and MSVC consumers.

For runtime loading specifically, a C ABI is the prerequisite and the usual shape is a single
`platemaker_get_api(version)` returning a struct of function pointers — but that is another
layer of indirection on top of the facade, and there is no use case for it today.

**Status: recorded, not scheduled.** Revisit only if one of the triggers above actually lands.

---

## No release impact

Investigations and test/dev work that ships no change in the library itself.

### Stage 1 integration tests (unit tests with real pixel data)

Seven tests are currently stubbed with `GTEST_SKIP()` pending real image fixtures:

| Test | File |
|---|---|
| `PixelBufferTest.MoveConstructorTransfersOwnership` | `test_pixel_buffer.cpp` |
| `PixelBufferTest.MoveAssignmentTransfersOwnership` | `test_pixel_buffer.cpp` |
| `ScalerTest.ScaleNonExistentFileThrows` | `test_pixel_buffer.cpp` |
| `ScaledStripTest.SliceAllWithZeroSliceHeightThrows` | `test_scaled_strip.cpp` |
| `ScaledStripTest.SliceAllCropPolicyDiscardsRemainder` | `test_scaled_strip.cpp` |
| `ScaledStripTest.SliceAllPadWhiteProducesFullHeightTailSlice` | `test_scaled_strip.cpp` |
| `ScaledStripTest.SliceAllKeepAsIsPreservesShortTailSlice` | `test_scaled_strip.cpp` |

Need: small test PNG fixtures in `tests/lib-unit-tests/fixtures/` and a test
helper that calls `vips_init()`/`vips_shutdown()` in a `SetUpTestSuite` /
`TearDownTestSuite` pair.

---

## Release history & coordination

**Shipped:** `0.1.0` → `0.1.1` → `0.2.0` → `0.2.1` (lib); GUI `1.0.0` → `1.0.1` → `1.1.0`.
`0.2.0` broke the API against `0.1.1` (`ProjectItem::sanitize()` gained a required parameter,
`applyProcessingResults()` two) — breaking, hence the minor, not `0.1.2`. `0.2.1` was additive
plus fixes, so a patch. `0.2.2`'s additive work (buildInfo, SBOM) was folded into `0.3.0`, which
also removes/changes the preset & CLI API — breaking, hence the minor. **In progress: `0.3.0`.**

**Order is forced: lib first.** The GUI pins `LIBPLATEMAKER_VERSION`, which also builds the
FetchContent URL, so until a lib version is on GitHub Releases anyone without a local
`LIBPLATEMAKER_DIR` gets a 404. Tag and upload the lib, then the GUI.

**Lockstep on a breaking lib bump.** When the lib reaches `0.3.0` the GUI moves with it and pins
`find_package(platemaker 0.3.0 CONFIG REQUIRED)`, so an older lib is rejected at configure time
instead of failing at compile time.
