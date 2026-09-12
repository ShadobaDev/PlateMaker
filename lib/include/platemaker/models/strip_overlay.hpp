/**
 * \file lib/include/platemaker/models/strip_overlay.hpp
 * \brief StripOverlay - a placed artwork asset in the strip domain, and how its page anchor resolves.
 *
 * The other optional processing step (see \c colour_correction.hpp).  An overlay is an image asset the
 * *consumer* authored — raster (PNG) or vector (SVG) — placed either at an absolute strip-Y or relative
 * to a page's top edge; \c resolveOverlayAnchors() is what turns the second form into the first, once a
 * render knows where each page actually landed.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_MODELS_STRIP_OVERLAY_HPP
#define PLATEMAKER_MODELS_STRIP_OVERLAY_HPP

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace Platemaker::Models {

/**
 * \brief How an overlay is blended onto the slice beneath it — a curated subset of libvips' blend modes.
 *
 * \c Over is normal source-over (the default). The others are the painting modes useful for
 * bubbles/effects; the compositor maps each to the matching \c VipsBlendMode.
 */
enum class BlendMode {
    Over,     //!< Normal source-over (default).
    Multiply, //!< Darkens: result = base × overlay.
    Screen,   //!< Lightens: inverse-multiply.
    Overlay,  //!< Multiply/screen by base lightness (contrast).
    Darken,   //!< Per-channel minimum.
    Lighten   //!< Per-channel maximum.
};

/**
 * \brief One text/bubble overlay composited onto the strip at render time (strip domain).
 *
 * The overlay is a consumer-authored image asset positioned by its top-left corner in **strip
 * coordinates** (the continuous, post-scale strip the slices are cut from).  The compositor draws it
 * onto every output slice its box intersects; libvips clips a layer that straddles a slice cut, so an
 * overlay spanning two slices lands correctly on both.
 *
 * Treated as a **resource parallel to an input file**: the consumer creates the asset, but the library
 * owns the inventory — \c ProjectItem::addOverlay() mints the \c uid, computes the \c sha256, and dedups
 * identical content. Prefer that over constructing a record by hand (see \c ProjectItem).
 */
struct StripOverlay {
    std::string uid;        //!< Local unique id (e.g. "ovl-<hex>"), minted by ProjectItem::addOverlay().

    /**
     * \brief Absolute path to the artwork on disk — **raster (PNG) or vector (SVG)**.
     *
     * The loader dispatches on content, so the consumer chooses the format and the library never needs
     * to know which it got.  A vector asset is the better choice for authored text and bubbles: it
     * rasterises at whatever size \c wFrac asks for, so re-profiling a chapter to a wider target width
     * costs a re-render rather than a resample.
     *
     * The library reads this path and never copies or writes it, exactly as it treats an input page.
     */
    std::string assetPath;
    std::string sha256;     //!< SHA-256 of the asset — feeds staleness + dedup (a re-authored asset re-renders output).

    /**
     * \brief The input page this overlay rides on — empty means absolute strip coordinates.
     *
     * Keyed by \c InputFile::uid, the same stable page identity \c ColourCorrection::excludedInputUids
     * uses, so a rename does not detach a bubble from its page.  When set, \c y is measured from that
     * page's top edge in the strip rather than from the strip's; \c resolveOverlayAnchors() turns the
     * pair back into an absolute strip-Y once the layout is known.
     *
     * This is what survives editing the chapter.  A bubble stored at an absolute strip-Y drifts onto the
     * wrong artwork the moment anything above it changes height — a page inserted or reordered, a canvas
     * profile's margins edited, a page dropped as unreadable — and the drift is silent.  Anchored, the
     * bubble moves with its page and only its own page can move it.
     */
    std::string anchorInputUid;

    /**
     * \brief Top-left X, as a **fraction of the render's target width**.
     *
     * Every coordinate here is measured in one unit: the width the strip is rendered at. Pixels are
     * meaningless without the width they were measured against, and every mechanism for remembering
     * that width can be captured at the wrong moment, lost, or disagreed about by two consumers — a
     * fraction cannot, because there is nothing to remember. Re-profiling a chapter from 800 px to
     * 1600 px is a no-op on this record: \c 0.42 means the same place at either width.
     *
     * Every page is scaled to that same target width, so the strip and a page share one X origin and
     * \c xFrac means the same thing anchored or not.
     */
    double    xFrac = 0.0;

    /**
     * \brief Top-left Y, in the same unit as \c xFrac: fractions of the **target width**.
     *
     * Of the width, not the height, deliberately. One unit for both axes means the placement carries no
     * dependence on a page's aspect ratio, and it is the only rule that also works for an unanchored
     * overlay — which has no page to be a fraction of. A page several screens tall simply gives \c yFrac
     * greater than 1.
     *
     * Measured from the anchor page's top edge when \c anchorInputUid is set, else from the strip's.
     */
    double    yFrac = 0.0;

