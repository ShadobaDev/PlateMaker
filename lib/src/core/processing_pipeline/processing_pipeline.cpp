/**
 * \file lib/src/core/processing_pipeline/processing_pipeline.cpp
 * \brief ProcessingPipeline implementation.
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
#include <platemaker/core/colour_corrector/colour_corrector.hpp>
#include <platemaker/core/margin_cropper/margin_cropper.hpp>
#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/core/scaler/scaler.hpp>
#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/infrastructure/log/log.hpp>
#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>

#include <optional>

#include <vips/vips.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <iomanip>
#include <sstream>

namespace Platemaker::Core {

namespace {

// Image geometry read from the header only (no pixel decode): the *display* dimensions plus the EXIF
// Orientation tag if the file carries one. `width`/`height` are -1 on error. For Orientation 5–8 (the
// 90°/270° cases) the stored width/height are transposed, so this reports the size the autorotated scaler
// produces — keeping canvas-profile matching in agreement with what the render builds.
struct HeaderGeometry {
    int  width          = -1;
    int  height         = -1;
    int  orientation    = 1;      // EXIF Orientation (1 = normal); 1 when the tag is absent
    bool hasOrientation = false;  // whether the file actually carried an Orientation tag
};

HeaderGeometry headerGeometry(const std::string& filePath)
{
    HeaderGeometry geo;
    VipsImage* img = vips_image_new_from_file(
        filePath.c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!img) {
        vips_error_clear();
        return geo;
    }
    geo.width  = img->Xsize;
    geo.height = img->Ysize;
    if (vips_image_get_typeof(img, VIPS_META_ORIENTATION) != 0) {
        int o = 0;
        if (vips_image_get_int(img, VIPS_META_ORIENTATION, &o) == 0) {
            geo.orientation    = o;
            geo.hasOrientation = true;
        }
    }
    // Orientation 5–8 rotate by 90°/270°, transposing the displayed image. Report the display size (what
    // the autorotated scaler produces) so matching and rendering agree; 1–4 keep the stored dimensions.
    if (geo.orientation >= 5 && geo.orientation <= 8) {
        const int t = geo.width; geo.width = geo.height; geo.height = t;
    }
    g_object_unref(img);
    return geo;
}

std::string zeroPad(int n, int width)
{
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(width) << n;
    return oss.str();
}

std::string fmtExt(Models::OutputFormat fmt)
{
    switch (fmt) {
        case Models::OutputFormat::PNG:  return ".png";
        case Models::OutputFormat::JPEG: return ".jpg";
        case Models::OutputFormat::WebP: return ".webp";
    }
    return ".png";
}

void emitLog(const std::function<void(ProcessingLogLevel, const std::string&)>& onLog,
             ProcessingLogLevel level, const std::string& msg)
{
    if (onLog) onLog(level, msg);
}

namespace Log = Platemaker::Infrastructure::Log;

// ---------------------------------------------------------------------------
// The page domain — one input page, from file to its slot on the strip
// ---------------------------------------------------------------------------
//
// Split in two deliberately. planPage() makes every *decision* about a page — its EXIF-upright display
// size, which canvas profile applies, what to report about that — from the header alone. renderPage()
// turns a plan into pixels. Both the render (run()) and the preview entry points go through these, so
// there is exactly one definition of "what a page looks like on the strip". A preview that re-derived
// any of it would drift from the render *silently*: both would work, the numbers would just stop
// agreeing, and every page below the first disagreement would sit at the wrong strip offset.

//! Header-only decisions about one input page. Producing this touches no pixels.
struct PagePlan {
    HeaderGeometry               geo;                //!< Display size + EXIF orientation, from the header.
    const Models::CanvasProfile* profile = nullptr;  //!< Matched profile; null → render implicitly, no margins.
    InputStatus                  status  = InputStatus::Appended; //!< What run() reports for this page.
    std::vector<std::string>     candidateIds;       //!< Same-size workspace profiles not linked to the project.
    std::string                  candidateName;      //!< The first of those — for the diagnostic message only.
};

/**
 * \brief Resolves what will happen to \p filePath, from its header alone.
 * \throws std::runtime_error if the project has canvas profiles but the file's dimensions are unreadable.
 */
