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

#include <platemaker/infrastructure/log/log.hpp>

#include <vips/vips.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace Platemaker::Core {

namespace { namespace Log = Platemaker::Infrastructure::Log; }

namespace {

// Bytes → whole KiB. KiB rather than MiB because a test's pages are small (a 400×2560 RGB page is
// 3000 KiB, i.e. 2 MiB after integer division — too coarse to tell one resident page from two), and
// because every size in play is an exact multiple of 1 KiB, so nothing is lost.
std::string kib(std::size_t bytes) { return std::to_string(bytes / 1024); }

} // namespace

// ---------------------------------------------------------------------------
// memTrace (private)
// ---------------------------------------------------------------------------

void ScaledStrip::memTrace(const char* phase) const
{
    PLATEMAKER_LOG(Log::Memory, [&] {
        // Entries the strip still intends to keep (buffer not yet released).
        int live = 0;
        std::string names;
        for (const auto& entry : m_entries) {
            if (!entry.image.buffer.isValid()) continue;
            ++live;
            const auto& p   = entry.image.sourceFilePath;
            const auto  pos = p.find_last_of("/\\");
            names += (names.empty() ? "" : ",") + (pos == std::string::npos ? p : p.substr(pos + 1));
        }
        // vipsMem is what libvips is *actually* holding; live/entries is what the strip is holding
        // references to. They diverge when a released page is still pinned elsewhere (a slice in
        // flight, or the libvips operation cache — hence cacheOps).
        return std::string(phase)
                + " vipsMemKiB=" + kib(vips_tracked_get_mem())
                + " peakKiB="    + kib(vips_tracked_get_mem_highwater())
                + " files="   + std::to_string(vips_tracked_get_files())
                + " cacheOps=" + std::to_string(vips_cache_get_size())
                + " live="    + std::to_string(live) + "/" + std::to_string(m_entries.size())
                + " [" + names + "]";
    }());
}

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

    PLATEMAKER_LOG(Log::ScaledStrip,
            "append " + entry.image.sourceFilePath + ": "
            + std::to_string(m_width) + "x" + std::to_string(entry.height)
            + " at startY=" + std::to_string(entry.startY)
            + " -> totalHeight=" + std::to_string(m_totalHeight));

    m_entries.push_back(std::move(entry));

    // Phase-1 residency: if load → crop → scale stayed lazy, vipsMem barely moves here no matter how
    // many pages are appended. A linear climb means the pages are being decoded up front.
    memTrace("append");
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
    result.index     = index;
    result.stripTopY = sliceStartY; // the true strip-Y top — strip-domain overlays position against this

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
        if (vips_extract_area(entry.image.buffer.vipsImage(), &part,
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
        seg.sourceY           = localSrcY;
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
        // Multi-source slice: concatenate the parts vertically, edge to edge. All parts share
        // m_width (they were extracted at that width); only their heights differ.
        //
        // vips_arrayjoin was WRONG here: it lays the inputs out in a uniform grid whose cells are
        // sized to the *tallest* input (vspacing defaults to the max height), padding every shorter
        // cell with background. Unequal-height parts therefore produced an N×maxHeight slice with a
        // black band instead of a tight sum-of-heights strip (see docs/TODO.md). vips_join places two
        // images top-to-bottom with no padding when their widths already match, so fold it over the
        // parts: acc = join(acc, parts[i]).
        VipsImage* acc      = parts[0]; // borrowed from `parts`; freed in the final unref loop
        bool       accOwned = false;    // true once `acc` is an intermediate we created and must free
        for (std::size_t i = 1; i < parts.size(); ++i) {
            VipsImage* joined = nullptr;
            if (vips_join(acc, parts[i], &joined, VIPS_DIRECTION_VERTICAL, nullptr) != 0) {
                if (accOwned) g_object_unref(acc);
                for (VipsImage* p : parts) g_object_unref(p);
                throw std::runtime_error(
                    "ScaledStrip::buildSlice() — vips_join failed "
                    "(slice " + std::to_string(index) + "): " +
                    vips_error_buffer());
            }
            if (accOwned) g_object_unref(acc); // release the previous intermediate
            acc      = joined;                 // vips_join references its inputs, so they stay alive
            accOwned = true;
        }
        // Each join took its own reference to the parts it consumed; drop ours.
        for (VipsImage* p : parts) g_object_unref(p);
        result.image = PixelBuffer{acc}; // takes ownership of the final joined image
    }

    // Diagnostic: the requested slice window against what was actually built, plus every
    // source segment. When `built` height ≠ the requested (sliceEndY - sliceStartY), the join
    // padded the slice — the black-band divergence. See docs/TODO.md.
    PLATEMAKER_LOG(Log::ScaledStrip, [&] {
        std::string s = "buildSlice " + std::to_string(index)
                + " req=[" + std::to_string(sliceStartY) + "," + std::to_string(sliceEndY) + ")"
                + " built=" + std::to_string(result.image.width()) + "x"
                + std::to_string(result.image.height())
                + " parts=" + std::to_string(result.sourceMap.size());
        for (const auto& seg : result.sourceMap)
            s += "; " + seg.sourceFilePath + " srcY=" + std::to_string(seg.sourceY)
                 + " h=" + std::to_string(seg.height);
        return s;
    }());

    // Invariant: the overlapping entries tile the slice window exactly, so the built strip must be
    // exactly the requested height (single- and multi-source alike). A mismatch means a join padded
    // the slice (the old arrayjoin black-band bug) — fail loudly rather than emit a padded panel.
    if (result.image.height() != sliceH) {
        throw std::runtime_error(
            "ScaledStrip::buildSlice() — slice " + std::to_string(index) +
            " built height " + std::to_string(result.image.height()) +
            " != requested " + std::to_string(sliceH) +
            " (a join padded the slice — see docs/TODO.md)");
    }

    return result;
}

