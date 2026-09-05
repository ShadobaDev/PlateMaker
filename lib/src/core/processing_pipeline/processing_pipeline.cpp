/**
 * \file lib/src/core/processing_pipeline/processing_pipeline.cpp
 * \brief ProcessingPipeline implementation.
 *
 * The three entry points here are thin on purpose. The work lives in three internal collaborators
 * in this same directory, one per phase of a render:
 *
 *   - \c PageRenderer  — the page domain: header decisions, then load → grade → crop → scale.
 *   - \c StripBuilder  — phase 1: append every page, and remember where each one landed.
 *   - \c SliceWriter   — phase 2: composite, encode, hash and record each streamed slice.
 *
 * What remains below is the sequence itself, plus the error and cancellation exits — which is the
 * part a reader actually needs to see in one screen.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-20
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>

#include <platemaker/core/canvas_profile_matcher/canvas_profile_matcher.hpp>
#include <platemaker/infrastructure/log/log.hpp>

#include "page_renderer.hpp"
#include "pipeline_log.hpp"
#include "slice_writer.hpp"
#include "strip_builder.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <vips/vips.h>

namespace Platemaker::Core {

namespace {
namespace Log = Platemaker::Infrastructure::Log;
} // namespace

ProcessingOutcome ProcessingPipeline::render(
    const RenderRequest&                     request,
    const Infrastructure::CancellationToken& cancel,
    const ProcessingCallbacks&               callbacks)
{
    using Platemaker::Models::ProcessingError;
    using Platemaker::Models::ProcessingErrorCategory;
    using Platemaker::Models::ProcessingErrorCode;

    ProcessingOutcome outcome;

    // Safety net for unforeseen faults. The pipeline handles expected failures inline (per-input
    // load, save, slicing) and returns them typed; this outer guard converts anything that still
    // escapes those blocks — an exception from setup / allocation / a dependency, or a non-std
    // throw — into a typed Unexpected/Internal failure instead of unwinding out of render() and
    // terminating the caller's (worker) thread. It captures a message for a bug report; it is NOT
    // recovery, and it does NOT catch hardware faults such as a segfault or null dereference
    // (those are OS signals / SEH, not C++ exceptions, and need a separate crash handler).
    try {

    CanvasProfileMatcher matcher(request.canvasProfiles, request.canvasProfileIds);
    const PageRenderer   pages(matcher, /*hasProfiles=*/!request.canvasProfiles.empty(),
                               request.outputProfile.targetWidth);

    // -----------------------------------------------------------------------
    // 1. Build the virtual strip (load → optional grade → optional crop → scale → append).
    // -----------------------------------------------------------------------
    StripBuilder builder(pages, request.colourCorrection, callbacks);
    if (!builder.appendAllPages(request.inputs, cancel, outcome))
        return outcome; // cancelled mid-build

    if (builder.strip().totalHeight() == 0) {
        outcome.failed = true;
        outcome.error  = ProcessingError{
            ProcessingErrorCode::NoPagesLoaded, ProcessingErrorCategory::Load,
            "No pages were loaded successfully.", {}, {}};
        emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return outcome;
    }

    // The strip is assembled; the phase-1 → phase-2 boundary. Report how many slices it will
    // produce (all of them, before any partial filter) so a consumer can lay out output rows.
    const int expectedTotal =
        expectedSliceCount(builder.strip().totalHeight(), request.outputProfile);
    if (callbacks.onSlicingStarted)
        callbacks.onSlicingStarted({expectedTotal});

    // -----------------------------------------------------------------------
    // 2. Slice and save in one pass.
    //
    // The strip streams each slice to us and releases source images as it goes, so saving must
    // happen inside the callback rather than over a collected list — that is what keeps only one
    // slice and ~two sources alive at a time.
    // -----------------------------------------------------------------------
    SliceWriter writer(request, builder.pageTopByInputUid(), expectedTotal, callbacks);
    outcome.records.reserve(static_cast<std::size_t>(writer.plannedSliceCount()));

    try {
        builder.strip().sliceAll(
            request.outputProfile.sliceHeight, request.outputProfile.lastSlicePolicy, cancel,
            [&](SliceResult&& slice) { return writer.writeSlice(std::move(slice), outcome); });
    } catch (const std::exception& e) {
        outcome.failed = true;
        outcome.error  = ProcessingError{
            ProcessingErrorCode::SlicingFailed, ProcessingErrorCategory::Slice,
            std::string("Slicing failed: ") + e.what(), {}, {}};
        emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return outcome;
    }

    // A save error already set `failed` and stopped the run; don't mask it.
    if (outcome.failed)
        return outcome;

    // If slicing was cut short by cancellation, flag it.
    if (cancel.isCancelled())
        outcome.cancelled = true;

    return outcome;

    } catch (const std::exception& e) {
        outcome.failed = true;
        outcome.error  = Models::ProcessingError{
            Models::ProcessingErrorCode::Unexpected, Models::ProcessingErrorCategory::Internal,
            std::string("Unexpected internal error: ") + e.what(), {}, {}};
        emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return outcome;
    } catch (...) {
        outcome.failed = true;
        outcome.error  = Models::ProcessingError{
            Models::ProcessingErrorCode::Unexpected, Models::ProcessingErrorCategory::Internal,
            "Unexpected internal error (non-standard exception).", {}, {}};
        emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return outcome;
    }
}