PagePlan planPage(const std::string& filePath, const CanvasProfileMatcher& matcher, bool hasProfiles)
{
    PagePlan plan;
    plan.geo = headerGeometry(filePath);

    PLATEMAKER_LOG(Log::ProcessingPipeline,
            "read " + filePath + ": header "
            + std::to_string(plan.geo.width) + "x" + std::to_string(plan.geo.height)
            + ", EXIF orientation "
            + (plan.geo.hasOrientation ? std::to_string(plan.geo.orientation)
                                       : std::string("none")));

    // No canvas profiles at all → the standard pipeline: scaled, never cropped. The dimension check
    // below is deliberately skipped in that case (as it always has been): nothing needs the size, and a
    // genuinely unreadable file fails loudly in the scaler instead.
    if (!hasProfiles) return plan;

    if (plan.geo.width <= 0 || plan.geo.height <= 0)
        throw std::runtime_error("cannot determine image dimensions");

    const ProfileMatchResult result = matcher.resolve(plan.geo.width, plan.geo.height);
    if (result.status == ProfileMatchResult::Status::Matched) {
        plan.profile = result.profile;
    } else if (result.status == ProfileMatchResult::Status::FoundInWorkspaceOnly) {
        // A same-size profile exists in the workspace but is not linked to this project. Render
        // implicitly (no margins) rather than drop the page — linking the profile is a one-click fix.
        plan.status = InputStatus::AppendedProfileNotLinked;
        plan.candidateIds.reserve(result.workspaceCandidates.size());
        for (const auto* cand : result.workspaceCandidates)
            plan.candidateIds.push_back(cand->id);
        if (!result.workspaceCandidates.empty())
            plan.candidateName = result.workspaceCandidates.front()->name;
    } else {
        // No profile of this size exists anywhere — render the page implicitly, exactly the
        // no-profiles path. Determinism is preserved by visibility (the input is flagged), not omission.
        plan.status = InputStatus::AppendedWithoutProfile;
    }
    return plan;
}

/**
 * \brief Turns a plan into the scaled page the strip receives: load → [grade] → [crop] → scale.
 *
 * \param ccApplies True when the colour step applies to this page (enabled and the page not excluded).
 *                  It drives the *load* — `ColourCorrection::iccToSRGB` decides the sRGB conversion
 *                  instead of the margin path's historical always-sRGB — independently of \p bakeGrade.
 * \param bakeGrade Whether to actually run the grade. The render bakes it in; the preview does not,
 *                  because its consumer grades the returned pixels itself (see previewPageRgba) and
 *                  must be able to re-grade on every slider move without re-decoding the page.
 */
ScaledImage renderPage(const PagePlan&                 plan,
                       const std::string&              filePath,
                       int                             targetWidth,
                       const Models::ColourCorrection& cc,
                       bool                            ccApplies,
                       bool                            bakeGrade)
{
    // All four are stateless and empty — constructing them per page costs nothing and keeps the page
    // domain self-contained.
    Scaler                  scaler;
    MarginCropper           cropper;
    Infrastructure::ImageIO imageIO;
    ColourCorrector         colourCorrector;

    const bool doMarginCrop =
        plan.profile != nullptr &&
        (plan.profile->margins.top    > 0 ||
         plan.profile->margins.bottom > 0 ||
         plan.profile->margins.left   > 0 ||
         plan.profile->margins.right  > 0);

    // When the colour step is off, both branches below are exactly the historical code paths, so the
    // output stays byte-identical to a build without the step.
    if (doMarginCrop) {
        auto buf = imageIO.load(filePath, ccApplies ? cc.iccToSRGB : true);
        if (ccApplies && bakeGrade)
            buf = colourCorrector.apply(std::move(buf), cc);
        auto cropped = cropper.crop(buf, plan.profile->margins);
        return scaler.scale(std::move(cropped), filePath, targetWidth);
    }
    if (ccApplies) {
        // Load explicitly so the grade can sit between load and scale.
        auto buf = imageIO.load(filePath, cc.iccToSRGB);
        if (bakeGrade)
            buf = colourCorrector.apply(std::move(buf), cc);
        return scaler.scale(std::move(buf), filePath, targetWidth);
    }
    return scaler.scale(filePath, targetWidth);
}

} // namespace

