/**
 * \file lib/include/platemaker/models/project_item.hpp
 * \brief ProjectItem — represents a single comic chapter (project).
 *
 * A \c ProjectItem is the central unit of work in Platemaker.  It aggregates:
 *  - An ordered list of input image files (\c InputFile) that form the virtual
 *    strip for one comic chapter.
 *  - A list of output slice files (\c OutputFile) produced by the last
 *    processing run.
 *  - Metadata required for incremental processing (SHA-256 hashes, statuses).
 *
 * ## Lookup tables (non-serialised)
 *
 * \c ProjectItem maintains two runtime lookup tables that are rebuilt from
 * serialised data on every load (\c rebuildLookupTables()):
 *  - \c m_inputToOutputLookup — maps input file path → output file names.
 *  - \c m_sha256Index         — maps SHA-256 hex digest → current file path;
 *                               used to detect file renames across directory scans.
 *
 * ## Incremental processing
 *
 * Call \c sanitize() before running the pipeline to update all file statuses.
 * After a successful pipeline run call \c applyProcessingResults() to update
 * hashes, re-populate provenance and rebuild the lookup tables — all in one
 * place rather than scattered across the CLI/GUI layer.
 *
 * ## Directory re-scan (project mod --input)
 *
 * \c mergeFileScan() replaces the current input list with a new directory
 * scan while preserving incremental-processing data for unchanged files.
 * It uses SHA-256 to identify files that were renamed (same content, different
 * path) and only marks structurally new/removed/reordered files as dirty,
 * avoiding a full reprocess when only filenames changed.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_PROJECT_ITEM_HPP
#define PLATEMAKER_MODELS_PROJECT_ITEM_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "platemaker/platemaker_export.h"
#include "platemaker/models/canvas_profile.hpp"
#include "platemaker/models/processing_error.hpp"

// Forward declarations only (no include, so Models stays independent of Infrastructure). The
// project's profile-link fields are private; WorkspaceEditor is the sole runtime mutation authority
// (it enforces the guards), and WorkspaceSerializer installs them at load time.
namespace Platemaker::Infrastructure { class WorkspaceEditor; class WorkspaceSerializer; class ProjectEditor; }

namespace Platemaker::Models {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/**
 * \enum FileStatus
 * \brief Lifecycle status of a file tracked by a \c ProjectItem.
 */
enum class FileStatus {
    Pending,        //!< New file, not yet processed.
    Processed,      //!< File processed and its hash matches the stored hash.
    Modified,       //!< File exists on disk but its content has changed since last processing.
    Missing,        //!< File was registered but cannot be found on disk.
    Desynchronized, //!< Output is out-of-sync with the current input set.
    Done,           //!< Output slice is up-to-date with the current input set and workspace config.
    Skipped,        //!< Input the last render could not include: the file is missing or failed to
                    //!< load. A size mismatch is NOT a skip — an unmatched page is rendered without
                    //!< margins and ends up Processed with an empty canvasProfileId. Sticky across
                    //!< sanitize() while the file is unchanged. Distinct from Missing, which is a
                    //!< pre-render disk check, and from Pending, which was never rendered.
    Error           //!< Input was rendered, but its content hash could not be computed afterwards
                    //!< (locked file / denied permission / offline drive). The output exists; the
                    //!< verification baseline does not. Sticky across sanitize() like Skipped and it
                    //!< does NOT force a re-render — otherwise the project would silently reprocess
                    //!< everything on every run forever. Recovers to Processed once the file hashes.
};

// ---------------------------------------------------------------------------
// Project data types
// ---------------------------------------------------------------------------

/**
 * \brief Describes the contribution of one source file to a single output slice.
 *
 * One \c SourceSegment is stored per source image that contributed pixels to a
 * \c SliceResult or \c OutputFile.  This provenance information drives
 * incremental processing.
 */
struct SourceSegment {
    std::string sourceFilePath; //!< Absolute path of the scaled source image.
    int         srcY   = 0;     //!< Y offset within the scaled source where this segment starts.
    int         height = 0;     //!< Number of pixels taken from this source for this slice.
};

/**
 * \brief An input image file tracked by a \c ProjectItem.
 */
