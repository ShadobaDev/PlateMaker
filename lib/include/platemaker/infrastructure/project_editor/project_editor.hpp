/**
 * \file lib/include/platemaker/infrastructure/project_editor/project_editor.hpp
 * \brief ProjectEditor — the authority for mutating a single project's content.
 *
 * Twin of \c WorkspaceEditor: where that facade owns workspace-level palette mutation, this one owns
 * a single \c Models::ProjectItem's content — input ordering, the directory re-scan, refreshing file
 * statuses, and taking a render's results back in.
 *
 * The four whole-project operations arrived here from \c ProjectItem, which is where this header
 * always said they belonged.  They are driven by state outside the project — what is on disk, what
 * the workspace's canvas palette says, what a render just produced — rather than being questions an
 * entity answers about itself, and between them they were 440 of \c project_item.cpp's 940 lines.
 * What stayed behind is the entity: its containers, its identity, and the \c const queries that
 * describe it (including \c detectStaleness(), which \c sanitize() consumes).
 *
 * The strip is built in \c Models::InputFile::order sequence, not in the stored-vector order.  Reordering
 * therefore only rewrites the \c order field — the physical \c m_inputImages layout is never touched,
 * so a reorder does not churn the project structure.  \c ProjectItem::detectInputCompositionChange()
 * (compared in \c sanitize()) is what turns a reorder into stale outputs.
 *
 * A transient bound to a \c ProjectItem& for the duration of an edit; it holds no state beyond the
 * reference, so construct one where you need it and let it go.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-29
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_PROJECT_EDITOR_HPP
#define PLATEMAKER_INFRASTRUCTURE_PROJECT_EDITOR_HPP

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/processing_error.hpp>
#include <platemaker/models/project_item.hpp>

namespace Platemaker::Infrastructure {

/**
 * \class ProjectEditor
 * \brief Intent-level edits to a single \c Models::ProjectItem's content.
 */
class PLATEMAKER_EXPORT ProjectEditor {
public:
    /// Binds to \p project; the reference must outlive the editor.
    explicit ProjectEditor(Models::ProjectItem& project) noexcept : m_project(project) {}

    /**
     * \brief Sets the strip order of the inputs to \p orderedUids.
     *
     * Rewrites each input's \c order field to its position in \p orderedUids; the stored input vector
     * is **not** physically reordered. Light touch — statuses are not changed here (a reorder surfaces
     * as stale outputs at the next \c sanitize(), which the caller runs on Refresh / render / reopen).
     *
     * \param orderedUids The input uids in the desired strip order. Must be a permutation of the
     *                    project's current input uids (same set, each once).
     * \return \c true on success; \c false if \p orderedUids is not a permutation of the current
     *         input uids (in which case nothing is changed).
     */
    bool setInputOrder(const std::vector<std::string>& orderedUids);

    /**
     * \brief Moves one input up or down by one strip position.
     *
     * Convenience for the ▲/▼ buttons: resolves the current \c order sequence, swaps \p uid with its
     * neighbour, and applies the result through \c setInputOrder.
     *
     * \param uid   The input to move.
     * \param delta -1 to move up (earlier), +1 to move down (later).
     * \return \c true if the move happened; \c false if \p uid is unknown or already at the edge.
     */
    bool moveInput(const std::string& uid, int delta);

    // -----------------------------------------------------------------------
    // Snapshot / restore (undo/redo support)
    // -----------------------------------------------------------------------

    /**
     * \brief Serialises the bound project's full content to an opaque snapshot string.
     *
     * Captures everything a project owns — inputs, outputs, profile links, output-profile selection,
     * output directory, and the render baselines. Meant to be paired with \c restore(): a consumer
     * (e.g. a GUI undo command) keeps the string and hands it back to revert an edit. The format is
     * the same JSON the workspace file uses, but this is a *component* snapshot (one project), so it
     * is light — no whole-workspace copy. Compact (no pretty-printing) to keep it small in memory.
     */
    [[nodiscard]] std::string snapshot() const;

    /**
     * \brief Restores the bound project from a string produced by \c snapshot().
     *
     * Replaces the project's content in place and rebuilds its runtime lookup tables. The project's
     * \c name is **preserved** (it is workspace-owned — renamed through \c WorkspaceEditor, tracked on
     * the workspace undo timeline — so a project-scope restore must not resurrect an old name). Applies
     * the private profile-link fields through the friend path, exactly as \c load() does; the snapshot
     * was a previously valid state, so no re-validation is performed.
     *
     * \param snapshot A string previously returned by \c snapshot() on a project with the same uid.
     * \throws nlohmann::json::exception if \p snapshot is not valid project JSON.
     */
    void restore(const std::string& snapshot);


