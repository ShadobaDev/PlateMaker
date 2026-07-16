/**
 * \file lib/src/core/scaled_strip/scaled_strip.cpp
 * \brief ScaledStrip implementation — virtual strip accumulator and slice engine.
 *
 * The strip accumulates ScaledImage objects and slices them into fixed-height
 * output panels.  libvips deferred evaluation ensures that pixel data for a
 * given source image is only computed when it is actually needed to assemble
 * the next output slice; sliceAll() then releases each source once the slice
 * cursor has passed it.  Together these keep only the sources overlapping the
 * current slice decoded — usually one or two, but as many as it takes when the
 * sources are shorter than a slice.  See the class docs for the full contract.
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
    // Cache the height now: sliceAll() releases buffers as it advances, and a released
    // buffer reports height 0 — the strip's geometry must survive that.
    entry.height = image.buffer.height();
    entry.image  = std::move(image);
    m_totalHeight += entry.height;
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
        // Geometry comes from the cached height, never from the buffer: entries we have
        // already sliced past have had their buffers released and report height 0.
        const int entryEndY = entry.startY + entry.height;

        if (entryEndY <= sliceStartY) continue; // entirely above this slice
        if (entry.startY >= sliceEndY) break;   // entirely below — entries are ordered

        // An overlapping entry must still hold its pixels. If it does not,
        // releaseConsumedEntries() ran ahead of the slice cursor — a logic error here,
        // not bad input, and one that would otherwise surface as a confusing vips failure.
        if (!entry.image.buffer.isValid()) {
            for (VipsImage* p : parts) g_object_unref(p);
            throw std::runtime_error(
                "ScaledStrip::buildSlice() — source '" + entry.image.sourceFilePath +
                "' overlaps slice " + std::to_string(index) +
                " but its buffer was already released");
        }

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
        Models::SourceSegment seg;
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
// releaseConsumedEntries (private)
// ---------------------------------------------------------------------------

void ScaledStrip::releaseConsumedEntries(int sliceStartY) noexcept
{
    // Slices advance in increasing Y, so an entry ending at or above the next slice's
    // top can never contribute again. Dropping the buffer unrefs the VipsImage, which
    // lets libvips free the decoded source — this is what keeps peak memory flat.
    // The entry stays in place; its startY/height still define the strip's geometry.
    for (auto& entry : m_entries) {
        if (entry.startY >= sliceStartY) break;  // entries are ordered — rest are below
        if (entry.startY + entry.height <= sliceStartY)
            entry.image.buffer = PixelBuffer{};
    }
}

// ---------------------------------------------------------------------------
// sliceAll
// ---------------------------------------------------------------------------

void ScaledStrip::sliceAll(
    int                                      sliceHeight,
    Models::LastSlicePolicy                  policy,
    const Infrastructure::CancellationToken& cancelToken,
    const SliceFn&                           onSlice)
{
    if (m_entries.empty()) {
        throw std::runtime_error("ScaledStrip::sliceAll() — strip is empty");
    }
    if (sliceHeight <= 0) {
        throw std::runtime_error(
            "ScaledStrip::sliceAll() — sliceHeight must be > 0 (got " +
            std::to_string(sliceHeight) + ")");
    }
    if (!onSlice) {
        throw std::runtime_error("ScaledStrip::sliceAll() — onSlice callback is empty");
    }

    const int numFull = m_totalHeight / sliceHeight;
    const int tail    = m_totalHeight % sliceHeight;

    // --- Full slices ---
    for (int i = 0; i < numFull; ++i) {
        if (cancelToken.isCancelled()) return;

        const int sliceStartY = i * sliceHeight;
        releaseConsumedEntries(sliceStartY);

        if (!onSlice(buildSlice(i, sliceStartY, sliceHeight))) return;
    }

    // --- Tail slice ---
    if (tail > 0 && !cancelToken.isCancelled()) {
        const int tailStartY = numFull * sliceHeight;

        switch (policy) {
            case Models::LastSlicePolicy::Crop:
                // Discard the tail — do nothing.
                break;

            case Models::LastSlicePolicy::KeepAsIs:
                releaseConsumedEntries(tailStartY);
                (void)onSlice(buildSlice(numFull, tailStartY, tail));
                break;

            case Models::LastSlicePolicy::PadWhite: {
                releaseConsumedEntries(tailStartY);
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
                (void)onSlice(std::move(tailSlice));
                break;
            }
        }
    }
}

} // namespace Platemaker::Core