struct InputFile {
    std::string uid;                     //!< Local unique id for this entry (e.g. "file-<hex>"). Not an RFC 4122 UUID.
    std::string filePath;                //!< Absolute path on disk.
    std::string sha256;                  //!< SHA-256 hex digest from the last processing run.
    int         order        = 0;        //!< 0-based position in the virtual strip.
    std::string thumbnailPath;           //!< Path inside `.platemaker-cache/` (not persisted by convention).
    FileStatus  status       = FileStatus::Pending; //!< Current lifecycle status.
    std::string lastProcessed;           //!< ISO 8601 timestamp of the last processing run.
    std::vector<std::string> contributesTo; //!< Output file names produced from this input.

    /**
     * \brief Id of the canvas profile applied to this page at the last render.
     *
     * Empty means no profile matched (the page was skipped) or the project has no
     * canvas profiles at all.  Not needed for staleness detection — \c canvasFingerprint
     * already catches a profile swap — but kept so the UI can say *which* profile was
     * used instead of showing an opaque fingerprint.
     */
    std::string canvasProfileId;

    /**
     * \brief \c canvasRenderFingerprint() of the profile applied at the last render.
     *
     * The record of what was actually applied to *this* page, exactly like \c sha256
     * records its content.  Compared against the current profile on the next run: a
     * mismatch means this page's output is stale even though its file never changed.
     * Empty when no profile was applied.
     */
    std::string canvasFingerprint;

    /**
     * \brief Display dimensions of this page at the last render, in pixels (0 = unknown).
     *
     * Recorded in the same coordinate space canvas matching uses — post-autorot display W×H,
     * exactly the values the pipeline fed to \c CanvasProfileMatcher::resolve().  Canvas profiles
     * match purely by W×H, so storing them is what lets \c detectCanvasConfigChange() answer
     * *offline* "which profile would this page match now?" instead of blanket-invalidating the whole
     * project whenever the effective profile list changes.  Both zero means the page has not been
     * rendered since dimensions were tracked (legacy record) — the caller may backfill from the file
     * header, and staleness detection falls back to the coarse list comparison until it does.
     */
    int width  = 0;
    int height = 0;
};

/**
 * \brief An output slice file produced by a \c ProjectItem processing run.
 */
struct OutputFile {
    std::string uid;      //!< Local unique id for this entry (e.g. "out-3"). Not an RFC 4122 UUID.
    std::string fileName; //!< Filename only (e.g. "output_001.png").
    std::string sha256;   //!< SHA-256 hex digest of the generated file.
    std::vector<SourceSegment> sourceMap; //!< Input contributions for this slice.
    FileStatus  status = FileStatus::Done; //!< Current lifecycle status.
};

// ---------------------------------------------------------------------------
// ProcessingSliceRecord
// ---------------------------------------------------------------------------

/**
 * \brief A processed slice descriptor passed to \c ProjectItem::applyProcessingResults().
 *
 * This is the Models-layer representation of one output slice — it carries
 * the same provenance data as \c Core::SliceResult but contains no pixel
 * buffer, keeping \c ProjectItem free from any Core or image-processing
 * dependency.
 *
 * The calling layer (CLI / GUI) is responsible for constructing these records
 * from \c Core::SliceResult objects after saving the pixel data to disk.
 */
struct ProcessingSliceRecord {
    std::string                fileName;     //!< Output filename, e.g. "output_001.png".
    std::string                outputSha256; //!< SHA-256 of the saved output file (may be empty).
    std::vector<SourceSegment> sourceMap;    //!< Provenance: one entry per contributing source file.
};

// ---------------------------------------------------------------------------
// AppliedCanvasProfile
// ---------------------------------------------------------------------------

/**
 * \brief Which canvas profile a processing run actually applied to one input.
 *
 * Editing a profile leaves both the input file and the output file byte-identical, so
 * no hash can notice that a page went stale.  This is the trace that makes it
 * detectable: the pipeline reports what it applied, \c applyProcessingResults() stores
 * it on the \c InputFile, and \c detectCanvasConfigChange() compares it next time.
 *
 * Lives in Models — like \c ProcessingSliceRecord — so \c ProjectItem can consume it
 * without depending on Core.
 */
struct AppliedCanvasProfile {
    std::string sourceFilePath; //!< Input this refers to.
    std::string profileId;      //!< Profile that matched ("" = none matched / project has no profiles).
    std::string fingerprint;    //!< canvasRenderFingerprint() of it ("" when profileId is empty).
    int         width  = 0;     //!< Display width the run resolved against (0 = not recorded).
    int         height = 0;     //!< Display height the run resolved against (0 = not recorded).
};