// ---------------------------------------------------------------------------
// layoutPagesFromHeaders
// ---------------------------------------------------------------------------

std::vector<PagePreviewGeometry> ProcessingPipeline::layoutPagesFromHeaders(
    const std::vector<Models::InputFile>&     inputs,
    const Models::OutputProfile&              outProfile,
    const std::vector<Models::CanvasProfile>& canvasProfiles,
    const std::vector<std::string>&           canvasProfileIds)
{
    CanvasProfileMatcher matcher(canvasProfiles, canvasProfileIds);
    const PageRenderer   pages(matcher, /*hasProfiles=*/!canvasProfiles.empty(),
                               outProfile.targetWidth);

    std::vector<PagePreviewGeometry> layout;
    layout.reserve(inputs.size());

    for (const auto& file : inputs) {
        PagePreviewGeometry g;
        g.sourceFilePath = file.filePath;
        g.inputUid       = file.uid;

        if (file.status == Models::FileStatus::Missing) {
            g.readable = false;
            layout.push_back(std::move(g));
            continue;
        }

        try {
            const PagePlan plan = pages.planFromHeader(file.filePath);

            // Run the real page domain and read the dimensions off the result. Every operation it
            // builds is lazy — nothing is decoded because nothing pulls a pixel — so this costs a
            // header read, and in exchange the numbers are the render's by construction instead of
            // arithmetic that could drift from it.
            const ScaledImage scaled = pages.scaledPage(plan, file.filePath, /*grade=*/nullptr);

            g.sourceWidth     = plan.geo.width;
            g.sourceHeight    = plan.geo.height;
            g.canvasProfileId = plan.profile ? plan.profile->id : std::string{};
            g.status          = plan.status;
            g.width           = scaled.buffer.width();
            g.height          = scaled.buffer.height();
        } catch (const std::exception& e) {
            // A page render() would skip contributes nothing to the strip. Report it as such rather
            // than failing the whole layout — one bad file must not cost the consumer the other
            // 99 pages.
            PLATEMAKER_LOG(Log::ProcessingPipeline,
                    "layoutPagesFromHeaders: " + file.filePath + " is not renderable (" + e.what() + ")");
            g                = PagePreviewGeometry{};
            g.sourceFilePath = file.filePath;
            g.inputUid       = file.uid;
            g.readable       = false;
        }

        layout.push_back(std::move(g));
    }

    return layout;
}

// ---------------------------------------------------------------------------
// decodePageToRgba
// ---------------------------------------------------------------------------

