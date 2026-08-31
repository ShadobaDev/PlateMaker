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

#include <functional>
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
    int                        stripTopY = 0; //!< Y of this slice's top within the continuous strip (strip coordinates).
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
 * Images are appended one at a time via \c append().  Slicing is triggered by a single
 * call to \c sliceAll(), which streams each finished slice to a callback.
 *
 * ### Memory behaviour
 *
 * **Only the sources overlapping the slice currently being built hold decoded pixels.**
 * That is the invariant — deliberately *not* a fixed number. How many sources that is
 * depends entirely on the data:
 *
 * - Sources taller than \p sliceHeight (the usual case): one when the slice falls inside
 *   a single source, two when it straddles a source boundary.
 * - Sources shorter than \p sliceHeight: as many as it takes to fill the slice. A 200px
 *   leftover tail + a 500px extra panel + a 100px spacer + 480px off the next page all
 *   feed one 1280px slice — four sources live at once, and every one of them is required.
 *
 * The set is therefore the minimum the slice cannot be built without, which is the most
 * that can honestly be promised. It is the product of two cooperating mechanisms, both
 * load-bearing:
 *
 * 1. **Decoding is lazy.**  \c append() takes a libvips image that is merely a promise:
 *    no pixels exist until something reads them.  A source is therefore decoded only when
 *    the first slice overlapping it is actually computed — not when it is appended.
 * 2. **\c sliceAll() releases what it has passed.**  Slices advance in strictly increasing
 *    Y, so once a source lies entirely above the current slice it can never contribute
 *    again.  \c sliceAll() drops such sources' pixel buffers as it goes, letting libvips
 *    free the decoded data.
 *
 * Without (2) the strip would retain every decoded source for the whole run — note this is
 * the *pre-scaling* original (Scaler loads with \c VIPS_ACCESS_RANDOM, which caches the full
 * image, and the resize on top of it stays lazy), so the cost would be the full-resolution
 * inputs, not the scaled strip.  Peak memory would grow linearly with page count.
 *
 * \note The strip width is determined by the first image appended.  All subsequent
 *       images must have the same width; the pipeline enforces this by passing all
 *       sources through Scaler with the same targetWidth.
 *
 * \warning \c sliceAll() consumes the strip: it releases source pixels as it advances, so
 *          it may only be called once.  Afterwards the instance must be discarded.
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
     * \brief Receives each finished slice, in order of increasing Y.
     *
     * The slice is moved in, so the handler owns it and its pixels are freed as soon as
     * the handler returns — keeping one slice in flight at a time.
     *
     * \return \c true to continue slicing, \c false to stop (e.g. after a write error).
     */
    using SliceFn = std::function<bool(SliceResult&&)>;

    /**
     * \brief Slices the entire accumulated strip, streaming each slice to \p onSlice.
     *
     * Produces \c floor(totalHeight / sliceHeight) full slices plus one tail slice
     * if \c (totalHeight \% sliceHeight) \> 0, subject to \p policy.  Slices are handed
     * to \p onSlice one at a time rather than collected, and each source's pixels are
     * released once no remaining slice can reference it (see the class-level "Memory
     * behaviour" notes) — this is why the method is non-const and single-use.
     *
     * Stops early, without error, when \p cancelToken is signalled or \p onSlice returns
     * \c false; slices already handed over stay handed over.  The caller distinguishes the
     * two by inspecting the token.
     *
     * \param sliceHeight  Height of each full output slice in pixels.  Must be > 0.
     * \param policy       How the tail (remainder) slice is handled.
     * \param cancelToken  Token polled between slices.  Const-ref; the strip does not own it.
     * \param onSlice      Invoked with each finished slice.  Must not be empty.
     *
     * \throws std::runtime_error if the strip is empty, \p sliceHeight is invalid, or a
     *         libvips operation fails while building a slice.
     */
    void sliceAll(
        int                                        sliceHeight,
        Models::LastSlicePolicy                    policy,
        const Infrastructure::CancellationToken&   cancelToken,
        const SliceFn&                             onSlice);

private:
    struct Entry {
        ScaledImage image;   //!< Scaled image data.  Its buffer is released once the strip has sliced past it.
        int         startY;  //!< Y offset of this image's top within the full virtual strip.
        int         height;  //!< Image height, cached at append() — stays valid after the buffer is released.
    };

    std::vector<Entry> m_entries; //!< Appended images in order.
    int m_totalHeight = 0;        //!< Running total of appended heights.
    int m_width       = 0;        //!< Strip width (set on first append).

    /**
     * \brief Extracts one slice from the accumulated entries.
     *
     * Accesses \c m_entries and \c m_width to compute the output image.  The returned
     * image is a lazy libvips graph over the overlapping sources, not decoded pixels.
     *
     * \param index       0-based output slice index stored in the result.
     * \param sliceStartY Y start position of the slice in the virtual strip.
     * \param sliceH      Height of the slice to extract in pixels.
     * \return A SliceResult owning the composited pixel data and sourceMap.
     *
     * \throws std::runtime_error if no entry overlaps the slice, a libvips operation
     *         fails, or an overlapping entry's buffer was already released (which would
     *         mean releaseConsumedEntries() ran ahead of the slice cursor).
     */
    [[nodiscard]] SliceResult buildSlice(int index, int sliceStartY, int sliceH) const;

    /**
     * \brief Releases the pixel buffers of entries lying entirely above \p sliceStartY.
     *
     * Called by \c sliceAll() before each slice.  Since slices advance in increasing Y,
     * such entries can no longer contribute to any remaining slice, so dropping the buffer
     * lets libvips free the decoded source.  Entries themselves stay in \c m_entries —
     * their \c startY / \c height keep the strip's geometry intact.
     *
     * \param sliceStartY Y position of the slice about to be built.
     */
    void releaseConsumedEntries(int sliceStartY) noexcept;

    /**
     * \brief Normalises every entry to one joinable, homogeneous band layout.
     *
     * Called once at the top of \c sliceAll(), before any slice is built or any buffer is
     * released, so every entry still holds its pixels.  \c vips_join (used by \c buildSlice()
     * to stack the parts of a multi-source slice) requires all inputs to share a band count;
     * a strip that mixes RGB (3-band) and RGBA (4-band) sources would otherwise abort the
     * whole render at the first slice straddling that boundary.
     *
     * Normalisation is **promote-only** — bands are added, never removed, so the user's
     * pixels are never composited onto a background here:
     *   1. Any non-RGB colourspace (grayscale, CMYK, …) is converted to sRGB.  Lossless for
     *      grayscale; already-RGB/RGBA pixels are left untouched.
     *   2. Entries with fewer bands than the strip's maximum gain a fully-opaque alpha
     *      channel (\c vips_addalpha).  If no source has alpha the strip stays RGB.
     *
     * The operations are lazy and preserve random access, and they change neither width nor
     * height, so the strip geometry and the slice-height invariant are unaffected.  Alpha is
     * dropped only later, at save time, and only for formats (JPEG) that cannot carry it.
     *
     * \throws std::runtime_error if a libvips band operation fails.
     */
    void normalizeBandCounts();
}; // class ScaledStrip

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_SCALED_STRIP_HPP