// ---------------------------------------------------------------------------
// ScanMergeResult
// ---------------------------------------------------------------------------

/**
 * \brief Canvas-profile staleness detected by \c ProjectItem::detectCanvasConfigChange().
 *
 * A page's output depends on the canvas profile applied to it (margins are cropped
 * away before scaling), but editing a profile touches neither the input files nor the
 * output files — so hashes alone can never notice it.  This is what notices.
 */
struct CanvasConfigChange {
    /// The effective profile list itself changed (added / removed / reordered).
    /// Coarse by nature: it can flip which profile a page matches, or make a
    /// previously-skipped page match, so it degrades to a full re-render.
    bool listChanged = false;

    /// Paths of inputs whose applied profile changed content (e.g. margins edited).
    /// Precise: only these pages — and whatever the layout shift below them touches —
    /// actually need redrawing.
    std::vector<std::string> changedInputs;

    /// True when anything at all is stale.
    [[nodiscard]] bool any() const noexcept { return listChanged || !changedInputs.empty(); }
};

/**
 * \brief Result returned by \c ProjectItem::mergeFileScan().
 *
 * Summarises the changes detected when a new directory scan is merged into
 * the existing input file list.
 */
struct ScanMergeResult {
    std::vector<std::string> added;              //!< Absolute paths of newly added files.
    std::vector<std::string> renamed;            //!< Absolute paths of renamed files (same content).
    std::vector<std::string> removed;            //!< Absolute paths of files no longer present.
    bool outputsInvalidated = false;             //!< True when outputs require a full reprocess.
};

// ---------------------------------------------------------------------------
// ProjectItem class
// ---------------------------------------------------------------------------

/**
 * \class ProjectItem
 * \brief Represents a single comic chapter — the primary unit of work.
 *
 * \c ProjectItem is **move-only** to prevent accidental copies of large
 * collections.  Use \c std::move() when passing to containers.
 *
 * ### Typical usage (incremental processing)
 * \code
 * project.sanitize(ws.canvasProfiles());    // refresh statuses (disk + config)
 * if (!project.isUpToDate()) {
 *     auto outcome = pipeline.run(...);     // Core layer
 *     project.applyProcessingResults(outcome.records, outcome.appliedProfiles,
 *                                    outcome.skippedPages, ws.canvasProfiles(), outDir, now);
 * }
 * \endcode
 *
 * \note The non-serialised lookup tables (\c m_inputToOutputLookup and
 *       \c m_sha256Index) are NOT written to JSON.  They must be rebuilt
 *       after every deserialisation via \c rebuildLookupTables().
 */
class PLATEMAKER_EXPORT ProjectItem {
public:
    // -----------------------------------------------------------------------
    // Public serialised fields
    // -----------------------------------------------------------------------
    std::string name;           //!< Human-readable chapter name (e.g. "Chapter 01").
    std::string uid;            //!< Local unique id (e.g. "proj-<hex>"), minted by makeUniqueId. Not an RFC 4122 UUID.
    std::string inputDirectory; //!< Absolute path of the folder inputs were last scanned from. NOT
                                //!< authoritative for the input list (each InputFile carries its own
                                //!< full path; inputs may come from several folders). Two defined
                                //!< uses: the CLI matches an existing project by this directory
                                //!< (`platemaker process <dir>`), and the GUI pre-opens it in the
                                //!< add-from-directory dialog. May be empty (never scanned a folder).

    /**
     * \brief Ids of canvas profiles linked to this project (empty = all workspace profiles accepted).
     *
     * Read-only view. The backing vector is private and mutated only through the guarded
     * \c addCanvasProfile() / \c WorkspaceEditor::removeCanvasProfileFromProject(), so a caller
     * cannot bypass the "at most one profile per canvas W×H" rule (SPECIFICATION.md §7.5.2) with a
     * raw \c push_back.
     */
    [[nodiscard]] const std::vector<std::string>& canvasProfileIds() const noexcept { return m_canvasProfileIds; }

