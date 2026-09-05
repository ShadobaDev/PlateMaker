/**
 * \file lib/src/core/processing_pipeline/slice_writer.cpp
 * \brief SliceWriter implementation — the slice-and-save phase, lifted from the pipeline.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include "slice_writer.hpp"

#include "pipeline_log.hpp"

#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/image_io/image_io.hpp>

#include <iomanip>
#include <sstream>
#include <utility>

namespace Platemaker::Core {

using Platemaker::Infrastructure::FileMetaData;
using Platemaker::Infrastructure::ImageIO;
using Platemaker::Infrastructure::OutputLockedError;
using Platemaker::Infrastructure::ThumbnailCache;
using Platemaker::Models::LastSlicePolicy;
using Platemaker::Models::ProcessingErrorCategory;
using Platemaker::Models::ProcessingErrorCode;
using Platemaker::Models::ProcessingError;
using Platemaker::Models::ProcessingSliceRecord;

namespace {

//! Left-pads \p n with zeroes to \p width characters, e.g. 3 → "003".
std::string zeroPad(int n, int width)
{
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(width) << n;
    return oss.str();
}

//! The file extension the output format writes.
std::string formatExtension(Models::OutputFormat fmt)
{
    switch (fmt) {
        case Models::OutputFormat::PNG:  return ".png";
        case Models::OutputFormat::JPEG: return ".jpg";
        case Models::OutputFormat::WebP: return ".webp";
    }
    return ".png";
}

/**
 * \brief Resolves overlay page anchors against the assembled strip and decodes the bitmaps once.
 * \return The decoded overlays, at absolute strip coordinates.
 */
std::vector<LoadedOverlay> loadOverlaysForStrip(
    const std::vector<Models::StripOverlay>&    overlays,
    const std::unordered_map<std::string, int>& pageTopByInputUid,
    const ProcessingCallbacks&                  callbacks)
{
    std::vector<std::string> orphaned;
    const std::vector<Models::StripOverlay> placed =
        Models::resolveOverlayAnchors(overlays, pageTopByInputUid, &orphaned);

    for (const auto& uid : orphaned)
        emitLog(callbacks.onLog, ProcessingLogLevel::Warning,
                "Skipping overlay " + uid + ": the page it is anchored to is not in this render.");

    StripOverlayCompositor compositor;
    return compositor.load(placed);
}

} // namespace

int expectedSliceCount(int stripHeight, const Models::OutputProfile& outProfile)
{
    const int fullCount = stripHeight / outProfile.sliceHeight;
    const int remainder = stripHeight % outProfile.sliceHeight;
    return fullCount +
        ((remainder > 0 && outProfile.lastSlicePolicy != LastSlicePolicy::Crop) ? 1 : 0);
}

SliceWriter::SliceWriter(const RenderRequest&                        request,
                         const std::unordered_map<std::string, int>& pageTopByInputUid,
                         int                                         expectedTotal,
                         const ProcessingCallbacks&                  callbacks)
    : m_request(request)
    , m_callbacks(callbacks)
    , m_extension(formatExtension(request.outputProfile.outputFormat))
{
    // Partial render: the progress denominator is the number of slices we will actually save, not
    // the full count. Slice names derive from the index alone, so this is resolved up front — no
    // need to build the slices first.
    m_plannedTotal = expectedTotal;
    if (m_request.onlySlices) {
        m_plannedTotal = 0;
        for (int i = 0; i < expectedTotal; ++i)
            if (m_request.onlySlices->count(sliceFileName(i))) ++m_plannedTotal;
    }

    // Optional (Arch C — see the "render output contract" in the specification): pre-warm a
    // thumbnail cache from each slice's in-RAM pixels, so a consumer never re-reads a
    // freshly-written output to preview it. A bad dir just disables previews for this run.
    if (!m_request.thumbnailCacheDir.empty()) {
        try {
            m_thumbnails.emplace(m_request.thumbnailCacheDir);
        } catch (const std::exception& e) {
            emitLog(m_callbacks.onLog, ProcessingLogLevel::Warning,
                    std::string("Thumbnail previews disabled: ") + e.what());
        }
    }

    // Last, so the orphan warnings keep their historical position after the thumbnail one.
    m_overlays = loadOverlaysForStrip(m_request.stripOverlays, pageTopByInputUid, m_callbacks);
}

