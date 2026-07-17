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

void emitLog(const ProcessingPipeline::LogFn& onLog,
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
    const ProgressFn&                         onProgress,
    const LogFn&                              onLog,
    const SliceSavedFn&                       onSliceSaved,
    const std::unordered_set<std::string>*    onlySlices) const
{
    using namespace Platemaker::Models;
    using Platemaker::Infrastructure::FileMetaData;
    using Platemaker::Infrastructure::ImageIO;

    ProcessingOutcome outcome;

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
            emitLog(onLog, ProcessingLogLevel::Warning,
                    "Skipping missing file: " + file.filePath);
            outcome.skippedPages.push_back(file.filePath);
            continue;
        }

        try {
            const CanvasProfile* matchedProfile = nullptr;
            if (hasProfiles) {
                const int w = headerDim(file.filePath, true);
                const int h = headerDim(file.filePath, false);
                if (w <= 0 || h <= 0)
                    throw std::runtime_error("cannot determine image dimensions");

                const auto result = matcher.resolve(w, h);
                if (result.status != ProfileMatchResult::Status::Matched) {
                    emitLog(onLog, ProcessingLogLevel::Warning,
                            "Skipping (no matching canvas profile for "
                            + std::to_string(w) + "x" + std::to_string(h) + "): "
                            + file.filePath);
                    outcome.skippedPages.push_back(file.filePath);
                    continue;
                }
                matchedProfile = result.profile;
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
        } catch (const std::exception& e) {
            emitLog(onLog, ProcessingLogLevel::Warning,
                    std::string("Skipping (") + e.what() + "): " + file.filePath);
            outcome.skippedPages.push_back(file.filePath);
        }
    }

    if (strip.totalHeight() == 0) {
        outcome.failed       = true;
        outcome.errorMessage = "No pages were loaded successfully.";
        emitLog(onLog, ProcessingLogLevel::Error, outcome.errorMessage);
        return outcome;
    }

    // Expected slice count (used as the progress denominator so it stays correct
    // even if the run is cancelled before all slices are produced).
    const int totalH    = strip.totalHeight();
    const int fullCount = totalH / outProfile.sliceHeight;
    const int remainder = totalH % outProfile.sliceHeight;
    const int expectedTotal = fullCount +
        ((remainder > 0 && outProfile.lastSlicePolicy != LastSlicePolicy::Crop) ? 1 : 0);

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
        if (onlySlices && onlySlices->count(outName) == 0)
            return true;

        const std::string outPath = outputDir + "/" + outName;

        try {
            imageIO.save(slice.image, outPath, outProfile);
        } catch (const std::exception& e) {
            outcome.failed       = true;
            outcome.errorMessage = "Failed to save '" + outName + "': " + e.what();
            emitLog(onLog, ProcessingLogLevel::Error, outcome.errorMessage);
            return false;   // stop slicing
        }

        ProcessingSliceRecord rec;
        rec.fileName     = outName;
        rec.outputSha256 = FileMetaData::computeFileSha256(outPath);
        rec.sourceMap    = std::move(slice.sourceMap);
        outcome.records.push_back(std::move(rec));

        ++savedCount;
        emitLog(onLog, ProcessingLogLevel::Info, "Saved " + outName);
        if (onProgress)
            onProgress({savedCount, renderTotal, outName});
        if (onSliceSaved)
            onSliceSaved(outName, outPath);

        return true;
    };

    try {
        strip.sliceAll(outProfile.sliceHeight, outProfile.lastSlicePolicy,
                       cancel, onSlice);
    } catch (const std::exception& e) {
        outcome.failed       = true;
        outcome.errorMessage = std::string("Slicing failed: ") + e.what();
        emitLog(onLog, ProcessingLogLevel::Error, outcome.errorMessage);
        return outcome;
    }

    // A save error already set `failed` and stopped the run; don't mask it.
    if (outcome.failed)
        return outcome;

    // If slicing was cut short by cancellation, flag it.
    if (cancel.isCancelled())
        outcome.cancelled = true;

    return outcome;
}

} // namespace Platemaker::Core