    /**
     * \brief Id of the output profile linked to this project (empty = workspace default).
     *
     * Read-only view. Set only through \c WorkspaceEditor::setProjectOutputProfile(), which validates
     * that the id resolves to a user profile or a preset — a raw write of an unknown id is not possible.
     */
    [[nodiscard]] const std::string& outputProfileId() const noexcept { return m_outputProfileId; }

    /// Signature of the output-profile configuration that produced the current
    /// outputs (see \c outputProfileSignature()).  Empty until the first render.
    /// A mismatch against the current profile means the outputs are stale and a
    /// full re-render is required (e.g. the output format or slice height changed).
    ///
    /// Covers the output profile **only** — canvas-profile staleness is tracked
    /// per input via \c InputFile::canvasFingerprint, because it invalidates only
    /// the pages that profile applies to rather than the whole project.  It is also
    /// the only mechanism that catches changes provenance cannot see: switching
    /// PNG→JPEG or nudging quality leaves the geometry (and thus every sourceMap)
    /// identical while changing every output byte.
    std::string outputSignature;

    /// Effective canvas-profile ids at the last render, in effective order (see
    /// \c effectiveCanvasProfileIds()).  Empty until the first render.
    ///
    /// Per-input fingerprints cannot catch a page that previously matched **no**
    /// profile and now matches a newly added one — there is nothing to compare
    /// against.  Comparing the list itself closes that hole: any add / remove /
    /// reorder degrades to a full re-render.
    std::vector<std::string> canvasProfileIdsAtRender;

    /// Input \c uid sequence, in strip (\c order) order, at the last render.  Empty until the
    /// first render.  The input-composition baseline: the strip is a continuous concatenation, so
    /// reordering, adding or removing an input shifts pixels across every downstream slice while
    /// leaving each input file byte-identical — no hash notices.  Comparing this against the current
    /// \c order-sorted uids (\c detectInputCompositionChange()) is what surfaces it.  Keyed by \c uid
    /// (not path) so a rename — same content, same position — does not false-invalidate.
    std::vector<std::string> inputOrderAtRender;

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /// Default constructor — creates an empty, unprocessed project.
    ProjectItem() noexcept;

    ~ProjectItem() = default;

    ProjectItem(const ProjectItem&)            = delete;
    ProjectItem& operator=(const ProjectItem&) = delete;

    /// Move constructor — transfers ownership and runtime lookup tables.
    ProjectItem(ProjectItem&& other) noexcept;

    /// Move assignment — transfers ownership and runtime lookup tables.
    ProjectItem& operator=(ProjectItem&& other) noexcept;

    // -----------------------------------------------------------------------
    // Accessors (const + non-const overloads for nlohmann/json compatibility)
    // -----------------------------------------------------------------------

    /**
     * \brief Returns the list of input files.
     * \return Mutable reference for non-const objects (used by serializer).
     */
    [[nodiscard]] std::vector<InputFile>& getInputImages() noexcept;

    /// \overload Const accessor for read-only contexts.
    [[nodiscard]] const std::vector<InputFile>& getInputImages() const noexcept;

    /**
     * \brief Returns a copy of the input files sorted ascending by \c InputFile::order.
     *
     * The strip is built in \c order sequence, which is *not* necessarily the stored vector
     * order — a reorder changes only the \c order field, never the physical \c m_input_images
     * layout (that is what keeps a reorder from churning the project structure).  Render callers
     * pass this to \c ProcessingPipeline::run so the pipeline stays a pure "render the sequence I
     * am handed" component with no knowledge of \c order.
     */
    [[nodiscard]] std::vector<InputFile> inputsInOrder() const;

    /**
     * \brief Returns the list of output files.
     * \return Mutable reference for non-const objects (used by serializer).
     */
    [[nodiscard]] std::vector<OutputFile>& getOutputImages() noexcept;

    /// \overload Const accessor for read-only contexts.
    [[nodiscard]] const std::vector<OutputFile>& getOutputImages() const noexcept;

    /**
     * \brief Returns the output directory path.
     * \return Mutable reference for non-const objects (used by serializer).
     */
    [[nodiscard]] std::string& getOutputDirectory() noexcept;

    /// \overload Const accessor for read-only contexts.
    [[nodiscard]] const std::string& getOutputDirectory() const noexcept;

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    /**
     * \brief Returns \c true when all input files are \c Processed and their
     *        SHA-256 hashes match the stored values.
     *
     * Only valid after calling \c sanitize().
     */
    [[nodiscard]] bool isUpToDate() const noexcept;

