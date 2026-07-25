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

Baseline: **0.2.1 released, 0.2.2 in progress** (`CMakeLists.txt`).

---

## PATCH — next: 0.2.2

Fixes and additive changes — nothing a consumer must react to. Code built against 0.2.1 keeps
compiling.

### Public API: report what the lib links, at which version, under which licence

Consumers currently have to hardcode this. The GUI's About dialog lists libvips and its
licence with the values injected from its **own** CMake — i.e. the GUI is asserting facts
about the lib's dependencies, which it cannot actually know. Swap libvips for another
backend, or bump it across a licence change, and the GUI keeps confidently showing stale
information with nothing to catch it.

The lib is the only thing that knows what it linked, so it should say so:

```cpp
struct LinkedComponent {
    std::string name;     //!< e.g. "libvips"
    std::string version;  //!< runtime version where available, else build-time
    std::string licence;  //!< SPDX id, e.g. "LGPL-2.1-or-later"
};

/// Third-party components this build of libplatemaker links, for About boxes and
/// licence notices (LGPL requires naming what is used and under what terms).
[[nodiscard]] PLATEMAKER_EXPORT std::vector<LinkedComponent> linkedComponents();
```

Pairs with the existing compile-time `platemaker/version.hpp`: that answers "which lib is
this", this answers "what is inside it". Entries carry versions from wherever they are
actually knowable — libvips at **runtime** via `vips_version(0/1/2)`, nlohmann/json from its
`NLOHMANN_JSON_VERSION_*` macros at build time — which also closes the GUI's open item of
showing them at all, without leaking either into the GUI's link line.

Components to report: **libvips** (LGPL-2.1-or-later) and **nlohmann/json** (MIT). GoogleTest
is test-only and never shipped, so it does not belong here.

**Additive** (nothing removed), so it needs only a **PATCH (0.2.2)**; it may still be bundled
into 0.3.0 if it ships alongside the breaking API work.

**Alternative considered — export it through CMake instead.** The lib could attach the facts
to its exported target and skip the runtime API entirely:

```cmake
target_compile_definitions(platemaker-lib INTERFACE
    PLATEMAKER_VIPS_LICENCE="LGPL-2.1-or-later")
```

`INTERFACE` definitions land in `platemaker-targets.cmake`, so every consumer picks them up
automatically — including one that downloaded a prebuilt package via FetchContent, since the
values are baked into the installed config files. Package-config variables
(`set(platemaker_VIPS_VERSION …)` in `platemaker-config.cmake.in`) work the same way.

It does move ownership to the lib, which is the main point, and it needs no API change — but
it reports what the lib was **built** against, not what the process actually loaded. For an
app that ships its own DLLs (and where a user can swap `libvips-42.dll`), the runtime answer
is the honest one. Not worth maintaining both, so: skip the CMake route and do the function;
being additive it fits a **patch (0.2.2)**.

### Preset adoption can leave two profiles with the same visible name

`enforceOutputProfilePresets()` (`workspace_serializer.cpp:444`) works in two passes: adopt a
profile that *is* the preset into the canonical id, then ensure the preset is present at all.
Adoption is skipped when `outputProfileSignature(op) != outputProfileSignature(preset)` — the
deliberate rule that "a profile the user changed is theirs, and promoting it would make the
shared id assert something false". That rule is right.

The unplanned consequence is a **name collision**. A workspace whose own *Webtoon Standard* has
diverged (canonical is PNG; a workspace shipping JPEG is the obvious real case) fails the
signature check, so the presence pass appends the canonical preset beside it — leaving two
entries both displaying *Webtoon Standard*, distinguishable only by the GUI's `(preset)` suffix.
Observed in a real workspace, not hypothetical.

Nothing is broken — the ids differ, matching is by id, and the read-only guard behaves — but the
user cannot tell from a project's selected-profile name which of the two is in use, and the
combo box on the Output tab shows the name alone.

Options, none obviously best:

- **Disambiguate on adoption failure** — when appending a preset whose name is already taken,
  rename the incoming one (`Webtoon Standard (built-in)`) or the existing one
  (`Webtoon Standard (yours)`). Cheap, but renames data the user did not ask to rename.
- **Disambiguate in presentation only** — the GUI already appends `(preset)` in the manage
  dialog; do the same wherever a profile name is shown, notably the Output tab combo. No data
  touched. Probably the right first move; pairs with a GUI-side entry.
- **Warn on load** — fold into the existing workspace-repair notice: "your *Webtoon Standard*
  differs from the built-in one, so both are now present".

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

### Third-party notices for the bundled DLLs

