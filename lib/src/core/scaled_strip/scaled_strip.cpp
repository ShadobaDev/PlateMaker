/**
 * \file lib/src/core/scaled_strip/scaled_strip.cpp
 * \brief ScaledStrip implementation — virtual strip accumulator and slice engine.
 *
 * The strip accumulates ScaledImage objects and slices them into fixed-height
 * output panels.  libvips deferred evaluation ensures that pixel data for a
 * given source image is only computed when it is actually needed to assemble
 * the next output slice.  At most two source images are held in memory
 * simultaneously when a slice boundary straddles a file boundary.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/core/scaled_strip/scaled_strip.hpp>

#include <vips/vips.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace Platemaker::Core {

// ---------------------------------------------------------------------------
// append
// ---------------------------------------------------------------------------

void ScaledStrip::append(ScaledImage image)
{
    if (m_width == 0) {
        m_width = image.buffer.width();
    }

    Entry entry;
    entry.startY = m_totalHeight;
    entry.image  = std::move(image);
    m_totalHeight += entry.image.buffer.height();
    m_entries.push_back(std::move(entry));
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int ScaledStrip::totalHeight() const noexcept { return m_totalHeight; }
int ScaledStrip::width()       const noexcept { return m_width; }

// ---------------------------------------------------------------------------
// buildSlice (private)
// ---------------------------------------------------------------------------

SliceResult ScaledStrip::buildSlice(int index, int sliceStartY, int sliceH) const
{
    const int sliceEndY = sliceStartY + sliceH;

    SliceResult result;
    result.index = index;

    // Collect one extracted sub-image per source entry that overlaps this slice.
    // We own these intermediate images until they are either transferred to
    // result.image (single-part case) or joined and then freed (multi-part).
    std::vector<VipsImage*> parts;
    parts.reserve(2); // at most 2 in the common case

    for (const auto& entry : m_entries) {
        const int entryEndY = entry.startY + entry.image.buffer.height();

        if (entryEndY <= sliceStartY) continue; // entirely above this slice
        if (entry.startY >= sliceEndY) break;   // entirely below — entries are ordered

        // Intersection in virtual-strip coordinates.
        const int overlapStart = std::max(entry.startY, sliceStartY);
        const int overlapEnd   = std::min(entryEndY,    sliceEndY);
        const int overlapH     = overlapEnd - overlapStart;

        // Convert to this entry's local Y coordinate space.
        const int localSrcY = overlapStart - entry.startY;

        VipsImage* part = nullptr;
        if (vips_extract_area(entry.image.buffer.get(), &part,
                0, localSrcY, m_width, overlapH, nullptr) != 0)
        {
            for (VipsImage* p : parts) g_object_unref(p);
            throw std::runtime_error(
                "ScaledStrip::buildSlice() — vips_extract_area failed "
                "(slice " + std::to_string(index) + "): " +
                vips_error_buffer());
        }

        // Record provenance for incremental processing.
        SourceSegment seg;
        seg.sourceFilePath = entry.image.sourceFilePath;
        seg.srcY           = localSrcY;
        seg.height         = overlapH;
        result.sourceMap.push_back(std::move(seg));

        parts.push_back(part);
    }

    if (parts.empty()) {
        throw std::runtime_error(
            "ScaledStrip::buildSlice() — no source entries overlap slice " +
            std::to_string(index));
    }

    if (parts.size() == 1) {
        // Single-source slice: no join needed.
        result.image = PixelBuffer{parts[0]};
        // Ownership transferred to PixelBuffer — do not unref.
    } else {
        // Multi-source slice: join vertically (across=1 → single column).
        VipsImage* joined = nullptr;
        if (vips_arrayjoin(parts.data(), &joined,
                static_cast<int>(parts.size()),
                "across", 1,
                nullptr) != 0)
        {
            for (VipsImage* p : parts) g_object_unref(p);
            throw std::runtime_error(
                "ScaledStrip::buildSlice() — vips_arrayjoin failed "
                "(slice " + std::to_string(index) + "): " +
                vips_error_buffer());
        }
        // vips_arrayjoin adds its own reference to the inputs; safe to unref ours.
        for (VipsImage* p : parts) g_object_unref(p);
        result.image = PixelBuffer{joined};
    }

    return result;
}

// ---------------------------------------------------------------------------
// sliceAll — no-cancel overload
// ---------------------------------------------------------------------------

std::vector<SliceResult> ScaledStrip::sliceAll(
    int                     sliceHeight,
    Models::LastSlicePolicy policy) const
{
    // Construct a dummy no-op token so we share one implementation path.
    Infrastructure::CancellationToken noop;
    return sliceAll(sliceHeight, policy, noop);
}

// ---------------------------------------------------------------------------
// sliceAll — cancellable overload
// ---------------------------------------------------------------------------

std::vector<SliceResult> ScaledStrip::sliceAll(
    int                                      sliceHeight,
    Models::LastSlicePolicy                  policy,
    const Infrastructure::CancellationToken& cancelToken) const
{
    if (m_entries.empty()) {
        throw std::runtime_error("ScaledStrip::sliceAll() — strip is empty");
    }
    if (sliceHeight <= 0) {
        throw std::runtime_error(
            "ScaledStrip::sliceAll() — sliceHeight must be > 0 (got " +
            std::to_string(sliceHeight) + ")");
    }

    const int numFull = m_totalHeight / sliceHeight;
    const int tail    = m_totalHeight % sliceHeight;

    std::vector<SliceResult> results;
    results.reserve(static_cast<std::size_t>(numFull) + (tail > 0 ? 1 : 0));

    // --- Full slices ---
    for (int i = 0; i < numFull; ++i) {
        if (cancelToken.isCancelled()) return results;
        results.push_back(buildSlice(i, i * sliceHeight, sliceHeight));
    }

    // --- Tail slice ---
    if (tail > 0 && !cancelToken.isCancelled()) {
        const int tailStartY = numFull * sliceHeight;

        switch (policy) {
            case Models::LastSlicePolicy::Crop:
                // Discard the tail — do nothing.
                break;

            case Models::LastSlicePolicy::KeepAsIs:
                results.push_back(buildSlice(numFull, tailStartY, tail));
                break;

            case Models::LastSlicePolicy::PadWhite: {
                SliceResult tailSlice = buildSlice(numFull, tailStartY, tail);

                // Embed the tail image into a white canvas of the full sliceHeight.
                // vips_embed places the image at (0,0) and fills the remainder with
                // the VIPS_EXTEND_WHITE strategy (solid white).
                VipsImage* padded = nullptr;
                if (vips_embed(tailSlice.image.get(), &padded,
                        0, 0, m_width, sliceHeight,
                        "extend", VIPS_EXTEND_WHITE,
                        nullptr) != 0)
                {
                    throw std::runtime_error(
                        "ScaledStrip::sliceAll() (PadWhite) — vips_embed failed: " +
                        std::string(vips_error_buffer()));
                }
                tailSlice.image = PixelBuffer{padded};
                results.push_back(std::move(tailSlice));
                break;
            }
        }
    }

    return results;
}

} // namespace Platemaker::Core