    // -----------------------------------------------------------------------
    // Whole-project operations
    // -----------------------------------------------------------------------
    //
    // These four moved off ProjectItem, which this header always said was their destination:
    // "it is the natural home for input add / remove / rescan (currently ProjectItem::mergeFileScan)
    // as those migrate here."  They are operations driven by *external* state — what is on disk,
    // what the workspace's profile palette says, what a render just produced — rather than questions
    // an entity answers about itself, and between them they were 440 of project_item.cpp's 940 lines.

    // -----------------------------------------------------------------------
    // Operations
    // -----------------------------------------------------------------------

    /**
     * \brief Refreshes the status of every tracked file against disk *and* config.
     *
     * For each \c Models::InputFile:
     * - \c Missing   — file no longer exists on disk.
     * - \c Modified  — file exists but its SHA-256 differs from the stored hash.
     * - \c Processed — file exists and its SHA-256 matches.
     * - \c Pending   — file exists but has never been hashed (empty sha256).
     *
     * For each \c Models::OutputFile (relative to \c m_outputDirectory):
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
    bool sanitize(const std::vector<Models::CanvasProfile>& workspaceProfiles);

    /**
     * \brief Applies the results of a pipeline run to all tracked records.
     *
     * Updates each \c Models::InputFile with its current SHA-256 hash, sets status
     * to \c Processed, fills \c contributesTo from the provenance data in
     * \p records, records the canvas profile applied to each page from
     * \p appliedProfiles, rebuilds the \c Models::OutputFile list and the runtime lookup
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
     *                          These inputs are marked \c Models::FileStatus::Skipped instead of
     *                          \c Processed, so a page the render left out does not masquerade as
     *                          done.
     * \param workspaceProfiles The full workspace palette, to capture
     *                          \c canvasProfileIdsAtRender via
     *                          \c effectiveCanvasProfileIds().
     * \param outputDirectory   Absolute path where output files were written.
     * \param timestamp         ISO 8601 string for \c Models::InputFile::lastProcessed.
     *
     * \return The typed failures that happened *after* a successful render — one per input whose
     *         content hash could not be computed (code \c InputHashFailed, category \c Io). Such an
     *         input is set to \c Models::FileStatus::Error rather than left \c Pending, so it no longer
     *         silently forces a full re-render on every subsequent run. Empty on a clean apply. The
     *         caller should surface these (CLI print / GUI log + tile) — ignoring them re-hides the
     *         very failure this reports.
     */
    [[nodiscard]] std::vector<Models::ProcessingError> applyProcessingResults(
        const std::vector<Models::ProcessingSliceRecord>& records,
        const std::vector<Models::AppliedCanvasProfile>&  appliedProfiles,
        const std::vector<std::string>&           skippedInputPaths,
        const std::vector<Models::CanvasProfile>&         workspaceProfiles,
        const std::string&                        outputDirectory,
        const std::string&                        timestamp);

    /**
     * \brief Applies the results of a *partial* re-render (only the dirty
     *        output slices were regenerated).
     *
     * For each record, the matching \c Models::OutputFile (by \c fileName) has its
     * SHA-256 and provenance refreshed and its status reset to \c Done.  Inputs
     * are left untouched (they were unchanged), and the output list is not
     * rebuilt.  Updates the up-to-date flag based on the remaining output
     * statuses.
     *
     * \param records One record per regenerated output file.
     */
    void applyPartialResults(
        const std::vector<Models::ProcessingSliceRecord>& records);

    /**
     * \brief Merges a new directory scan into the current input file list.
     *
     * Replaces \c m_inputImages with a new ordered list derived from
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
     * reordered at a different strip position), all \c Models::OutputFile entries are
     * marked \c Desynchronized and \c Models::ScanMergeResult::outputsInvalidated is
     * set to \c true.
     *
     * \param newFilePaths Absolute paths from the new directory scan, in
     *                     the desired strip order (typically sorted by name).
     * \return A \c Models::ScanMergeResult describing what changed.
     */
    Models::ScanMergeResult mergeFileScan(const std::vector<std::string>& newFilePaths);

private:
    Models::ProjectItem& m_project;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_PROJECT_EDITOR_HPP
