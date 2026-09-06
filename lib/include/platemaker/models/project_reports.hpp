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

/**
 * \brief Every reason a project's outputs might no longer match its configuration.
 *
 * Returned whole by \c ProjectItem::detectStaleness(), which is the single answer to
 * "does this need rendering, and why".  Before it existed, each consumer assembled the same
 * five-term disjunction itself — the CLI and the GUI carried near-identical copies, and only
 * three of the five axes were library code at all.  Two copies of one rule in two repositories
 * is one place for a sixth axis to be forgotten, and the forgotten copy ships stale output
 * without saying anything.
 *
 * The axes are kept separate rather than collapsed to a single flag because a consumer needs
 * more than the verdict: the CLI prints *which* setting moved, and the GUI's warning depends on
 * whether existing files will be replaced or merely overwritten.
 */
struct StalenessReport {
    /// False when the project has never rendered — there are no outputs to invalidate, so no
    /// configuration axis can make it stale (the inputs being Pending is what forces the run).
    bool hasOutputs = false;

    /**
     * \brief The file-content verdict recorded by the **last** \c sanitize().
     *
     * This one axis is not recomputed here: deciding it means hashing every input and output,
     * which is far too expensive for a query a consumer calls to draw a status. Call
     * \c ProjectEditor::sanitize() first — every caller already does, because the statuses it
     * writes are what the UI renders.
     */
    bool contentDirty = false;

    bool outputProfileChanged = false; //!< Stored \c outputSignature differs from the current profile's.

    /**
     * \brief The recorded slice extension is not the one the current format writes.
     *
     * Deliberately separate from \c outputProfileChanged: a project rendered before signatures
     * existed has no stored signature to compare, but its file names still betray a format switch.
     * It is also the one axis that guarantees *every* existing output is orphaned, since the new
     * settings cannot produce any of the old names.
     */
    bool outputFormatChanged = false;

    CanvasConfigChange canvas;               //!< Per-page canvas-profile staleness (precise).
    bool inputCompositionChanged = false;    //!< Inputs added / removed / reordered — shifts the whole strip.
    bool processingStepsChanged  = false;    //!< Colour correction or overlays differ from the render's.

    /**
     * \brief True when a *setting* changed, so every slice must be regenerated.
     *
     * Distinct from \c needsRender(): a project can need rendering purely because a file changed
     * on disk, which is ordinary work, whereas this means the previous output was produced under
     * rules that no longer apply.
     */
    [[nodiscard]] bool configurationChanged() const noexcept
    {
        return hasOutputs && (outputProfileChanged || outputFormatChanged || canvas.anyChanged()
                              || inputCompositionChanged || processingStepsChanged);
    }

    //! True when anything at all — content or configuration — requires a render.
    [[nodiscard]] bool needsRender() const noexcept
    {
        return contentDirty || configurationChanged();
    }
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_PROJECT_REPORTS_HPP