void ProcessingPipeline::decodePageToRgba(
    const Models::InputFile&                  input,
    const Models::OutputProfile&              outProfile,
    const std::vector<Models::CanvasProfile>& canvasProfiles,
    const std::vector<std::string>&           canvasProfileIds,
    unsigned char*                            rgba,
    int                                       width,
    int                                       height)
{
    if (!rgba || width <= 0 || height <= 0)
        throw std::runtime_error("ProcessingPipeline::decodePageToRgba — invalid buffer or dimensions");

    // The library does not own the vips lifecycle, and a consumer without the vips headers cannot call
    // VIPS_INIT itself — it may reach here before any render has initialised vips. VIPS_INIT only does
    // anything on its first successful call (same reasoning as ColourCorrector::applyToRgba).
    if (VIPS_INIT("platemaker"))
        vips_error_clear(); // a genuine init failure surfaces as a throw from the vips calls below

    CanvasProfileMatcher matcher(canvasProfiles, canvasProfileIds);
    const PageRenderer   pages(matcher, /*hasProfiles=*/!canvasProfiles.empty(),
                               outProfile.targetWidth);

    const PagePlan    plan   = pages.planFromHeader(input.filePath);
    const ScaledImage scaled = pages.scaledPage(plan, input.filePath, /*grade=*/nullptr);

    if (scaled.buffer.width() != width || scaled.buffer.height() != height) {
        throw std::runtime_error(
            "ProcessingPipeline::decodePageToRgba — '" + input.filePath + "' renders to " +
            std::to_string(scaled.buffer.width()) + "x" + std::to_string(scaled.buffer.height()) +
            ", not the requested " + std::to_string(width) + "x" + std::to_string(height) +
            " (stale layout — call layoutPagesFromHeaders() again)");
    }

    // Normalise to the one layout a consumer can rely on: 8-bit sRGB, four interleaved bands.
    // vips_colourspace to sRGB settles both the colour space and the depth (sRGB is 8-bit by
    // definition in libvips) and passes any alpha through untouched; vips_addalpha then makes a
    // 1- or 3-band page opaque-4-band. Both are no-ops for a page that already is 8-bit sRGB RGBA.
    VipsImage* srgb = nullptr;
    if (vips_colourspace(scaled.buffer.get(), &srgb, VIPS_INTERPRETATION_sRGB, nullptr) != 0) {
        const std::string err = vips_error_buffer();
        vips_error_clear();
        throw std::runtime_error("ProcessingPipeline::decodePageToRgba — to sRGB: " + err);
    }

    VipsImage* out = srgb;
    if (vips_image_get_bands(srgb) < 4) {
        VipsImage* withAlpha = nullptr;
        if (vips_addalpha(srgb, &withAlpha, nullptr) != 0) {
            const std::string err = vips_error_buffer();
            vips_error_clear();
            g_object_unref(srgb);
            throw std::runtime_error("ProcessingPipeline::decodePageToRgba — add alpha: " + err);
        }
        g_object_unref(srgb);
        out = withAlpha;
    }

    if (out->Bands != 4 || out->BandFmt != VIPS_FORMAT_UCHAR) {
        const std::string got = std::to_string(out->Bands) + " bands, format "
                              + std::to_string(static_cast<int>(out->BandFmt));
        g_object_unref(out);
        throw std::runtime_error(
            "ProcessingPipeline::decodePageToRgba — expected 4-band 8-bit sRGB, got " + got);
    }

    const std::size_t nbytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    std::size_t       outSize = 0;
    void*             outData = vips_image_write_to_memory(out, &outSize);
    g_object_unref(out);
    if (!outData) {
        const std::string err = vips_error_buffer();
        vips_error_clear();
        throw std::runtime_error("ProcessingPipeline::decodePageToRgba — read pixels: " + err);
    }
    std::memcpy(rgba, outData, std::min(outSize, nbytes));
    g_free(outData);
}

} // namespace Platemaker::Core