    /**
     * \brief Returns the output file names that were produced from \p filePath.
     *
     * Uses the runtime lookup table built by \c rebuildLookupTables() /
     * \c applyProcessingResults().  Returns an empty vector if the file is
     * unknown or was not yet processed.
     *
     * \param filePath Absolute path of the input file.
     * \return Const reference to the list of output file names.
     */
    [[nodiscard]] const std::vector<std::string>& outputsForInput(
        const std::string& filePath) const noexcept;

    /**
     * \brief Returns the file names of output slices that are not \c Done.
     *
     * Valid after \c sanitize().  These are the slices a partial re-render must
     * regenerate (status \c Missing or \c Modified).
     */
    [[nodiscard]] std::vector<std::string> dirtyOutputNames() const;

    /**
     * \brief Returns \c true if there is at least one input and every input is
     *        \c Processed (i.e. no input is Pending/Modified/Missing).
     *
     * Valid after \c sanitize().  When this is true but \c isUpToDate() is
     * false, only outputs are dirty — a partial re-render suffices.
     */
    [[nodiscard]] bool inputsAllProcessed() const noexcept;

    /**
     * \brief Returns the canvas profile ids this project effectively renders with.
     *
     * Mirrors \c CanvasProfileMatcher's rule: the project's own \c canvasProfileIds
     * when set, otherwise **every** workspace profile (an empty per-project list means
     * "accept all").  Ids listed but absent from \p workspaceProfiles are dropped —
     * they match nothing and so cannot affect a render.
     *
     * \param workspaceProfiles The full workspace palette.
     * \return Ids in effective order.
     */
    [[nodiscard]] std::vector<std::string> effectiveCanvasProfileIds(
        const std::vector<CanvasProfile>& workspaceProfiles) const;

    /**
     * \brief Detects canvas-profile edits that invalidate this project's outputs.
     *
     * Compares each input's recorded \c canvasFingerprint against the current profile
     * of the same id, and the effective profile list against
     * \c canvasProfileIdsAtRender.  Neither the input files nor the output files change
     * when a profile is edited, so no hash notices — without this the project would
     * report itself up to date while its outputs are stale.
     *
     * Only meaningful after a first render — a project with no outputs reports no
     * change, having no baseline to compare against (and Pending inputs already force
     * a full run).
     *
     * \param workspaceProfiles The full workspace palette.
     * \return What changed; see \c CanvasConfigChange.
     */
    [[nodiscard]] CanvasConfigChange detectCanvasConfigChange(
        const std::vector<CanvasProfile>& workspaceProfiles) const;

    /**
     * \brief Detects an input-composition change (reorder / add / remove) that invalidates outputs.
     *
     * Compares the current \c order-sorted input uids against \c inputOrderAtRender.  This catches
     * what hashes and the canvas axis cannot: reordering inputs shifts the continuous strip so every
     * downstream slice changes, yet no input or output file changes on disk.
     *
     * Only meaningful after a first render (a project with no outputs, or no baseline, reports no
     * change — Pending inputs already force a full run, and \c load() backfills the baseline of a
     * pre-existing project from output provenance).  A hit degrades the whole project to a full
     * re-render: fold it into the caller's \c configChanged so the *full* path runs and refreshes the
     * baseline (the partial path leaves it stale, which would re-render forever).
     *
     * \return \c true when the input order/composition differs from the last render.
     */
    [[nodiscard]] bool detectInputCompositionChange() const;

    // -----------------------------------------------------------------------
    // Operations
    // -----------------------------------------------------------------------