std::string SliceWriter::sliceFileName(int index) const
{
    return "output_" + zeroPad(m_request.outputProfile.startIndex + index, 3) + m_extension;
}

bool SliceWriter::writeSlice(SliceResult&& slice, ProcessingOutcome& outcome)
{
    const std::string outName = sliceFileName(slice.index);

    // Skip clean slices when a partial-render filter is supplied.
    if (m_request.onlySlices && m_request.onlySlices->count(outName) == 0) {
        if (m_callbacks.onSliceSkipped)
            m_callbacks.onSliceSkipped({slice.index, outName});
        return true;
    }

    const std::string outPath = m_request.outputDirectory + "/" + outName;

    if (!compositeOverlays(slice, outName, outcome))
        return false;
    if (!saveSlice(slice, outName, outPath, outcome))
        return false;

    warmThumbnail(slice, outName, outPath);

    ProcessingSliceRecord rec;
    rec.fileName     = outName;
    rec.outputSha256 = FileMetaData::computeFileSha256(outPath);
    rec.sourceMap    = std::move(slice.sourceMap);
    outcome.records.push_back(std::move(rec));

    ++m_savedCount;
    emitLog(m_callbacks.onLog, ProcessingLogLevel::Info, "Saved " + outName);
    if (m_callbacks.onProgress)
        m_callbacks.onProgress({m_savedCount, m_plannedTotal, outName});
    if (m_callbacks.onSliceSaved)
        m_callbacks.onSliceSaved({slice.index, outName, outPath});

    return true;
}

bool SliceWriter::compositeOverlays(SliceResult& slice, const std::string& outName,
                                    ProcessingOutcome& outcome) const
{
    // A slice no overlay touches is returned unchanged; a failure aborts the render.
    if (m_overlays.empty())
        return true;

    try {
        StripOverlayCompositor compositor;
        slice.image = compositor.apply(std::move(slice.image), slice.stripTopY, m_overlays);
    } catch (const std::exception& e) {
        outcome.failed = true;
        outcome.error  = ProcessingError{
            ProcessingErrorCode::SliceEncodeFailed, ProcessingErrorCategory::Encode,
            "Failed to composite overlays on '" + outName + "': " + e.what(), {}, outName};
        emitLog(m_callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return false;
    }
    return true;
}

bool SliceWriter::saveSlice(const SliceResult& slice, const std::string& outName,
                            const std::string& outPath, ProcessingOutcome& outcome) const
{
    try {
        ImageIO imageIO;
        imageIO.save(slice.image, outPath, m_request.outputProfile);
    } catch (const OutputLockedError& e) {
        outcome.failed = true;
        outcome.error  = ProcessingError{
            ProcessingErrorCode::OutputLocked, ProcessingErrorCategory::Io,
            e.what(), outPath, outName};
        emitLog(m_callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return false;
    } catch (const std::exception& e) {
        outcome.failed = true;
        outcome.error  = ProcessingError{
            ProcessingErrorCode::SliceEncodeFailed, ProcessingErrorCategory::Encode,
            "Failed to save '" + outName + "': " + e.what(), {}, outName};
        emitLog(m_callbacks.onLog, ProcessingLogLevel::Error, outcome.error->message);
        return false;
    }
    return true;
}

void SliceWriter::warmThumbnail(const SliceResult& slice, const std::string& outName,
                                const std::string& outPath)
{
    // Warmed before onSliceSaved fires, so a consumer's getOrGenerate(outPath) is a cache hit and
    // never re-reads the freshly-written output during the run. A preview failure must not fail the
    // render.
    if (!m_thumbnails)
        return;

    try {
        m_thumbnails->generate(outPath, slice.image);
    } catch (const std::exception& e) {
        emitLog(m_callbacks.onLog, ProcessingLogLevel::Warning,
                "Thumbnail preview for '" + outName + "' skipped: " + e.what());
    }
}

} // namespace Platemaker::Core
