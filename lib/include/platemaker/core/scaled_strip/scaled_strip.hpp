/**
 * \file lib/include/platemaker/core/scaled_strip/scaled_strip.hpp
 * \brief ScaledStrip — the virtual strip abstraction that accumulates scaled images
 *        and produces output slices.
 *
 * Also defines SliceResult type that carry provenance
 * information for incremental processing.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_CORE_SCALED_STRIP_HPP
#define PLATEMAKER_CORE_SCALED_STRIP_HPP

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/core/scaler/scaler.hpp>
#include <platemaker/models/common_types.hpp>
#include <platemaker/models/project_item.hpp>

namespace Platemaker::Core {

// ---------------------------------------------------------------------------
// Helper types (companions of ScaledStrip, not primary classes)
// ---------------------------------------------------------------------------

/**
 * \brief The result of slicing one segment out of the virtual strip.
 *
 * Produced by \c ScaledStrip::sliceAll().  Each SliceResult corresponds to
 * exactly one output file.
 *
 * \note SliceResult is move-only because PixelBuffer is move-only.
 */
struct PLATEMAKER_EXPORT SliceResult {
    PixelBuffer                image;       //!< Pixel data for this output slice.
    int                        index = 0;   //!< 0-based output index — use with startIndex to build the filename.
    std::vector<Models::SourceSegment> sourceMap;   //!< Provenance: one entry per source file that contributed pixels to this slice.
};

// ---------------------------------------------------------------------------
// ScaledStrip
// ---------------------------------------------------------------------------

/**
 * \class ScaledStrip
 * \brief Streaming accumulator that treats an ordered list of scaled images as a
 *        single continuous virtual strip and slices it into fixed-height output panels.
 *
 * Images are appended one at a time via \c append().  The strip internally retains
 * only the minimum number of scaled images needed to complete the next output slice
 * (at most two images are in memory simultaneously when a slice boundary falls across
 * a file boundary).  As soon as an image's pixels are no longer required by any
 * future slice it is freed immediately.
 *
 * Slicing is triggered by a single call to \c sliceAll(), which processes the
 * accumulated strip and returns all SliceResult objects.
 *
 * \note The strip width is determined by the first image appended.  All subsequent
 *       images must have the same width; the pipeline enforces this by passing all
 *       sources through Scaler with the same targetWidth.
 *
 * \warning \c sliceAll() may only be called once.  After slicing, the strip is in
 *          an undefined state and the instance should be discarded.
 */
class PLATEMAKER_EXPORT ScaledStrip {
public:
    ScaledStrip() = default;

    // Non-copyable, movable.
    ScaledStrip(const ScaledStrip&)            = delete;
    ScaledStrip& operator=(const ScaledStrip&) = delete;
    ScaledStrip(ScaledStrip&&) noexcept            = default;
    ScaledStrip& operator=(ScaledStrip&&) noexcept = default;

    // ---------------------------------------------------------------------------
    // Building the strip
    // ---------------------------------------------------------------------------

    /**
     * \brief Appends a scaled image to the bottom of the virtual strip.
     *
     * Takes ownership of \p image.  The strip may free previously appended images
     * immediately after this call if they are no longer needed to assemble the
     * next slice.
     *
     * \param image A ScaledImage to append.  Must have the same width as the first
     *              image appended to this strip.
     */
    void append(ScaledImage image);

    // ---------------------------------------------------------------------------
    // Querying
    // ---------------------------------------------------------------------------

    /**
     * \brief Returns the total accumulated height of the virtual strip in pixels.
     *
     * Equals the sum of the heights of all appended images.
     *
     * \return Total height in pixels.
     */
    [[nodiscard]] int totalHeight() const noexcept;

    /**
     * \brief Returns the width of the strip in pixels.
     *
     * Determined by the first image appended.  Returns 0 before any image is appended.
     *
     * \return Strip width in pixels.
     */
    [[nodiscard]] int width() const noexcept;

    // ---------------------------------------------------------------------------
    // Slicing
    // ---------------------------------------------------------------------------

    /**
     * \brief Slices the entire accumulated strip and returns all output slices.
     *
     * Produces \c floor(totalHeight / sliceHeight) full slices plus one tail slice
     * if \c (totalHeight \% sliceHeight) \> 0, subject to \p policy.
     *
     * \param sliceHeight Height of each full output slice in pixels.  Must be > 0.
     * \param policy      How the tail (remainder) slice is handled.
     * \return A vector of SliceResult objects in order, each owning its pixel data.
     *
     * \throws std::runtime_error if the strip is empty or sliceHeight is invalid.
     */
    [[nodiscard]] std::vector<SliceResult> sliceAll(
        int                        sliceHeight,
        Models::LastSlicePolicy    policy) const;

    /**
     * \brief Slices the strip with cancellation support.
     *
     * Same as the two-argument overload but polls \p cancelToken between each
     * slice.  If cancellation is requested, the method returns the slices that
     * have been completed so far (partial result); the caller can detect
     * cancellation by comparing result size against the expected slice count.
     *
     * \param sliceHeight  Height of each full output slice in pixels.
     * \param policy       How the tail slice is handled.
     * \param cancelToken  Token polled between slices.  Const-ref; the strip does not own it.
     * \return Completed SliceResult objects up to the point of cancellation.
     */
    [[nodiscard]] std::vector<SliceResult> sliceAll(
        int                                        sliceHeight,
        Models::LastSlicePolicy                    policy,
        const Infrastructure::CancellationToken&   cancelToken) const;

private:
    struct Entry {
        ScaledImage image;   //!< Scaled image data.
        int         startY;  //!< Y offset of this image's top within the full virtual strip.
    };

    std::vector<Entry> m_entries; //!< Appended images in order.
    int m_totalHeight = 0;        //!< Running total of appended heights.
    int m_width       = 0;        //!< Strip width (set on first append).

    /**
     * \brief Extracts one slice from the accumulated entries.
     *
     * Accesses \c m_entries and \c m_width to compute the output image.
     *
     * \param index       0-based output slice index stored in the result.
     * \param sliceStartY Y start position of the slice in the virtual strip.
     * \param sliceH      Height of the slice to extract in pixels.
     * \return A SliceResult owning the composited pixel data and sourceMap.
     */
    [[nodiscard]] SliceResult buildSlice(int index, int sliceStartY, int sliceH) const;
}; // class ScaledStrip

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_SCALED_STRIP_HPP