    /**
     * \brief Refreshes the status of every tracked file against disk *and* config.
     *
     * For each \c InputFile:
     * - \c Missing   — file no longer exists on disk.
     * - \c Modified  — file exists but its SHA-256 differs from the stored hash.
     * - \c Processed — file exists and its SHA-256 matches.
     * - \c Pending   — file exists but has never been hashed (empty sha256).
     *
     * For each \c OutputFile (relative to \c m_output_directory):
     * - \c Missing   — slice file no longer exists on disk.
     * - \c Modified  — slice exists but its SHA-256 differs from the stored hash.
     * - \c Done      — slice exists and (if hashed) matches.
     *
     * Finally, pages whose canvas profile changed since the render that produced them
     * — or was never recorded at all — are marked \c Desynchronized, along with the
     * outputs they fed.  A profile edit changes neither the input nor the output file,
     * so hashes alone can never notice it; this is what surfaces it (and what makes the
     * GUI colour those tiles "out of sync").  See \c detectCanvasConfigChange().
     *
     * Sets the internal up-to-date flag to \c false if any input is not
     * \c Processed or any output is not \c Done.
     *
     * \param workspaceProfiles The canvas profiles currently in effect. Required rather
     *        than optional: an overload without it could be called by accident and would
     *        silently skip the config check — exactly the class of bug this detects.
     * \return \c true if every input is \c Processed and every output is \c Done.
     */
    bool sanitize(const std::vector<CanvasProfile>& workspaceProfiles);

    /**
     * \brief Rebuilds the non-serialised runtime lookup tables from the
     *        current \c m_input_images and \c m_output_images data.
     *
     * Must be called:
     *  - After every deserialisation (e.g. in \c WorkspaceSerializer::load()).
     *  - After \c applyProcessingResults() (called internally).
     *  - After \c mergeFileScan() (called internally).
     *
     * Rebuilds:
     *  - \c m_inputToOutputLookup — filePath → output file names.
     *  - \c m_sha256Index         — sha256   → current file path.
     */
    void rebuildLookupTables();

    /**
     * \brief Gives every input / output file a non-empty, unique \c uid, minting one where it is
     *        missing or duplicated.
     *
     * The uids are short local identifiers (\c "file-<hex>" / \c "out-N"), not RFC 4122 UUIDs.  This
     * repairs two cases: a workspace written under the old \c "uuid" key (no longer read) arrives with
     * empty uids; and the historical position-derived scheme (\c "file-" + index) could hand the same
     * id to two files across re-scans.  Idempotent — an already-unique set is left untouched, so
     * existing ids stay stable.  Called after deserialisation (\c load()) and after \c mergeFileScan().
     */
    void ensureUniqueFileUids();

    /**
     * \brief Applies the results of a pipeline run to all tracked records.
     *
     * Updates each \c InputFile with its current SHA-256 hash, sets status
     * to \c Processed, fills \c contributesTo from the provenance data in
     * \p records, records the canvas profile applied to each page from
     * \p appliedProfiles, rebuilds the \c OutputFile list and the runtime lookup
     * tables.
     *
     * \param appliedProfiles What the run applied per input; establishes the baseline
     *                        \c detectCanvasConfigChange() compares against later.
     *                        Also captures \c canvasProfileIdsAtRender.
     *
     * This method centralises all post-processing bookkeeping that was
     * previously scattered across the CLI layer.
     *
     * \param records           One record per saved output file, in order.
     * \param appliedProfiles   One entry per input the run considered.
     * \param skippedInputPaths Absolute paths the run did not include (no matching/linked canvas
     *                          profile, or a load error — i.e. \c ProcessingOutcome::skippedPages).
     *                          These inputs are marked \c FileStatus::Skipped instead of
     *                          \c Processed, so a page the render left out does not masquerade as
     *                          done.
     * \param workspaceProfiles The full workspace palette, to capture
     *                          \c canvasProfileIdsAtRender via
     *                          \c effectiveCanvasProfileIds().
     * \param outputDirectory   Absolute path where output files were written.
     * \param timestamp         ISO 8601 string for \c InputFile::lastProcessed.
     *
     * \return The typed failures that happened *after* a successful render — one per input whose
     *         content hash could not be computed (code \c InputHashFailed, category \c Io). Such an
     *         input is set to \c FileStatus::Error rather than left \c Pending, so it no longer
     *         silently forces a full re-render on every subsequent run. Empty on a clean apply. The
     *         caller should surface these (CLI print / GUI log + tile) — ignoring them re-hides the
     *         very failure this reports.
     */
    [[nodiscard]] std::vector<ProcessingError> applyProcessingResults(
        const std::vector<ProcessingSliceRecord>& records,
        const std::vector<AppliedCanvasProfile>&  appliedProfiles,
        const std::vector<std::string>&           skippedInputPaths,
        const std::vector<CanvasProfile>&         workspaceProfiles,
        const std::string&                        outputDirectory,
        const std::string&                        timestamp);

