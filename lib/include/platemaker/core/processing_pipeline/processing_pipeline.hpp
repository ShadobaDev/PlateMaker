/**
 * \file lib/include/platemaker/core/processing_pipeline/processing_pipeline.hpp
 * \brief ProcessingPipeline — runs the full scale → strip → slice → save pipeline
 *        for one project, with progress, logging and cooperative cancellation.
 *
 * This is the single source of truth for the processing pipeline shared by the CLI
 * and the GUI.  It owns only the compute + disk-write phase; the caller is
 * responsible for resolving the project/profile/output directory, calling
 * \c ProjectItem::sanitize() / \c isUpToDate(), applying the returned records via
 * \c ProjectItem::applyProcessingResults(), and persisting the workspace.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-20
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_PROCESSING_PIPELINE_HPP
#define PLATEMAKER_CORE_PROCESSING_PIPELINE_HPP

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/core/processing_callbacks/processing_callbacks.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

namespace Platemaker::Core {

// ProcessingProgress, ProcessingLogLevel and the ProcessingCallbacks set live in
// processing_callbacks.hpp (included above), keeping this header focused on the pipeline.

/// Result of a pipeline run.  The caller applies \c records to the project.
struct ProcessingOutcome {
    std::vector<Models::ProcessingSliceRecord> records;      //!< One per saved slice, in order.
    std::vector<std::string>                   skippedPages; //!< Inputs that were skipped (missing / unmatched / load error).
    std::vector<Models::AppliedCanvasProfile>  appliedProfiles; //!< Canvas profile applied per input (see the Models type).
    bool cancelled = false; //!< True if cancellation cut the run short.
    bool failed    = false; //!< True if a fatal error aborted the run (== \c error.has_value()).
    std::optional<Models::ProcessingError> error; //!< The fatal error, when \c failed. Replaces the old
                                                  //!< free-text errorMessage: \c error->message carries the
                                                  //!< text, \c error->code / \c category the machine tag.
};

/**
 * \brief Where one input page lands on the preview strip.
 *
 * Produced by \c ProcessingPipeline::previewLayout().  The dimensions are the *render's* — they come
 * from the same page-domain code \c run() uses, not from re-derived arithmetic — so a consumer can
 * stack these into a strip that agrees with what a render would produce, before any render exists.
 */
struct PagePreviewGeometry {
    std::string sourceFilePath;      //!< The input this describes (absolute path, as given).
    int         width        = 0;    //!< Scaled width — the output profile's target width, in practice.
    int         height       = 0;    //!< Scaled height: this page's slot in the strip.
    int         sourceWidth  = 0;    //!< Display size from the header (EXIF-rotated), before margins/scale.
    int         sourceHeight = 0;
    std::string canvasProfileId;     //!< Profile applied, or empty when the page is rendered implicitly.
    InputStatus status = InputStatus::Appended; //!< Exactly what \c run() would report for this page.

    /**
     * \brief False when this page contributes nothing to the strip.
     *
     * Missing, unreadable, or failing the same checks that make \c run() skip a page — in which case
     * \c width / \c height are 0.  A consumer stacking pages **must** skip these exactly as the render
     * does, or every page below sits at the wrong strip offset.
     */
    bool readable = true;
};

/**
 * \class ProcessingPipeline
 * \brief Stateless runner for the scale → strip → slice → save pipeline.
 *
 * Holds no state — \c run() is \c static; call it as \c ProcessingPipeline::run(...) without
 * constructing an instance. It operates on a **copy** of the input file list (never the live
 * \c ProjectItem), so it is safe to invoke from a worker thread while the GUI holds the workspace.
 * Callbacks fire on the calling thread.
 */
class PLATEMAKER_EXPORT ProcessingPipeline {
public:
    /**
     * \brief Builds the virtual strip from \p inputs, slices it, and saves every slice.
     *
     * \param inputs           Copy of the project's input files (paths + statuses).
     *                         Files with \c FileStatus::Missing are skipped.
     * \param outProfile       Output profile (target width, slice height, format, …).
     * \param canvasProfiles   Full workspace canvas-profile palette (may be empty →
     *                         no margin matching, plain scale).
     * \param canvasProfileIds Project-linked profile ids (empty → accept all).
     * \param outputDir        Existing directory where slice files are written.
     * \param cancel           Polled between slices; a partial result is returned on cancel.
     * \param callbacks        Optional progress/event callbacks (see \c ProcessingCallbacks); any
     *                         field may be null. Invoked synchronously on the calling thread.
     * \param onlySlices       Optional partial-render filter.  When non-null, only slices whose
     *                         output file name is in the set are encoded, hashed, saved and
     *                         recorded; all others are skipped (the strip is still assembled and
     *                         sliced once).  Null → render every slice (full render).
     * \param thumbnailCacheDir Optional. When non-empty, each saved slice's preview is written into a
     *                         \c ThumbnailCache rooted here — from the **in-RAM** slice, with no
     *                         re-read of the output — *before* \c onSliceSaved fires. A consumer that
     *                         later calls \c ThumbnailCache::getOrGenerate(path) on the same dir then
     *                         gets a cache hit and never opens the output during the run. Warming a
     *                         thumbnail is best-effort: a failure is logged, not fatal. Empty → no
     *                         thumbnails (the CLI default; headless callers pay nothing).
     * \param colourCorrection Optional per-page colour grade applied in the page domain (before scale)
     *                         to every input except those in its \c excludedInputUids.  Default /
     *                         \c enabled==false → no colour work at all, so the output is byte-identical
     *                         to a build without this step.  The grade never changes how a page is
     *                         *read*, only what happens to its pixels, so a neutral grade is a no-op.
     * \param stripOverlays    Optional text/bubble overlays composited in the strip domain (per slice,
     *                         at strip-Y).  Empty → no compositing, so the output is byte-identical to a
     *                         build without this step; a slice no overlay intersects is likewise
     *                         untouched even when overlays are present elsewhere.
     * \return A \c ProcessingOutcome with per-slice records, skipped pages and flags.
     */
    [[nodiscard]] static ProcessingOutcome run(
        const std::vector<Models::InputFile>&      inputs,
        const Models::OutputProfile&               outProfile,
        const std::vector<Models::CanvasProfile>&  canvasProfiles,
        const std::vector<std::string>&            canvasProfileIds,
        const std::string&                         outputDir,
        const Infrastructure::CancellationToken&   cancel,
        const ProcessingCallbacks&                 callbacks         = {},
        const std::unordered_set<std::string>*     onlySlices        = nullptr,
        const std::string&                         thumbnailCacheDir = {},
        const Models::ColourCorrection&            colourCorrection  = {},
        const std::vector<Models::StripOverlay>&   stripOverlays     = {});