The About dialog names the direct dependencies, but the Windows packages ship the whole
libvips dependency graph (~90 DLLs: glib, libpng, libjpeg, zlib, expat, …), several of them
LGPL. Distributing them carries notice obligations that listing only "libvips" does not
discharge. Worth a generated `THIRD-PARTY-NOTICES` file in the package rather than more rows
in a dialog — the DLL closure is already computed at install time
(`_pm_install_mingw_dll_closure()`), so the list can be derived rather than maintained by hand.

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

### Aggregate `ProcessingPipeline::run()` parameters into a callbacks struct — **breaking, 0.3.0**

`run()` already takes 10 parameters. The next feature (live per-input status for the GUI,
see the GUI TODO) needs an 11th — a per-input callback — which is the tipping point.

Replace the loose trailing parameters with one aggregate:

```cpp
struct ProcessingCallbacks {
    ProgressFn   onProgress;     //!< after each slice is saved
    LogFn        onLog;
    SliceSavedFn onSliceSaved;
    InputDoneFn  onInputDone;    //!< NEW: per input, as the strip is built or it is skipped
};

ProcessingOutcome run(inputs, outProfile, canvasProfiles, canvasProfileIds, outputDir,
                      const CancellationToken& cancel,
                      const ProcessingCallbacks& callbacks = {},
                      const std::unordered_set<std::string>* onlySlices = nullptr) const;
```

Whether the cancellation token joins the struct is open: it is not a callback, and grouping
it would blur "what the caller wants told" with "how the caller stops the run". Leaning
towards keeping it a separate parameter.

Why `onInputDone` is needed at all: inputs are consumed in phase 1 (strip building) before
the first slice exists, and the pipeline currently reports nothing per input, so a GUI cannot
show input progress live no matter what it does on its side.

**This is an API/ABI break → lib 0.3.0**, and the GUI must move in lockstep (it will pin
`find_package(platemaker 0.3.0 …)`).

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

### Unmatched pages: render them implicitly instead of dropping them — **deviates from SPECIFICATION §7.5.1**

**Current behaviour, and it is deliberate.** `ProcessingPipeline::run()` sets
`hasProfiles = !canvasProfiles.empty()` (`processing_pipeline.cpp:94`) and branches per input
at `:112-128`:

| Workspace | Page whose `W×H` matches nothing |
|---|---|
| no canvas profiles | matching never attempted — page is scaled to `targetWidth` and appended, `appliedProfiles` records an empty profile id (`:133-137`) |
| ≥ 1 canvas profile | warning logged, path pushed to `outcome.skippedPages`, `continue` — the page never joins the strip |

