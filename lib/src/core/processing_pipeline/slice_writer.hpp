/**
 * \file lib/src/core/processing_pipeline/slice_writer.hpp
 * \brief SliceWriter — phase 2: composites, encodes and records one slice at a time.
 *
 * Internal to the pipeline: this header lives under \c src/ and is never installed.
 *
 * The strip *streams* slices out and releases source images as it goes, so a slice has to be
 * written the moment it arrives rather than collected into a list — that streaming is exactly what
 * keeps only one slice and roughly two sources alive at a time, whatever the chapter's length.
 * This class is therefore a sink: \c writeSlice() is called back once per slice, and returning
 * \c false is how it stops the strip mid-flight when a save fails.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_PROCESSING_PIPELINE_SLICE_WRITER_HPP
#define PLATEMAKER_CORE_PROCESSING_PIPELINE_SLICE_WRITER_HPP

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <platemaker/core/processing_callbacks/processing_callbacks.hpp>
#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/core/processing_pipeline/render_request.hpp>
#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>
#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/processing_steps.hpp>

namespace Platemaker::Core {

/**
 * \brief How many slices an assembled strip will produce, before any partial-render filter.
 *
 * Used as the progress denominator, so it stays correct even when a run is cancelled before all
 * slices exist.  Whether the final short slice counts depends on the profile's last-slice policy.
 *
 * \param stripHeight Total height of the assembled strip, in pixels.
 * \param outProfile  Supplies \c sliceHeight and \c lastSlicePolicy.
 * \return The expected slice count.
 */
[[nodiscard]] int expectedSliceCount(int stripHeight, const Models::OutputProfile& outProfile);

/**
 * \class SliceWriter
 * \brief Writes each slice the strip streams out: composite → encode → hash → record → report.
 */
class SliceWriter {
public:
    /**
     * \brief Binds to everything the whole phase shares.
     *
     * Reads five fields off the request — \c outputProfile, \c outputDirectory, \c onlySlices,
     * \c thumbnailCacheDir and \c stripOverlays — so it takes the request rather than five loose
     * arguments whose order a caller could get wrong.
     *
     * The overlays' page anchors are resolved here, against \p pageTopByInputUid, and the bitmaps
     * decoded once.  An overlay anchored to a page that did not load has nowhere to go: it is
     * reported and dropped rather than allowed to land on whatever page took its place.
     *
     * \param request           The render request. Must outlive this writer.
     * \param pageTopByInputUid Strip layout from \c StripBuilder::pageTopByInputUid().
     * \param expectedTotal     Slice count from \c expectedSliceCount(), before the filter.
     * \param callbacks         Progress/log/slice sinks; any field may be null.
     */
    SliceWriter(const RenderRequest&                        request,
                const std::unordered_map<std::string, int>& pageTopByInputUid,
                int                                         expectedTotal,
                const ProcessingCallbacks&                  callbacks);

    /**
     * \brief The output file name for slice \p index, e.g. "output_003.png".
     *
     * Derived from the index alone, which is what lets the partial-render filter be resolved up
     * front instead of after the slices exist.
     */
    [[nodiscard]] std::string sliceFileName(int index) const;

    //! How many slices this writer will actually save — the filtered count, for \c reserve().
    [[nodiscard]] int plannedSliceCount() const noexcept { return m_plannedTotal; }

    /**
     * \brief Handles one slice streamed out of the strip.
     *
     * \param slice   The slice, consumed.
     * \param outcome Accumulates the per-slice record, or the fatal error that stops the run.
     * \return \c true to continue slicing; \c false to stop (a fatal error is on \p outcome).
     */
    bool writeSlice(SliceResult&& slice, ProcessingOutcome& outcome);

private:
    //! Composites the overlays intersecting this slice. False → \p outcome carries a fatal error.
    bool compositeOverlays(SliceResult& slice, const std::string& outName,
                           ProcessingOutcome& outcome) const;

    //! Encodes and writes the slice. False → \p outcome carries a fatal error.
    bool saveSlice(const SliceResult& slice, const std::string& outName,
                   const std::string& outPath, ProcessingOutcome& outcome) const;

    //! Best-effort preview warm-up; a failure is logged, never fatal.
    void warmThumbnail(const SliceResult& slice, const std::string& outName,
                       const std::string& outPath);

    const RenderRequest&       m_request;
    const ProcessingCallbacks& m_callbacks;

    std::string                m_extension;  //!< File extension for the profile's format.
    std::vector<LoadedOverlay> m_overlays;
    std::optional<Infrastructure::ThumbnailCache> m_thumbnails;

    int m_plannedTotal = 0;  //!< Slices this run will save — the progress denominator.
    int m_savedCount   = 0;  //!< Slices saved so far.
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PROCESSING_PIPELINE_SLICE_WRITER_HPP
