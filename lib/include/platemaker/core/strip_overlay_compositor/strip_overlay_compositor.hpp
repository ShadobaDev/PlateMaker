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
 * The library is deliberately format-agnostic: it composites an image asset supplied by the consumer,
 * **raster or vector**.  libvips dispatches on content, so a PNG loads through its own loader and an SVG
 * through librsvg, and neither this class nor its caller has to know which arrived.  What the asset
 * *depicts* — a balloon, laid-out lettering, an effect — stays entirely the consumer's concern; the lib
 * never grows a text engine, and an SVG changes nothing about that: it arrives as resolved geometry.
 *
 * Vector assets are why \c rasterizeOverlays() takes a scale.  A raster overlay authored for an 800 px
 * target is simply wrong at 1600 px and can only be resampled; a vector one re-renders sharp.  That is
 * the whole reason the scale is threaded through here rather than being the consumer's problem.
 *
 * Both entry points take overlays in **absolute strip coordinates**. A project stores them anchored to
 * a page instead (\c Models::StripOverlay::anchorInputUid), so the caller runs
 * \c Models::resolveOverlayAnchors() over the finished layout first; that keeps this class a placement
 * consumer with no opinion about how a chapter is laid out.
 *
 * Usage: call \c rasterizeOverlays() once (after the strip is built, before slicing), then \c composite()
 * per slice inside the pipeline's slice loop.  Stateless and thread-safe like the other Core steps; the
 * rasterised bitmaps live in the returned \c LoadedOverlay vector, not in the compositor.
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

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/processing_steps.hpp>

namespace Platemaker::Core {

/**
 * \brief One decoded overlay ready to composite: its RGBA bitmap plus its strip-coordinate placement.
 *
 * Produced by \c StripOverlayCompositor::rasterizeOverlays().  The bitmap is produced once and reused (by
 * reference) across every slice it intersects, so a tall overlay is not re-read — or, for a vector asset,
 * not re-rendered — per slice.
 */
struct PLATEMAKER_EXPORT LoadedOverlay {
    PixelBuffer bitmap;   //!< Rasterised RGBA layer (promoted to 4-band on load).
    int x = 0;            //!< Top-left X in strip coordinates.
    int y = 0;            //!< Top-left Y in strip coordinates.
    int w = 0;            //!< Bitmap width in pixels.
    int h = 0;            //!< Bitmap height in pixels.
    Models::BlendMode blend = Models::BlendMode::Over; //!< How it blends onto the slice.
};

/**
 * \class StripOverlayCompositor
 * \brief Rasterises overlay assets and composites the ones intersecting a slice onto it.
 */
class PLATEMAKER_EXPORT StripOverlayCompositor {
public:
    StripOverlayCompositor() = default;

    /**
     * \brief Rasterises one overlay asset — raster or vector — to an RGBA buffer.
     *
     * Exposed publicly because a consumer previewing a chapter needs *the render's own* rasteriser, not
     * an approximation of it: a GUI that draws bubbles with its own vector engine and then renders them
     * through this one has two implementations to keep in step, and an SVG filter its engine cannot draw
     * (Qt SVG, for one, implements no \c feTurbulence) would appear only in the committed output.  Going
     * through here means what the author approves on screen is what the render bakes.
     *
     * \param assetPath Absolute path to a PNG, SVG, or anything else libvips can load.
     * \param scale     Render scale; for a vector asset this is resolution, for a raster one a resample.
     * \return The RGBA buffer, or an invalid \c PixelBuffer if the asset could not be loaded.
     */
    [[nodiscard]] PixelBuffer rasterizeOverlay(const std::string& assetPath, double scale = 1.0) const;

    /**
     * \brief Rasterises the enabled overlays' assets once each, in \p overlays order.
     *
     * A disabled overlay, one with an empty path, or one that fails to load is skipped (logged,
     * non-fatal — a broken bubble must not abort a whole chapter render).  Each result is promoted to
     * RGBA so compositing has a consistent band layout.
     *
     * \param overlays The overlay definitions, already resolved to absolute strip coordinates **at this
     *                 same scale** — pass the scale to \c Models::resolveOverlayAnchors() too, or the
     *                 artwork will be the right size in the wrong place.
     * \param scale    Ratio of the render's target width to the overlays' authored width.
     * \return The successfully rasterised overlays with their placement; empty when none are usable.
     */
    [[nodiscard]] std::vector<LoadedOverlay> rasterizeOverlays(
        const std::vector<Models::StripOverlay>& overlays, double scale = 1.0) const;

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
     * \param overlays  The rasterised overlays from \c rasterizeOverlays().
     * \return The slice with intersecting overlays composited (RGBA when any was applied).
     * \throws std::runtime_error if \p slice is invalid or a libvips operation fails.
     */
    [[nodiscard]] PixelBuffer composite(
        PixelBuffer slice, int stripTopY, const std::vector<LoadedOverlay>& overlays) const;
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_STRIP_OVERLAY_COMPOSITOR_HPP
