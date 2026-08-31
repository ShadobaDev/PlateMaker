/**
 * \file lib/include/platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp
 * \brief StripOverlayCompositor — composites text/bubble RGBA overlays onto output slices at render.
 *
 * This is the strip-domain half of the optional overlay step (see \c Models::StripOverlay).  Overlays
 * are positioned in **strip coordinates** (the continuous, post-scale strip the slices are cut from),
 * so one overlay can straddle a slice cut: the compositor draws each overlay onto every output slice
 * whose vertical span it intersects, offset into that slice's local space.  libvips clips a layer that
 * extends past a slice edge, so a straddling overlay lands correctly on both adjacent slices.
 *
 * The library is deliberately format-agnostic: it composites a pre-rendered RGBA bitmap supplied by the
 * consumer.  Whether that bitmap came from raster art, an SVG, or laid-out rich text is the consumer's
 * concern — the lib never grows a text engine.
 *
 * Usage: call \c load() once (after the strip is built, before slicing) to decode the overlay bitmaps,
 * then \c apply() per slice inside the pipeline's slice loop.  Stateless and thread-safe like the other
 * Core steps; the decoded bitmaps live in the returned \c LoadedOverlay vector, not in the compositor.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-31
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_CORE_STRIP_OVERLAY_COMPOSITOR_HPP
#define PLATEMAKER_CORE_STRIP_OVERLAY_COMPOSITOR_HPP

#include <vector>

#include "platemaker/platemaker_export.h"
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/processing_steps.hpp>

namespace Platemaker::Core {

/**
 * \brief One decoded overlay ready to composite: its RGBA bitmap plus its strip-coordinate placement.
 *
 * Produced by \c StripOverlayCompositor::load().  The bitmap is decoded once and reused (by reference)
 * across every slice it intersects, so a tall overlay is not re-read per slice.
 */
struct PLATEMAKER_EXPORT LoadedOverlay {
    PixelBuffer bitmap;   //!< Decoded RGBA layer (promoted to 4-band on load).
    int x = 0;            //!< Top-left X in strip coordinates.
    int y = 0;            //!< Top-left Y in strip coordinates.
    int w = 0;            //!< Bitmap width in pixels.
    int h = 0;            //!< Bitmap height in pixels.
    Models::BlendMode blend = Models::BlendMode::Over; //!< How it blends onto the slice.
};

/**
 * \class StripOverlayCompositor
 * \brief Decodes overlay bitmaps and composites the ones intersecting a slice onto it.
 */
class PLATEMAKER_EXPORT StripOverlayCompositor {
public:
    StripOverlayCompositor() = default;

    /**
     * \brief Decodes the enabled overlays' bitmaps once, in \p overlays order.
     *
     * A disabled overlay, one with an empty path, or one whose bitmap fails to decode is skipped
     * (logged, non-fatal — a broken bubble must not abort a whole chapter render).  Each decoded
     * bitmap is promoted to RGBA so compositing has a consistent band layout.
     *
     * \param overlays The project's overlay definitions.
     * \return The successfully decoded overlays with their placement; empty when none are usable.
     */
    [[nodiscard]] std::vector<LoadedOverlay> load(const std::vector<Models::StripOverlay>& overlays) const;

    /**
     * \brief Composites every loaded overlay intersecting this slice onto \p slice.
     *
     * For each overlay whose strip-Y span \c [y, y+h) overlaps \c [stripTopY, stripTopY + sliceHeight),
     * the bitmap is embedded into a slice-sized transparent layer at \c (x, y - stripTopY) — which clips
     * any part outside the slice — and blended over the slice with source-over alpha.  A slice that no
     * overlay intersects is returned unchanged (same pixels and band count), so slices away from any
     * overlay stay byte-identical to an overlay-free render.
     *
     * \param slice     The output slice pixels (ownership transferred in).  Must be valid.
     * \param stripTopY The slice's top Y in strip coordinates (\c SliceResult::stripTopY).
     * \param overlays  The decoded overlays from \c load().
     * \return The slice with intersecting overlays composited (RGBA when any was applied).
     * \throws std::runtime_error if \p slice is invalid or a libvips operation fails.
     */
    [[nodiscard]] PixelBuffer apply(
        PixelBuffer slice, int stripTopY, const std::vector<LoadedOverlay>& overlays) const;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_STRIP_OVERLAY_COMPOSITOR_HPP
