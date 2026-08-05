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

#include <vips/vips.h>

#include <functional>
#include <iomanip>
#include <sstream>

namespace Platemaker::Core {

namespace {

// Reads an image's pixel dimension from its header only (no pixel decode).
// Returns -1 on error.
int headerDim(const std::string& filePath, bool wantWidth)
{
    VipsImage* img = vips_image_new_from_file(
        filePath.c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!img) {
        vips_error_clear();
        return -1;
    }
    const int dim = wantWidth ? img->Xsize : img->Ysize;
    g_object_unref(img);
    return dim;
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
    const std::unordered_set<std::string>*    onlySlices)
{
    using namespace Platemaker::Models;
    using Platemaker::Infrastructure::FileMetaData;
    using Platemaker::Infrastructure::ImageIO;

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
            const CanvasProfile* matchedProfile = nullptr;
            if (hasProfiles) {
                const int w = headerDim(file.filePath, true);
                const int h = headerDim(file.filePath, false);
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
                               : std::string{}});

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
        } catch (const std::exception& e) {
            outcome.failed = true;
            outcome.error  = ProcessingError{
                ProcessingErrorCode::SliceEncodeFailed, ProcessingErrorCategory::Encode,
                "Failed to save '" + outName + "': " + e.what(), {}, outName};
            emitLog(callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
            return false;   // stop slicing
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
