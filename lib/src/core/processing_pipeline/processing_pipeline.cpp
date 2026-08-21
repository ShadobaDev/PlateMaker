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
#include <platemaker/core/margin_cropper/margin_cropper.hpp>
#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/core/scaler/scaler.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/infrastructure/log/log.hpp>
#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>

#include <optional>

#include <vips/vips.h>

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
    const std::string&                        thumbnailCacheDir)
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

    Scaler        scaler;
    MarginCropper cropper;
    ImageIO       imageIO;
    ScaledStrip   strip;

    namespace Log = Platemaker::Infrastructure::Log;

    const bool hasProfiles = !canvasProfiles.empty();
    CanvasProfileMatcher matcher(canvasProfiles, canvasProfileIds);

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

        // Status reported to onInput after the page is appended. Stays Appended for a matched
        // profile (or a project with no profiles at all); the two mismatch cases below downgrade it
        // to an implicit-render status but the page is still rendered — never dropped.
        InputStatus              appendStatus = InputStatus::Appended;
        std::vector<std::string> candidateIds;

        try {
            // Read the header unconditionally: the display W×H is recorded per input (below) so a later
            // run can tell offline which canvas profile each page would match now — matching is purely by
            // W×H, and without the stored dimensions any profile-list change degrades to a whole-project
            // re-render. A header read is negligible beside the decode + scale that follow.
            const HeaderGeometry geo = headerGeometry(file.filePath);
            PLATEMAKER_LOG(Log::ProcessingPipeline,
                    "read " + file.filePath + ": header "
                    + std::to_string(geo.width) + "x" + std::to_string(geo.height)
                    + ", EXIF orientation "
                    + (geo.hasOrientation ? std::to_string(geo.orientation)
                                          : std::string("none")));

            const CanvasProfile* matchedProfile = nullptr;
            if (hasProfiles) {
                const int w = geo.width;
                const int h = geo.height;
                if (w <= 0 || h <= 0)
                    throw std::runtime_error("cannot determine image dimensions");

                const std::string dims = std::to_string(w) + "x" + std::to_string(h);
                const auto result = matcher.resolve(w, h);
                if (result.status == ProfileMatchResult::Status::Matched) {
                    matchedProfile = result.profile;
                    emitLog(callbacks.onLog, ProcessingLogLevel::Info,
                            "Applied canvas profile '" + matchedProfile->name + "' (" + dims
                            + ") to " + file.filePath);
                } else if (result.status == ProfileMatchResult::Status::FoundInWorkspaceOnly) {
                    // A same-size profile exists in the workspace but is not linked to this project.
                    // Render implicitly (no margins) rather than drop the page, but say so loudly:
                    // linking the profile is a one-click fix that would apply its margins.
                    appendStatus = InputStatus::AppendedProfileNotLinked;
                    candidateIds.reserve(result.workspaceCandidates.size());
                    for (const auto* cand : result.workspaceCandidates)
                        candidateIds.push_back(cand->id);
                    const std::string name = result.workspaceCandidates.empty()
                                                 ? std::string{}
                                                 : result.workspaceCandidates.front()->name;
                    emitLog(callbacks.onLog, ProcessingLogLevel::Warning,
                            "No linked canvas profile matches " + dims + " — rendering " + file.filePath
                            + " without margins; profile '" + name + "' (" + dims
                            + ") exists in the workspace but is not linked to this project."
                            + " Link it to apply its margins.");
                } else {
                    // No profile of this size exists anywhere — render the page implicitly (scaled,
                    // no margins), exactly the no-profiles path. Determinism is preserved by
                    // visibility (the input is flagged) rather than by omission.
                    appendStatus = InputStatus::AppendedWithoutProfile;
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
                matchedProfile ? matchedProfile->id : std::string{},
                matchedProfile ? Models::canvasRenderFingerprint(*matchedProfile)
                               : std::string{},
                geo.width,
                geo.height});

            const bool doMarginCrop =
                matchedProfile != nullptr &&
                (matchedProfile->margins.top    > 0 ||
                 matchedProfile->margins.bottom > 0 ||
                 matchedProfile->margins.left   > 0 ||
                 matchedProfile->margins.right  > 0);

            if (doMarginCrop) {
                auto buf     = imageIO.load(file.filePath);
                auto cropped = cropper.crop(buf, matchedProfile->margins);
                auto scaled  = scaler.scale(std::move(cropped), file.filePath,
                                            outProfile.targetWidth);
                strip.append(std::move(scaled));
            } else {
                auto scaled = scaler.scale(file.filePath, outProfile.targetWidth);
                strip.append(std::move(scaled));
            }
            if (callbacks.onInput)
                callbacks.onInput({file.filePath, appendStatus, std::move(candidateIds), {}});
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

} // namespace Platemaker::Core