ProcessingOutcome ProcessingPipeline::run(
    const std::vector<Models::InputFile>&     inputs,
    const Models::OutputProfile&              outProfile,
    const std::vector<Models::CanvasProfile>& canvasProfiles,
    const std::vector<std::string>&           canvasProfileIds,
    const std::string&                        outputDir,
    const Infrastructure::CancellationToken&  cancel,
    const ProcessingCallbacks&                callbacks,
    const std::unordered_set<std::string>*    onlySlices,
    const std::string&                        thumbnailCacheDir,
    const Models::ColourCorrection&           colourCorrection,
    const std::vector<Models::StripOverlay>&  stripOverlays)
{
    using namespace Platemaker::Models;
    using Platemaker::Infrastructure::FileMetaData;
    using Platemaker::Infrastructure::ImageIO;
    using Platemaker::Infrastructure::OutputLockedError;
    using Platemaker::Infrastructure::ThumbnailCache;

    ProcessingOutcome outcome;

    // Safety net for unforeseen faults. The pipeline handles expected failures inline (per-input load,
    // save, slicing) and returns them typed; this outer guard converts anything that still escapes those
    // blocks — an exception from setup / allocation / a dependency, or a non-std throw — into a typed
    // Unexpected/Internal failure instead of unwinding out of run() and terminating the caller's (worker)
    // thread. It captures a message for a bug report; it is NOT recovery, and it does NOT catch hardware
    // faults such as a segfault or null dereference (those are OS signals / SEH, not C++ exceptions, and
    // need a separate crash handler).
    try {

    // The page domain (load / crop / grade / scale) is owned by renderPage(); what remains here is the
    // strip domain and the save.
    ImageIO                imageIO;
    ScaledStrip            strip;
    StripOverlayCompositor overlayCompositor;

    const bool hasProfiles = !canvasProfiles.empty();
    CanvasProfileMatcher matcher(canvasProfiles, canvasProfileIds);

    // Colour correction (page domain): applied per input before scale/append, unless the step is
    // disabled or this page's uid is excluded. Disabled → the historical load/scale paths run untouched,
    // so the output is byte-identical to a build without this step.
    const std::unordered_set<std::string> ccExcluded(
        colourCorrection.excludedInputUids.begin(), colourCorrection.excludedInputUids.end());

    // -----------------------------------------------------------------------
    // 1. Build the virtual strip (load → optional margin crop → scale → append).
    // -----------------------------------------------------------------------
    for (const auto& file : inputs) {
        if (cancel.isCancelled()) { outcome.cancelled = true; return outcome; }

        if (file.status == FileStatus::Missing) {
            emitLog(callbacks.onLog, ProcessingLogLevel::Warning,
                    "Skipping missing file: " + file.filePath);
            outcome.skippedPages.push_back(file.filePath);
            if (callbacks.onInput)
                callbacks.onInput({file.filePath, InputStatus::SkippedMissing, {}, {}});
            continue;
        }

        try {
            // The header is read unconditionally inside planPage(): the display W×H is recorded per
            // input (below) so a later run can tell offline which canvas profile each page would match
            // now — matching is purely by W×H, and without the stored dimensions any profile-list change
            // degrades to a whole-project re-render. A header read is negligible beside the decode +
            // scale that follow.
            PagePlan plan = planPage(file.filePath, matcher, hasProfiles);

            // Report the match. planPage() returns the facts; the messages live here because this loop
            // owns the callbacks.
            if (hasProfiles) {
                const std::string dims = std::to_string(plan.geo.width) + "x"
                                       + std::to_string(plan.geo.height);
                if (plan.profile) {
                    emitLog(callbacks.onLog, ProcessingLogLevel::Info,
                            "Applied canvas profile '" + plan.profile->name + "' (" + dims
                            + ") to " + file.filePath);
                } else if (plan.status == InputStatus::AppendedProfileNotLinked) {
                    // Say it loudly: linking the profile is a one-click fix that would apply its margins.
                    emitLog(callbacks.onLog, ProcessingLogLevel::Warning,
                            "No linked canvas profile matches " + dims + " — rendering " + file.filePath
                            + " without margins; profile '" + plan.candidateName + "' (" + dims
                            + ") exists in the workspace but is not linked to this project."
                            + " Link it to apply its margins.");
                } else {
                    emitLog(callbacks.onLog, ProcessingLogLevel::Info,
                            "No canvas profile matches " + dims + " — rendering " + file.filePath
                            + " without margins");
                }
            }

            // Record what was actually applied to this page. Editing a profile leaves
            // both the input and the output file byte-identical, so this is the only
            // trace that lets the next run notice the page went stale.
            outcome.appliedProfiles.push_back(Models::AppliedCanvasProfile{
                file.filePath,
                plan.profile ? plan.profile->id : std::string{},
                plan.profile ? Models::canvasRenderFingerprint(*plan.profile)
                             : std::string{},
                plan.geo.width,
                plan.geo.height});

            // Grade this page when the step is enabled and the page is not excluded. When off,
            // renderPage() runs exactly the historical code paths (byte-identical output).
            const bool applyCC = colourCorrection.enabled && ccExcluded.count(file.uid) == 0;

            strip.append(renderPage(plan, file.filePath, outProfile.targetWidth,
                                    colourCorrection, applyCC, /*bakeGrade=*/true));

            if (callbacks.onInput)
                callbacks.onInput({file.filePath, plan.status, std::move(plan.candidateIds), {}});
        } catch (const std::exception& e) {
            emitLog(callbacks.onLog, ProcessingLogLevel::Warning,
                    std::string("Skipping (") + e.what() + "): " + file.filePath);
            outcome.skippedPages.push_back(file.filePath);
            if (callbacks.onInput) {
                InputResult r;
                r.inputPath     = file.filePath;
                r.status        = InputStatus::SkippedError;
                r.detail        = e.what();
                r.errorCode     = ProcessingErrorCode::InputLoadFailed; // non-fatal: reported here, not on the outcome
                r.errorCategory = ProcessingErrorCategory::Load;
                callbacks.onInput(r);
            }
        }
    }

    if (strip.totalHeight() == 0) {
        outcome.failed = true;
        outcome.error  = ProcessingError{
            ProcessingErrorCode::NoPagesLoaded, ProcessingErrorCategory::Load,
            "No pages were loaded successfully.", {}, {}};
        emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return outcome;
    }

    // Expected slice count (used as the progress denominator so it stays correct
    // even if the run is cancelled before all slices are produced).
    const int totalH    = strip.totalHeight();
    const int fullCount = totalH / outProfile.sliceHeight;
    const int remainder = totalH % outProfile.sliceHeight;
    const int expectedTotal = fullCount +
        ((remainder > 0 && outProfile.lastSlicePolicy != LastSlicePolicy::Crop) ? 1 : 0);

    // The strip is assembled; the phase-1 → phase-2 boundary. Report how many slices it will
    // produce (all of them, before any partial filter) so a consumer can lay out output rows.
    if (callbacks.onSlicingStarted)
        callbacks.onSlicingStarted({expectedTotal});

    // -----------------------------------------------------------------------
    // 2. Slice and save in one pass.
    //
    // The strip streams each slice to us and releases source images as it goes,
    // so saving must happen here rather than over a collected list — that is what
    // keeps only one slice and ~two sources alive at a time.
    // -----------------------------------------------------------------------
    const std::string ext = fmtExt(outProfile.outputFormat);

    const auto sliceName = [&](int index) {
        return "output_" + zeroPad(outProfile.startIndex + index, 3) + ext;
    };

    // Partial render: progress denominator is the number of slices we will actually
    // save (those whose name is in the filter), not the full count. Slice names derive
    // from the index alone, so this is computed from the expected count up front —
    // no need to build the slices first.
    int renderTotal = expectedTotal;
    if (onlySlices) {
        renderTotal = 0;
        for (int i = 0; i < expectedTotal; ++i)
            if (onlySlices->count(sliceName(i))) ++renderTotal;
    }
    outcome.records.reserve(static_cast<std::size_t>(renderTotal));

    // Optional (Arch C — see the "render output contract" in docs/SPECIFICATION.md): pre-warm a
    // thumbnail cache from each slice's in-RAM pixels, so a consumer never re-reads a freshly-written
    // output to preview it. Constructed once; a bad dir just disables previews for this run, not the run.
    std::optional<ThumbnailCache> thumbCache;
    if (!thumbnailCacheDir.empty()) {
        try {
            thumbCache.emplace(thumbnailCacheDir);
        } catch (const std::exception& e) {
            emitLog(callbacks.onLog, ProcessingLogLevel::Warning,
                    std::string("Thumbnail previews disabled: ") + e.what());
        }
    }

    // Strip-domain overlays (text/bubbles): decode the bitmaps once, then composite the ones each slice
    // intersects just before it is saved. Empty → no compositing (byte-identical output).
    const std::vector<LoadedOverlay> loadedOverlays = overlayCompositor.load(stripOverlays);

    int savedCount = 0;
    const auto onSlice = [&](SliceResult&& slice) -> bool {
        const std::string outName = sliceName(slice.index);

        // Skip clean slices when a partial-render filter is supplied.
        if (onlySlices && onlySlices->count(outName) == 0) {
            if (callbacks.onSliceSkipped)
                callbacks.onSliceSkipped({slice.index, outName});
            return true;
        }

        const std::string outPath = outputDir + "/" + outName;

        // Composite any overlays intersecting this slice (strip domain) before encoding. A slice no
        // overlay touches is returned unchanged; a failure aborts the render with a typed error.
        if (!loadedOverlays.empty()) {
            try {
                slice.image = overlayCompositor.apply(std::move(slice.image), slice.stripTopY, loadedOverlays);
            } catch (const std::exception& e) {
                outcome.failed = true;
                outcome.error  = ProcessingError{
                    ProcessingErrorCode::SliceEncodeFailed, ProcessingErrorCategory::Encode,
                    "Failed to composite overlays on '" + outName + "': " + e.what(), {}, outName};
                emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
                return false;   // stop slicing
            }
        }

        try {
            imageIO.save(slice.image, outPath, outProfile);
        } catch (const OutputLockedError& e) {
            outcome.failed = true;
            outcome.error  = ProcessingError{
                ProcessingErrorCode::OutputLocked, ProcessingErrorCategory::Io,
                e.what(), outPath, outName};
            emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
            return false;   // stop slicing
        } catch (const std::exception& e) {
            outcome.failed = true;
            outcome.error  = ProcessingError{
                ProcessingErrorCode::SliceEncodeFailed, ProcessingErrorCategory::Encode,
                "Failed to save '" + outName + "': " + e.what(), {}, outName};
            emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
            return false;   // stop slicing
        }

        // Pre-warm the thumbnail cache from the in-RAM slice (before onSliceSaved), so a consumer's
        // getOrGenerate(outPath) is a cache hit and never re-reads the freshly-written output during the
        // run. Best-effort — a preview failure must not fail the render.
        if (thumbCache) {
            try {
                thumbCache->generate(outPath, slice.image);
            } catch (const std::exception& e) {
                emitLog(callbacks.onLog, ProcessingLogLevel::Warning,
                        "Thumbnail preview for '" + outName + "' skipped: " + e.what());
            }
        }

        ProcessingSliceRecord rec;
        rec.fileName     = outName;
        rec.outputSha256 = FileMetaData::computeFileSha256(outPath);
        rec.sourceMap    = std::move(slice.sourceMap);
        outcome.records.push_back(std::move(rec));

        ++savedCount;
        emitLog(callbacks.onLog, ProcessingLogLevel::Info, "Saved " + outName);
        if (callbacks.onProgress)
            callbacks.onProgress({savedCount, renderTotal, outName});
        if (callbacks.onSliceSaved)
            callbacks.onSliceSaved({slice.index, outName, outPath});

        return true;
    };

    try {
        strip.sliceAll(outProfile.sliceHeight, outProfile.lastSlicePolicy,
                       cancel, onSlice);
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
        outcome.error  = ProcessingError{
            ProcessingErrorCode::Unexpected, ProcessingErrorCategory::Internal,
            std::string("Unexpected internal error: ") + e.what(), {}, {}};
        emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return outcome;
    } catch (...) {
        outcome.failed = true;
        outcome.error  = ProcessingError{
            ProcessingErrorCode::Unexpected, ProcessingErrorCategory::Internal,
            "Unexpected internal error (non-standard exception).", {}, {}};
        emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return outcome;
    }
}

// ---------------------------------------------------------------------------
// previewLayout
// ---------------------------------------------------------------------------

std::vector<PagePreviewGeometry> ProcessingPipeline::previewLayout(
    const std::vector<Models::InputFile>&     inputs,
    const Models::OutputProfile&              outProfile,
    const std::vector<Models::CanvasProfile>& canvasProfiles,
    const std::vector<std::string>&           canvasProfileIds)
{
    const bool           hasProfiles = !canvasProfiles.empty();
    CanvasProfileMatcher matcher(canvasProfiles, canvasProfileIds);

    std::vector<PagePreviewGeometry> layout;
    layout.reserve(inputs.size());

    for (const auto& file : inputs) {
        PagePreviewGeometry g;
        g.sourceFilePath = file.filePath;

        if (file.status == Models::FileStatus::Missing) {
            g.readable = false;
            layout.push_back(std::move(g));
            continue;
        }

        try {
            const PagePlan plan = planPage(file.filePath, matcher, hasProfiles);

            // Run the real page domain and read the dimensions off the result. Every operation it
            // builds is lazy — nothing is decoded because nothing pulls a pixel — so this costs a
            // header read, and in exchange the numbers are the render's by construction instead of
            // arithmetic that could drift from it. The colour step is irrelevant here: neither the ICC
            // transform nor the grade changes an image's size.
            const ScaledImage scaled = renderPage(plan, file.filePath, outProfile.targetWidth,
                                                  Models::ColourCorrection{},
                                                  /*ccApplies=*/false, /*bakeGrade=*/false);

            g.sourceWidth     = plan.geo.width;
            g.sourceHeight    = plan.geo.height;
            g.canvasProfileId = plan.profile ? plan.profile->id : std::string{};
            g.status          = plan.status;
            g.width           = scaled.buffer.width();
            g.height          = scaled.buffer.height();
        } catch (const std::exception& e) {
            // A page run() would skip contributes nothing to the strip. Report it as such rather than
            // failing the whole layout — one bad file must not cost the consumer the other 99 pages.
            PLATEMAKER_LOG(Log::ProcessingPipeline,
                    "previewLayout: " + file.filePath + " is not renderable (" + e.what() + ")");
            g          = PagePreviewGeometry{};
            g.sourceFilePath = file.filePath;
            g.readable = false;
        }

        layout.push_back(std::move(g));
    }

    return layout;
}

// ---------------------------------------------------------------------------
// previewPageRgba
// ---------------------------------------------------------------------------

void ProcessingPipeline::previewPageRgba(
    const Models::InputFile&                  input,
    const Models::OutputProfile&              outProfile,
    const std::vector<Models::CanvasProfile>& canvasProfiles,
    const std::vector<std::string>&           canvasProfileIds,
    const Models::ColourCorrection&           colourCorrection,
    unsigned char*                            rgba,
    int                                       width,
    int                                       height)
{
    if (!rgba || width <= 0 || height <= 0)
        throw std::runtime_error("ProcessingPipeline::previewPageRgba — invalid buffer or dimensions");

    // The library does not own the vips lifecycle, and a consumer without the vips headers cannot call
    // VIPS_INIT itself — it may reach here before any render has initialised vips. VIPS_INIT only does
    // anything on its first successful call (same reasoning as ColourCorrector::applyToRgba).
    if (VIPS_INIT("platemaker"))
        vips_error_clear(); // a genuine init failure surfaces as a throw from the vips calls below

    const bool           hasProfiles = !canvasProfiles.empty();
    CanvasProfileMatcher matcher(canvasProfiles, canvasProfileIds);
    const PagePlan       plan = planPage(input.filePath, matcher, hasProfiles);

    // Whether the colour step applies decides which *load* path the render takes for this page, and
    // therefore what the ungraded baseline looks like. The grade itself is not baked — see the header.
    const auto& ex        = colourCorrection.excludedInputUids;
    const bool  ccApplies = colourCorrection.enabled &&
                            std::find(ex.begin(), ex.end(), input.uid) == ex.end();

    const ScaledImage scaled = renderPage(plan, input.filePath, outProfile.targetWidth,
                                          colourCorrection, ccApplies, /*bakeGrade=*/false);

    if (scaled.buffer.width() != width || scaled.buffer.height() != height) {
        throw std::runtime_error(
            "ProcessingPipeline::previewPageRgba — '" + input.filePath + "' renders to " +
            std::to_string(scaled.buffer.width()) + "x" + std::to_string(scaled.buffer.height()) +
            ", not the requested " + std::to_string(width) + "x" + std::to_string(height) +
            " (stale layout — call previewLayout() again)");
    }

    // Normalise to the one layout a consumer can rely on: 8-bit sRGB, four interleaved bands.
    // vips_colourspace to sRGB settles both the colour space and the depth (sRGB is 8-bit by
    // definition in libvips) and passes any alpha through untouched; vips_addalpha then makes a
    // 1- or 3-band page opaque-4-band. Both are no-ops for a page that already is 8-bit sRGB RGBA.
    VipsImage* srgb = nullptr;
    if (vips_colourspace(scaled.buffer.get(), &srgb, VIPS_INTERPRETATION_sRGB, nullptr) != 0) {
        const std::string err = vips_error_buffer();
        vips_error_clear();
        throw std::runtime_error("ProcessingPipeline::previewPageRgba — to sRGB: " + err);
    }

    VipsImage* out = srgb;
    if (vips_image_get_bands(srgb) < 4) {
        VipsImage* withAlpha = nullptr;
        if (vips_addalpha(srgb, &withAlpha, nullptr) != 0) {
            const std::string err = vips_error_buffer();
            vips_error_clear();
            g_object_unref(srgb);
            throw std::runtime_error("ProcessingPipeline::previewPageRgba — add alpha: " + err);
        }
        g_object_unref(srgb);
        out = withAlpha;
    }

    if (out->Bands != 4 || out->BandFmt != VIPS_FORMAT_UCHAR) {
        const std::string got = std::to_string(out->Bands) + " bands, format "
                              + std::to_string(static_cast<int>(out->BandFmt));
        g_object_unref(out);
        throw std::runtime_error(
            "ProcessingPipeline::previewPageRgba — expected 4-band 8-bit sRGB, got " + got);
    }

    const std::size_t nbytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    std::size_t       outSize = 0;
    void*             outData = vips_image_write_to_memory(out, &outSize);
    g_object_unref(out);
    if (!outData) {
        const std::string err = vips_error_buffer();
        vips_error_clear();
        throw std::runtime_error("ProcessingPipeline::previewPageRgba — read pixels: " + err);
    }
    std::memcpy(rgba, outData, std::min(outSize, nbytes));
    g_free(outData);
}

} // namespace Platemaker::Core
