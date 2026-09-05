/**
 * \file lib/include/platemaker/models/strip_overlay.hpp
 * \brief StripOverlay - a placed bitmap in the strip domain, and how its page anchor resolves.
 *
 * The other optional processing step (see \c colour_correction.hpp).  An overlay is a bitmap the
 * *consumer* rasterised, placed either at an absolute strip-Y or relative to a page's top edge;
 * \c resolveOverlayAnchors() is what turns the second form into the first, once a render knows
 * where each page actually landed.
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
 * The overlay is a consumer-rendered RGBA bitmap positioned by its top-left corner in **strip
 * coordinates** (the continuous, post-scale strip the slices are cut from).  The compositor draws it
 * onto every output slice its box intersects; libvips clips a layer that straddles a slice cut, so an
 * overlay spanning two slices lands correctly on both.
 *
 * Treated as a **resource parallel to an input file**: the consumer creates the bitmap, but the library
 * owns the inventory — \c ProjectItem::addOverlay() mints the \c uid, computes the \c sha256, and dedups
 * identical content. Prefer that over constructing a record by hand (see \c ProjectItem).
 */
struct StripOverlay {
    std::string uid;        //!< Local unique id (e.g. "ovl-<hex>"), minted by ProjectItem::addOverlay().
    std::string bitmapPath; //!< Absolute path to the pre-rendered RGBA layer on disk.
    std::string sha256;     //!< SHA-256 of the bitmap — feeds staleness + dedup (a re-rendered layer re-renders output).

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

    int       x = 0;                    //!< Top-left X (pixels). Every page is scaled to the same target
                                        //!< width, so the strip and a page share one X origin — \c x means
                                        //!< the same thing anchored or not.
    int       y = 0;                    //!< Top-left Y (pixels): from the anchor page's top when
                                        //!< \c anchorInputUid is set, else from the strip's top.
    bool      enabled = true;           //!< Per-overlay toggle; a disabled overlay is not composited.
    BlendMode blend   = BlendMode::Over; //!< How it blends onto the slice beneath.
};

/**
 * \brief Turns page-anchored overlays into absolute strip coordinates for a known strip layout.
 *
 * The bridge between the two ways an overlay can be placed (see \c StripOverlay::anchorInputUid): the
 * durable, page-relative form the project stores, and the absolute strip-Y the compositor draws at.
 * Both the render and a consumer's preview call this with the layout they are about to draw, so the
 * preview cannot disagree with the render about where a bubble lands.
 *
 * An unanchored overlay passes through untouched.  An anchored one gets its page's top added to \c y and
 * comes back unanchored — the result is uniformly absolute, so calling this twice is harmless.
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
 * \param orphanedUids      Optional: receives the \c uid of each overlay dropped for a missing anchor.
 * \return The overlays that can be placed, in the input order, all in absolute strip coordinates.
 */
[[nodiscard]] inline std::vector<StripOverlay> resolveOverlayAnchors(
    const std::vector<StripOverlay>&            overlays,
    const std::unordered_map<std::string, int>& pageTopByInputUid,
    std::vector<std::string>*                   orphanedUids = nullptr)
{
    std::vector<StripOverlay> resolved;
    resolved.reserve(overlays.size());

    for (const auto& o : overlays) {
        if (o.anchorInputUid.empty()) {   // already absolute
            resolved.push_back(o);
            continue;
        }

        const auto it = pageTopByInputUid.find(o.anchorInputUid);
        if (it == pageTopByInputUid.end()) {
            if (orphanedUids)
                orphanedUids->push_back(o.uid);
            continue;
        }

        StripOverlay abs = o;
        abs.y += it->second;
        abs.anchorInputUid.clear();
        resolved.push_back(std::move(abs));
    }

    return resolved;
}

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_STRIP_OVERLAY_HPP