// ---------------------------------------------------------------------------
// releaseConsumedEntries (private)
// ---------------------------------------------------------------------------

void ScaledStrip::releaseConsumedEntries(int sliceStartY) noexcept
{
    // Slices advance in increasing Y, so an entry ending at or above the next slice's
    // top can never contribute again. Dropping the buffer unrefs the VipsImage, which is
    // what *allows* libvips to free the decoded source. Necessary but not sufficient: the
    // libvips operation cache still holds a reference of its own, so the memory comes back
    // on that cache's LRU schedule, bounded by its budget (100 operations / ~100 MB by
    // default) rather than immediately. Skipping the release keeps every page alive
    // instead, and peak memory then grows with the chapter — see tests/cli-tests/test_memory.py.
    // The entry stays in place; its startY/height still define the strip's geometry.
    for (auto& entry : m_entries) {
        if (entry.startY >= sliceStartY) break;  // entries are ordered — rest are below
        if (entry.startY + entry.height <= sliceStartY && entry.image.buffer.isValid()) {
            entry.image.buffer = PixelBuffer{};
            PLATEMAKER_LOG(Log::Memory,
                    "release " + entry.image.sourceFilePath
                    + " (rows " + std::to_string(entry.startY) + ".."
                    + std::to_string(entry.startY + entry.height)
                    + ") at sliceStartY=" + std::to_string(sliceStartY));
        }
    }
}

// ---------------------------------------------------------------------------
// normalizeBandCounts (private)
// ---------------------------------------------------------------------------

