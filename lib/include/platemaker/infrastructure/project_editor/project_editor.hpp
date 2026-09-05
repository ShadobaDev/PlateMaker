/**
 * \file lib/include/platemaker/infrastructure/project_editor/project_editor.hpp
 * \brief ProjectEditor — the authority for mutating a single project's content (input ordering).
 *
 * Twin of \c WorkspaceEditor: where that facade owns workspace-level palette mutation, this one owns
 * a single \c Models::ProjectItem's content.  Today that is input ordering; it is the natural home for
 * input add / remove / rescan (currently \c ProjectItem::mergeFileScan) as those migrate here.
 *
 * The strip is built in \c InputFile::order sequence, not in the stored-vector order.  Reordering
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

private:
    Models::ProjectItem& m_project;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_PROJECT_EDITOR_HPP