    // -----------------------------------------------------------------------
    // Preview — the same page domain, stopped before the strip
    // -----------------------------------------------------------------------
    //
    // A consumer that wants to *show* the strip does not want slices: slices are an output artifact
    // (files to publish), and a viewer draws a continuous strip and hides the seams anyway. It wants the
    // pages, and only the ones on screen. These two calls give it exactly that — the layout of every
    // page (cheap) and the pixels of one (on demand) — so the cost of showing a chapter tracks the
    // viewport, not the chapter. They live here, beside run(), because they must go through the same
    // page-domain code: a preview that re-derived page geometry would drift from the render silently.

    /**
     * \brief The preview strip's layout: one entry per input, in strip order.
     *
     * Decodes no pixels. Every libvips operation the page domain builds — open, autorotate, crop,
     * resize — is lazy: it settles the output dimensions on construction and reads the file's header,
     * but touches a pixel only when a consumer pulls one, which this never does. The dimensions
     * therefore come from the real pipeline rather than from arithmetic that could drift from it.
     * Budget roughly a header read per page (~1 ms); cache the result and refresh it when the inputs,
     * the canvas profiles or the output profile's target width change.
     *
     * Never throws for a bad page: one that cannot be read is returned with \c readable false and zero
     * dimensions, exactly as \c run() skips it. A consumer must skip those when stacking.
     *
     * \param inputs           The project's input files, in strip order.
     * \param outProfile       Supplies \c targetWidth — the width every page is scaled to.
     * \param canvasProfiles   The workspace's canvas profile palette (margins, sizes).
     * \param canvasProfileIds The profiles linked to this project, in priority order.
     * \return One entry per input, in the same order.
     */
    [[nodiscard]] static std::vector<PagePreviewGeometry> previewLayout(
        const std::vector<Models::InputFile>&      inputs,
        const Models::OutputProfile&               outProfile,
        const std::vector<Models::CanvasProfile>&  canvasProfiles,
        const std::vector<std::string>&            canvasProfileIds);

    /**
     * \brief Writes one page's **ungraded** pixels, at strip scale, into a caller-owned RGBA buffer.
     *
     * Runs the page exactly as \c run() would — EXIF-upright, profile-matched, margin-cropped, scaled —
     * and stops there. The result is always **8-bit sRGB RGBA8888**, four interleaved bytes per pixel
     * with no row padding, whatever the source's depth or band count was; that narrowing is
     * preview-only, the committed render still keeps the source's format.
     *
     * \par Why ungraded
     * The grade is deliberately **not** applied: its consumer re-grades on every slider move, and
     * re-decoding a page for that would cost ~100× what grading the buffer costs. Grade the returned
     * buffer with \c ColourCorrector::applyToRgba() instead — the same engine the render uses, so the
     * preview matches. The colour step never influences how a page is *read*, so this needs no colour
     * argument at all: a page fetched once stays a valid baseline for every grade the user tries on it.
     *
     * \par Buffer ownership
     * The caller allocates and owns \p rgba, as with \c ColourCorrector::applyToRgba() — no allocation
     * crosses the library boundary. Size it from this page's \c previewLayout() entry: exactly
     * \p width × \p height × 4 bytes. A mismatch throws rather than writing a plausible-looking wrong
     * image, so a layout that went stale surfaces immediately.
     *
     * \param input            The input file to render (its \c uid decides colour exclusion).
     * \param outProfile       Supplies \c targetWidth.
     * \param canvasProfiles   The workspace's canvas profile palette.
     * \param canvasProfileIds The profiles linked to this project, in priority order.
     * \param rgba             Destination buffer, at least \p width × \p height × 4 bytes.
     * \param width            Expected width  (from \c previewLayout()).
     * \param height           Expected height (from \c previewLayout()).
     *
     * \throws std::runtime_error if \p rgba is null, the dimensions are not positive, the page cannot be
     *         read, or the page's real size differs from \p width × \p height.
     */
    static void previewPageRgba(
        const Models::InputFile&                   input,
        const Models::OutputProfile&               outProfile,
        const std::vector<Models::CanvasProfile>&  canvasProfiles,
        const std::vector<std::string>&            canvasProfileIds,
        unsigned char*                             rgba,
        int                                        width,
        int                                        height);
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PROCESSING_PIPELINE_HPP