    /**
     * \brief Applies the results of a *partial* re-render (only the dirty
     *        output slices were regenerated).
     *
     * For each record, the matching \c OutputFile (by \c fileName) has its
     * SHA-256 and provenance refreshed and its status reset to \c Done.  Inputs
     * are left untouched (they were unchanged), and the output list is not
     * rebuilt.  Updates the up-to-date flag based on the remaining output
     * statuses.
     *
     * \param records One record per regenerated output file.
     */
    void applyPartialResults(
        const std::vector<ProcessingSliceRecord>& records);

    /**
     * \brief Merges a new directory scan into the current input file list.
     *
     * Replaces \c m_input_images with a new ordered list derived from
     * \p newFilePaths while maximally preserving existing incremental-
     * processing data:
     *
     * - Files matched **by path**: existing record is kept as-is.
     * - Files matched **by SHA-256** (same content, new path): treated as a
     *   rename; path is updated but status / hash / contributesTo are kept.
     *   No output invalidation is triggered for pure renames at the same
     *   strip position.
     * - Files with no match: inserted as new \c Pending entries.
     * - Old files no longer present: removed.
     *
     * If any structural change is detected (file added, removed, or
     * reordered at a different strip position), all \c OutputFile entries are
     * marked \c Desynchronized and \c ScanMergeResult::outputsInvalidated is
     * set to \c true.
     *
     * \param newFilePaths Absolute paths from the new directory scan, in
     *                     the desired strip order (typically sorted by name).
     * \return A \c ScanMergeResult describing what changed.
     */
    ScanMergeResult mergeFileScan(const std::vector<std::string>& newFilePaths);

    /**
     * \brief Links a canvas profile to this project, with a conflict guard.
     *
     * Adds \p profileId to \c canvasProfileIds if:
     *  - The ID exists in \p workspaceProfiles.
     *  - No already-linked profile shares the same canvas W×H (SPECIFICATION.md §7.5.2).
     *
     * Calling with an already-linked ID is a no-op (idempotent, returns true).
     *
     * \param workspaceProfiles  The complete palette from \c Workspace::canvasProfiles.
     * \param profileId          \c CanvasProfile::id to link.
     * \return \c true on success, \c false if the ID was not found or caused a conflict.
     */
    bool addCanvasProfile(
        const std::vector<CanvasProfile>& workspaceProfiles,
        const std::string& profileId);

private:
    // WorkspaceEditor is the sole runtime authority allowed to write the profile-link fields below
    // (it applies the validation / dimension guard); WorkspaceSerializer installs them at load time;
    // ProjectEditor writes them back when restoring a project snapshot for undo/redo (a previously
    // valid state, so no re-validation is needed — the same friend path load() uses).
    friend class Platemaker::Infrastructure::WorkspaceEditor;
    friend class Platemaker::Infrastructure::WorkspaceSerializer;
    friend class Platemaker::Infrastructure::ProjectEditor;

    std::vector<std::string> m_canvasProfileIds; //!< Ids of linked canvas profiles (see canvasProfileIds()).
    std::string              m_outputProfileId;  //!< Id of the linked output profile (see outputProfileId()).

    /// The current input uids in strip (\c order) order — the value captured as
    /// \c inputOrderAtRender and compared by \c detectInputCompositionChange().
    [[nodiscard]] std::vector<std::string> orderedInputUids() const;

    std::vector<InputFile>  m_input_images;     //!< Ordered input image list.
    std::vector<OutputFile> m_output_images;    //!< Output slice list from last run.
    std::string             m_output_directory; //!< Absolute path for output slices.

    bool m_isUpToDate = false; //!< Updated by sanitize() / applyProcessingResults().

    // -----------------------------------------------------------------------
    // Runtime lookup tables (NOT serialised)
    // -----------------------------------------------------------------------

    /**
     * \brief Maps input file path → output file names it contributed to.
     *
     * Rebuilt by \c rebuildLookupTables().  Not written to JSON.
     */
    std::unordered_map<std::string, std::vector<std::string>> m_inputToOutputLookup;

    /**
     * \brief Maps SHA-256 hex digest → current absolute file path.
     *
     * Used in \c mergeFileScan() to detect renames (file with known hash
     * has moved to a new path).  Not written to JSON.
     */
    std::unordered_map<std::string, std::string> m_sha256Index;
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PROJECT_ITEM_HPP
