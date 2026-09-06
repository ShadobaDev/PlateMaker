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
 * The operations that *drive* it live on \c Infrastructure::ProjectEditor, because each is
 * governed by state outside the project — the disk, the workspace's canvas palette, a render's
 * output.  What lives here is the entity they act on and the \c const queries that describe it:
 * \c detectStaleness() answers "does this need rendering, and why" in one report.
 *
 * \code
 * ProjectEditor{project}.sanitize(ws.canvasProfiles());   // refresh statuses (disk + config)
 * const auto stale = project.detectStaleness(ws.canvasProfiles(), outProfile);
 * if (stale.needsRender()) { ... }
 * \endcode
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
#include "platemaker/models/output_profile.hpp"
#include "platemaker/models/processing_error.hpp"
#include "platemaker/models/processing_steps.hpp"
#include "platemaker/models/project_files.hpp"
#include "platemaker/models/processing_results.hpp"
#include "platemaker/models/project_reports.hpp"

// Forward declarations only (no include, so Models stays independent of Infrastructure). The
// project's profile-link fields are private; WorkspaceEditor is the sole runtime mutation authority
// (it enforces the guards), and WorkspaceSerializer installs them at load time.
namespace Platemaker::Infrastructure { class WorkspaceEditor; class WorkspaceSerializer; class ProjectEditor; }

namespace Platemaker::Models {


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
 * ProjectEditor editor{project};
 * editor.sanitize(ws.canvasProfiles());     // refresh statuses (disk + config)
 * if (project.detectStaleness(ws.canvasProfiles(), outProfile).needsRender()) {
 *     auto outcome = ProcessingPipeline::render(request, cancel);   // Core layer
 *     editor.applyProcessingResults(outcome.records, outcome.appliedProfiles,
 *                                   outcome.skippedPages, ws.canvasProfiles(), outDir, now);
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

    /// \c processingConfigSignature() of the colour-correction + overlay config at the last render.
    /// Empty until the first render, and empty whenever no processing is configured (so it matches a
    /// pre-feature workspace and never forces a needless re-render).  A mismatch against the current
    /// config means the outputs are stale for a reason no file hash can see — enabling/adjusting the
    /// grade or moving an overlay leaves every input and output file untouched — so the caller folds it
    /// into its full-re-render decision, exactly like \c outputSignature does for the output profile.
    std::string processingSignature;

    /// Optional project-wide colour correction applied per input page at render (page domain).
    /// Disabled by default — an untouched project renders byte-identically.  See \c processing_steps.hpp.
    ColourCorrection colourCorrection;

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
     * order — a reorder changes only the \c order field, never the physical \c m_inputImages
     * layout (that is what keeps a reorder from churning the project structure).  Render callers
     * pass it as \c RenderRequest::inputs so the pipeline stays a pure "render the sequence I
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
    // Strip overlays (text/bubbles) — a resource inventory parallel to the input files
    // -----------------------------------------------------------------------

    /**
     * \brief The project's strip overlays (text/bubble placements + their bitmaps).
     * \return Mutable reference for non-const objects (used by the serializer / undo snapshot).
     *
     * Prefer \c addOverlay() / \c removeOverlay() for mutation — they own the inventory (uid minting,
     * SHA-256, dedup). This mutable accessor mirrors \c getInputImages() and exists for the codec.
     */
    [[nodiscard]] std::vector<StripOverlay>& getStripOverlays() noexcept;

    /// \overload Const accessor for read-only contexts (render feed, staleness signature).
    [[nodiscard]] const std::vector<StripOverlay>& getStripOverlays() const noexcept;

    /**
     * \brief Registers a consumer-created overlay bitmap and returns the new overlay's uid.
     *
     * The library owns the inventory, exactly as it does for input files: this mints a unique
     * \c "ovl-<hex>" uid, computes the bitmap's SHA-256, and **dedups by content** — if an existing
     * overlay already references a bitmap with the same hash, the new placement reuses that stored path
     * so identical content is kept once (the same sha-identity mechanic \c mergeFileScan() uses for
     * input renames). The bitmap file itself is created and owned by the consumer (like an input file);
     * the library never copies it.
     *
     * \param bitmapPath    Absolute path to the RGBA bitmap the consumer rasterised. Must exist.
     * \param x,y            Top-left placement — page-relative when \p anchorInputUid is given, else in
     *                      absolute strip coordinates (see \c StripOverlay::anchorInputUid).
     * \param blend          Blend mode (default \c Over).
     * \param anchorInputUid The \c InputFile::uid of the page this overlay rides on. Prefer passing it:
     *                      an anchored overlay follows its page when the chapter is edited, while an
     *                      absolute one silently drifts onto different artwork as soon as anything above
     *                      it changes height. Empty (the default) keeps the absolute placement.
     * \return The minted uid. If the file cannot be hashed the overlay is still added with an empty
     *         sha256 (no dedup, and staleness cannot see a later content change until it is re-added).
     */
    std::string addOverlay(const std::string& bitmapPath, int x, int y,
                           BlendMode blend = BlendMode::Over,
                           const std::string& anchorInputUid = {});

    /**
     * \brief Removes the overlay with \p overlayUid.
     * \return \c true if an overlay was removed, \c false if \p overlayUid was not found.
     */
    bool removeOverlay(const std::string& overlayUid);

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

    /**
     * \brief Every reason this project's outputs might be stale, in one answer.
     *
     * The single place the "does it need rendering, and why" rule lives.  A consumer used to
     * assemble it out of five separate calls plus two signature comparisons of its own — and both
     * consumers did, in near-identical copies, so the rule existed twice outside the library that
     * owns it.
     *
     * \note Call \c ProjectEditor::sanitize() first.  \c StalenessReport::contentDirty reports
     *       what that pass recorded and is not recomputed here, because deciding it means hashing
     *       every input and output — far too expensive for a query used to draw a status.
     *
     * \param workspaceProfiles The workspace's canvas-profile palette, as the render will see it
     *                          (pass an empty list to mirror a profile-less render).
     * \param outputProfile     The **resolved** output profile this project would render with —
     *                          resolved by the caller, since a preset id resolves against the
     *                          catalogue rather than the workspace.
     * \return The per-axis report; see \c StalenessReport::needsRender().
     */
    [[nodiscard]] StalenessReport detectStaleness(
        const std::vector<CanvasProfile>& workspaceProfiles,
        const OutputProfile&              outputProfile) const;

    /**
     * \brief Rebuilds the non-serialised runtime lookup tables from the
     *        current \c m_inputImages and \c m_outputImages data.
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

    std::vector<InputFile>    m_inputImages;     //!< Ordered input image list.
    std::vector<OutputFile>   m_outputImages;    //!< Output slice list from last run.
    std::vector<StripOverlay> m_stripOverlays;    //!< Overlay inventory (see getStripOverlays() / addOverlay()).
    std::string               m_outputDirectory; //!< Absolute path for output slices.

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