void ScaledStrip::normalizeBandCounts()
{
    // vips_join (buildSlice's multi-source stacker) requires all inputs to share a band count.
    // Bring every entry to one joinable, homogeneous layout — promote-only, so the user's pixels
    // are never flattened onto a background here (that happens only at save, only for JPEG).

    // Early out: if every entry already shares a band count, joins already work and there is nothing
    // to reconcile — leave the strip exactly as it is. A uniform grayscale / RGB / RGBA strip is thus
    // untouched (no colour conversion, no added alpha); only a genuine mix is normalised below.
    {
        int firstBands = -1;
        bool uniform   = true;
        for (const auto& entry : m_entries) {
            if (VipsImage* cur = entry.image.buffer.vipsImage()) {
                const int b = vips_image_get_bands(cur);
                if (firstBands < 0)        firstBands = b;
                else if (b != firstBands)  { uniform = false; break; }
            }
        }
        if (uniform) return;
    }

    // Pass 1: fold any non-RGB colourspace (grayscale, CMYK, …) into sRGB. Lossless for grayscale
    // (luma is replicated across the three bands); already-RGB/RGBA pixels are left untouched. After
    // this every entry is 3-band (RGB) or 4-band (RGBA).
    for (auto& entry : m_entries) {
        VipsImage* cur = entry.image.buffer.vipsImage();
        if (!cur) continue;
        const VipsInterpretation interp = cur->Type;
        if (interp == VIPS_INTERPRETATION_sRGB || interp == VIPS_INTERPRETATION_RGB) continue;

        VipsImage* rgb = nullptr;
        if (vips_colourspace(cur, &rgb, VIPS_INTERPRETATION_sRGB, nullptr) != 0) {
            throw std::runtime_error(
                "ScaledStrip::normalizeBandCounts() — vips_colourspace failed for '" +
                entry.image.sourceFilePath + "': " + vips_error_buffer());
        }
        entry.image.buffer = PixelBuffer{rgb}; // takes ownership; unrefs the previous image
    }

    // Pass 2: promote every entry to the widest band count present by adding a fully-opaque alpha
    // channel. If no source carries alpha, maxBands stays 3 and nothing is touched.
    int maxBands = 0;
    for (const auto& entry : m_entries) {
        if (VipsImage* cur = entry.image.buffer.vipsImage())
            maxBands = std::max(maxBands, vips_image_get_bands(cur));
    }
    for (auto& entry : m_entries) {
        VipsImage* cur = entry.image.buffer.vipsImage();
        if (!cur || vips_image_get_bands(cur) >= maxBands) continue;

        VipsImage* withAlpha = nullptr;
        if (vips_addalpha(cur, &withAlpha, nullptr) != 0) {
            throw std::runtime_error(
                "ScaledStrip::normalizeBandCounts() — vips_addalpha failed for '" +
                entry.image.sourceFilePath + "': " + vips_error_buffer());
        }
        entry.image.buffer = PixelBuffer{withAlpha}; // takes ownership; unrefs the previous image

        PLATEMAKER_LOG(Log::ScaledStrip,
                "promoted " + entry.image.sourceFilePath + " to "
                + std::to_string(maxBands) + " bands (opaque alpha added)");
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

    // Homogenise band counts before any slice is built (all buffers are still live here) so a strip
    // mixing RGB and RGBA sources joins cleanly instead of aborting mid-render.
    memTrace("pre-normalize");
    normalizeBandCounts();
    memTrace("post-normalize");

    const int numFull = m_totalHeight / sliceHeight;
    const int tail    = m_totalHeight % sliceHeight;

    PLATEMAKER_LOG(Log::ScaledStrip,
            "sliceAll totalHeight=" + std::to_string(m_totalHeight)
            + " sliceHeight=" + std::to_string(sliceHeight)
            + " numFull=" + std::to_string(numFull)
            + " tail=" + std::to_string(tail));

    // --- Full slices ---
    for (int i = 0; i < numFull; ++i) {
        if (cancelToken.isCancelled()) return;

        const int sliceStartY = i * sliceHeight;
        releaseConsumedEntries(sliceStartY);
        memTrace(("slice " + std::to_string(i) + " post-release").c_str());

        // The slice is only a lazy graph until onSlice pulls it (encode + save), so the decode of any
        // page this slice newly touches lands *inside* the callback — hence the sample after it.
        if (!onSlice(buildSlice(i, sliceStartY, sliceHeight))) return;
        memTrace(("slice " + std::to_string(i) + " post-write  ").c_str());
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
                if (vips_embed(tailSlice.image.vipsImage(), &padded,
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

    memTrace("done         ");
}

} // namespace Platemaker::Core
