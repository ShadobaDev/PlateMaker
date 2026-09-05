/**
 * \file lib/include/platemaker/models/project_reports.hpp
 * \brief What ProjectItem's staleness and re-scan queries answer with.
 *
 * Two small report types, each returned by one \c ProjectItem query.  Kept apart from the
 * render's own results (\c processing_results.hpp) because they describe what *changed* about a
 * project's configuration or its files on disk, not what a render produced.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_PROJECT_REPORTS_HPP
#define PLATEMAKER_MODELS_PROJECT_REPORTS_HPP

#include <string>
#include <vector>

namespace Platemaker::Models {

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
    [[nodiscard]] bool anyChanged() const noexcept { return listChanged || !changedInputs.empty(); }
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

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PROJECT_REPORTS_HPP