    /**
     * \brief Rendered width, in the same unit again. **0 means "the asset's own pixel size"**.
     *
     * This is what makes an overlay self-describing: the asset's natural size stops being load-bearing,
     * so re-emitting a bubble's SVG at a different internal size can no longer change how big it
     * renders. Only its aspect ratio still comes from the file, and the height follows from that — one
     * number, not two.
     *
     * \c 0 is the honest default for artwork placed before this existed, and for a consumer that simply
     * wants the asset drawn at its own size.
     */
    double    wFrac = 0.0;

    bool      enabled = true;           //!< Per-overlay toggle; a disabled overlay is not composited.
    BlendMode blend   = BlendMode::Over; //!< How it blends onto the slice beneath.
};

/**
 * \brief One overlay resolved against a known layout: absolute strip **pixels**, ready to composite.
 *
 * A separate type from \c StripOverlay on purpose. The stored form is resolution-independent and the
 * drawable form is not, and they were previously the same struct with its fields quietly reinterpreted
 * — which made "resolve twice" a thing that compiled. It no longer is.
 */
struct PlacedOverlay {
    std::string uid;        //!< Copied from the record, so a diagnostic can name the overlay.
    std::string assetPath;  //!< Copied from the record.
    int  x = 0;             //!< Absolute strip X, in this render's pixels.
    int  y = 0;             //!< Absolute strip Y, in this render's pixels.
    int  width = 0;         //!< Rendered width in pixels; **0 = the asset's own size**.
    bool enabled = true;    //!< Carried through so the compositor skips it, exactly as before.
    BlendMode blend = BlendMode::Over;
};

/**
 * \brief Turns page-anchored overlays into absolute strip coordinates for a known strip layout.
 *
 * The bridge between the two ways an overlay can be placed (see \c StripOverlay::anchorInputUid): the
 * durable, page-relative form the project stores, and the absolute strip-Y the compositor draws at.
 * Both the render and a consumer's preview call this with the layout they are about to draw, so the
 * preview cannot disagree with the render about where a bubble lands.
 *
 * Every fraction becomes pixels at \p targetWidth; an anchored overlay additionally gets its page's top
 * added to \c y.  The result is a different type (\c PlacedOverlay), so "resolve it twice" is no longer
 * something that compiles — it used to be the same struct with its fields reinterpreted, and the only
 * thing stopping a second call was a sentence in this comment.
 *
 * An overlay whose anchor page is **not in the layout** is *dropped from the result* and reported in
 * \p orphanedUids.  That is the honest reading of "the page it sat on is not being rendered": the page
 * may have been removed, or skipped this run as missing/unreadable.  Nothing is deleted — the record
 * stays in the project, so a consumer can list orphans and offer to re-anchor them, and the overlay
 * reappears by itself once its page is back.
 *
 * \param overlays          The project's overlays, in composite order.
 * \param pageTopByInputUid Strip-Y of each page's top edge, keyed by \c InputFile::uid — built from the
 *                          pages that actually landed in the strip (or, in a consumer, from the same
 *                          preview layout it is drawing).
 * \param targetWidth       The width this render (or preview) lays the strip out at. Every fraction on
 *                          the record is in this unit, so this is the only number needed to turn the
 *                          durable form into pixels — there is no authored width to remember.
 * \param orphanedUids      Optional: receives the \c uid of each overlay dropped for a missing anchor.
 * \return The overlays that can be placed, in the input order, all in absolute strip pixels.
 */
[[nodiscard]] inline std::vector<PlacedOverlay> resolveOverlayAnchors(
    const std::vector<StripOverlay>&            overlays,
    const std::unordered_map<std::string, int>& pageTopByInputUid,
    int                                         targetWidth,
    std::vector<std::string>*                   orphanedUids = nullptr)
{
    const auto px = [targetWidth](double f) {
        return static_cast<int>(std::lround(f * targetWidth));
    };

    std::vector<PlacedOverlay> resolved;
    resolved.reserve(overlays.size());

    for (const auto& o : overlays) {
        PlacedOverlay p;
        p.uid       = o.uid;
        p.assetPath = o.assetPath;
        p.enabled   = o.enabled;
        p.blend     = o.blend;
        p.x         = px(o.xFrac);
        p.width     = px(o.wFrac);   // 0 stays 0 — the asset's own size

        if (o.anchorInputUid.empty()) {   // already absolute: y is measured from the strip's top
            p.y = px(o.yFrac);
            resolved.push_back(std::move(p));
            continue;
        }

        const auto it = pageTopByInputUid.find(o.anchorInputUid);
        if (it == pageTopByInputUid.end()) {
            if (orphanedUids)
                orphanedUids->push_back(o.uid);
            continue;
        }

        // The offset scales; the page top does not — it is already in this render's own pixels.
        p.y = px(o.yFrac) + it->second;
        resolved.push_back(std::move(p));
    }

    return resolved;
}

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_STRIP_OVERLAY_HPP
