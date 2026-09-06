/**
 * \file lib/include/platemaker/core/processing_pipeline/render_request.hpp
 * \brief RenderRequest — everything one render needs, named rather than positional.
 *
 * This replaces the eleven positional arguments \c ProcessingPipeline::run() used to take, five of
 * them optional and defaulted, where a call site ended up reading as a column of values whose
 * meaning came from counting commas.
 *
 * Every field is held **by value**, which is not incidental: a render usually happens on a worker
 * thread while the caller keeps editing the workspace, so the request owning its data is what makes
 * that safe. It is a plain aggregate — designated initialisers or field assignment both work — and
 * it deliberately knows nothing about \c ProjectItem or \c Workspace, keeping the pipeline a pure
 * "render the sequence I am given".
 *
 * Every optional step is off in the default-constructed request, and off means *absent*, not
 * neutral: a disabled grade runs no colour code at all, and an empty overlay list composites
 * nothing. Output is byte-identical to a build without either step.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_PROCESSING_PIPELINE_RENDER_REQUEST_HPP
#define PLATEMAKER_CORE_PROCESSING_PIPELINE_RENDER_REQUEST_HPP

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/processing_steps.hpp>
#include <platemaker/models/project_item.hpp>

namespace Platemaker::Core {

/**
 * \struct RenderRequest
 * \brief The complete description of one render: what to render, how, and where to put it.
 */
struct PLATEMAKER_EXPORT RenderRequest {
    // -----------------------------------------------------------------------
    // What to render
    // -----------------------------------------------------------------------

    /**
     * \brief The project's input files, **already in strip order**.
     *
     * The pipeline stacks them in this vector's order and does not sort — pass
     * \c ProjectItem::inputsInOrder(), not the stored vector, or the strip comes out in whatever
     * order the files happened to be added. Entries with \c FileStatus::Missing are skipped and
     * reported.
     */
    std::vector<Models::InputFile> inputs;

    //! Target width, slice height, format, numbering and encoder options.
    Models::OutputProfile outputProfile;

    /**
     * \brief The workspace's full canvas-profile palette. Empty → no margin matching, plain scale.
     *
     * The *whole* palette, not just the project's: a page matching a profile that exists but is not
     * linked is rendered without margins **and reported**, so linking it is an offered one-click fix
     * rather than a silent difference.
     */
    std::vector<Models::CanvasProfile> canvasProfiles;

    //! Ids of the profiles linked to this project, in priority order. Empty → accept any match.
    std::vector<std::string> canvasProfileIds;

    // -----------------------------------------------------------------------
    // Where it goes
    // -----------------------------------------------------------------------

    //! Existing directory the slice files are written into. The pipeline does not create it.
    std::string outputDirectory;

    /**
     * \brief Optional thumbnail cache to pre-warm, rooted at this directory. Empty → none.
     *
     * Each saved slice's preview is written here from the **in-RAM** slice, before \c onSliceSaved
     * fires, so a consumer that later asks the same cache for that path gets a hit and never opens
     * the freshly-written output mid-run. Warming is best-effort: a failure is logged, not fatal.
     */
    std::string thumbnailCacheDir;

    // -----------------------------------------------------------------------
    // Optional processing steps — absent by default
    // -----------------------------------------------------------------------

    /**
     * \brief Per-page colour grade, applied in the page domain (before scale).
     *
     * Applies to every input except those in its \c excludedInputUids. The grade never changes how
     * a page is *read*, only what happens to its pixels, so \c enabled false is a true no-op.
     */
    Models::ColourCorrection colourCorrection;

    /**
     * \brief Text/bubble overlays, composited in the strip domain (per slice, at strip-Y).
     *
     * An overlay carrying an \c anchorInputUid is placed relative to that page's top edge in the
     * strip *this* run builds, so it stays on its own artwork when the chapter is edited; one
     * anchored to a page that did not load is logged and skipped. A slice no overlay intersects is
     * untouched even when overlays exist elsewhere.
     */
    std::vector<Models::StripOverlay> stripOverlays;

    /**
     * \brief The \c outputProfile.targetWidth the overlays above were authored against. 0 = "this one".
     *
     * Overlay placement and artwork are both in pixels, so both mean something only relative to a
     * target width. Re-profiling a chapter from 800 px to 1600 px doubles every page — and would leave
     * every bubble at half size in the wrong place if this were not recorded.
     *
     * The render derives \c scale = targetWidth / overlayAuthoredWidth and applies it to the artwork
     * (\c StripOverlayCompositor::rasterizeOverlays()) and to the placement
     * (\c Models::resolveOverlayAnchors()) together. A vector asset re-renders sharp at the new size;
     * a raster one is resampled, which is the honest limit of a raster overlay rather than a defect.
     *
     * 0 means "authored at this render's own target width", so a consumer that never re-profiles can
     * ignore this field entirely and a project written before it existed renders exactly as it did.
     */
    int overlayAuthoredWidth = 0;

    // -----------------------------------------------------------------------
    // Partial re-render
    // -----------------------------------------------------------------------

    /**
     * \brief Restricts the render to the named output files. Absent → render every slice.
     *
     * Only slices whose output file name is in the set are encoded, hashed, saved and recorded.
     * The strip is still assembled and sliced once either way — a slice's pixels depend on the
     * pages above it, so there is no cheaper way to reach slice 40 than to build the strip.
     */
    std::optional<std::unordered_set<std::string>> onlySlices;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PROCESSING_PIPELINE_RENDER_REQUEST_HPP