This matches `SPECIFICATION.md` §7.5.1 steps 4a/4b ("page is skipped; reported in final
summary") and its design rationale at `:574-577`: adding a profile to the workspace must not
silently change how existing projects behave, so the user has to opt a project into a profile
consciously.

**The proposal — keep the page, flag the input.** The determinism argument is sound, but the
remedy is disproportionate: the cost of *dropping* a page is a published chapter with a page
missing, and because slice numbering is continuous there is no gap in the output to notice it
by. The only signal is a summary line the user has to read (`render.cpp:466-472` in the GUI,
which reports a bare count).

Treating the page as if it belongs in the project — scaled to `targetWidth`, no margin crop,
exactly the no-profiles path that already exists at `:152-155` — fails softer. Determinism is
then preserved by *visibility* rather than by omission: mark the input as having been rendered
with an **implicit profile** (amber, say), so the state is obvious in the tile grid rather than
buried in a log.

Points that need deciding when this is picked up:

- **`FoundInWorkspaceOnly` probably should not be quietened.** A profile of exactly the right
  size existing in the workspace but not linked to the project is a different situation from
  no profile existing at all — it is a one-click fix, and §7.5.1 step 4b wants it named. Keeping
  it loud (or at least distinctly labelled) while relaxing `NotFoundAnywhere` is likely the
  right split.
- **The diagnosis is currently thrown away.** `ProfileMatchResult` carries three statuses and,
  for `FoundInWorkspaceOnly`, a populated `workspaceCandidates` list. The pipeline discards all
  of it and emits one generic string — `"Skipping (no matching canvas profile for WxH)"`
  (`:120-123`) — so the actionable case is indistinguishable from the genuine gap. Whatever the
  outcome here, that distinction should survive into the log. Pairs with the **structured error
  system** entry above.
- **Surfacing "implicit" per input needs a channel that does not exist.** Inputs are consumed
  in phase 1, before the first slice, and nothing is reported per input — see the
  `onInputDone` callback in the **ProcessingCallbacks** entry below. This item naturally rides
  the same 0.3.0 window.
- **Model impact.** `FileStatus` (`models/project_item.hpp:63-70`) has no value for "processed,
  but without a profile". Whether that becomes a new enumerator or a separate flag alongside
  `AppliedCanvasProfile` (which already represents the no-profile case as an empty id) is open.

**`SPECIFICATION.md` §7.5.1 must be amended if this lands** — steps 4a/4b and the design
rationale below them describe the skip as intended, so the spec is the source of truth being
changed here, not a document lagging behind the code.

Documented as-is in the GUI wiki (`Manual-Canvas-Profiles`, `Manual-Rendering`,
`Manual-Troubleshooting`); those three need revisiting if the behaviour changes.

### WorkspaceEditor — enforce workspace invariants in one place — **0.3.0 window**

**Problem.** `Models::Workspace` is a bare struct — public vectors, no mutating methods. Every
invariant (unique profile ids, no duplicate canvas dimensions, presets present, `templateInfo`
preserved) lives only in `WorkspaceSerializer::load()`, in the anonymous namespace of
`workspace_serializer.cpp` (`mintMissingProfileIds`, `deduplicateIds`,
`enforceOutputProfilePresets`, `relinkProfileId`), reachable **only through a save→load round
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
`enforceOutputProfilePresets` / `relinkProfileId` out of the serializer's anonymous namespace
into a shared internal header, so `load()` and `WorkspaceEditor` call **one** copy of the
rules. Without that the facade merely duplicates what the GUI duplicates today — the problem is
moved, not solved.

**Related, separate:** `ManageCanvasProfilesDialog` losing `templateInfo` is the other half of
the hand-patch — either the dialog stops dropping it, or `replaceCanvasProfiles` carries it.

The **class** is additive, but its purpose is to take away the GUI's direct access to
`m_workspace` — removing/hiding today-public state — so the change **as a whole is breaking**,
hence a MINOR (0.3.0), not a patch. It ships alongside `ProcessingCallbacks`; the GUI adopts it
in **1.2.0**, dropping its direct `m_workspace` mutations. See the GUI TODO for the call-site map.

### The "Webtoon Standard" preset is PNG, which cannot meet the platform it is named after

`webtoonStandardPreset()` (`output_profile.hpp:164-184`) sets `outputFormat = PNG`. The preset
is named after a platform whose published limits are roughly **2 MB per slice and ~20 MB per
chapter** — and a chapter is easily 80+ slices at 800×1280. PNG of comic artwork will not fit
that budget in the general case. The first chapter actually published from this project was
shipped as **JPEG for exactly that reason**, which is the strongest evidence available that the
preset does not describe the workflow it claims to.

So the preset asserts "this is what that platform wants" while specifying a format that will
usually violate the platform's cap. Either the format is wrong or the name is.

**This is not a one-field change.** The header carries its own warning and it applies squarely
here:

> Every field is set **explicitly** rather than left to the struct's defaults. The defaults
> happen to match today, which is precisely the hazard: changing one would silently redefine the
> preset and desynchronise it from every workspace already on disk.

Concretely, flipping PNG → JPEG would:

- **Change the meaning of `op-preset-webtoon-standard`** for every workspace already holding it.
  The id is stable by contract; its *contents* silently would not be.
- **Invalidate rendered output everywhere.** Format is part of `outputProfileSignature()`, so
  every project using the preset would flag its outputs out of sync and ask to re-render — a
  correct signal, but one the user did not cause.
- **Invert the adoption outcome** described in the entry below. Workspaces whose own
  *Webtoon Standard* is JPEG would suddenly match the signature and be adopted (removing their
  duplicate-name problem), while any workspace whose profile is PNG would start diverging and
  gain the duplicate instead. The population simply swaps places.

Options:

- **Change the preset to JPEG**, and treat it as a data migration rather than an edit: decide the
  quality/subsampling (the real-world profile in use is quality 90, 4:4:4, optimise on), bump the
  lib minor, and say so loudly in the changelog and in the workspace-repair notice.
- **Add a second preset** — keep the PNG one, add a JPEG one, and let the names carry the
  difference. No migration, no silent redefinition, at the cost of two near-identical entries in
  a list that already has a name-collision problem.
- **Rename rather than re-format** — if PNG is genuinely the intended "lossless default", stop
  naming it after a platform whose limits it cannot meet, and give it a format-neutral name.

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
plus fixes, so a patch. **In progress: `0.2.2`.**

**Order is forced: lib first.** The GUI pins `LIBPLATEMAKER_VERSION`, which also builds the
FetchContent URL, so until a lib version is on GitHub Releases anyone without a local
`LIBPLATEMAKER_DIR` gets a 404. Tag and upload the lib, then the GUI.

**Lockstep on a breaking lib bump.** When the lib reaches `0.3.0` the GUI moves with it and pins
`find_package(platemaker 0.3.0 CONFIG REQUIRED)`, so an older lib is rejected at configure time
instead of failing at compile time.
