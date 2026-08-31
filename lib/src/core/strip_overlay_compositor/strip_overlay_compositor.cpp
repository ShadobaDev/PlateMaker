/**
 * \file lib/src/core/strip_overlay_compositor/strip_overlay_compositor.cpp
 * \brief StripOverlayCompositor implementation — decode overlays, composite per slice via libvips.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-31
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>

#include <platemaker/infrastructure/log/log.hpp>

#include <vips/vips.h>

#include <stdexcept>
#include <string>

namespace { namespace Log = Platemaker::Infrastructure::Log; }

namespace Platemaker::Core {

namespace {

//! Unref \p img if non-null and throw — leak-free error paths without a web of gotos.
[[noreturn]] void failVips(VipsImage* img, const std::string& what)
{
    const std::string err = vips_error_buffer();
    vips_error_clear();
    if (img) g_object_unref(img);
    throw std::runtime_error("StripOverlayCompositor::apply — " + what + ": " + err);
}

//! True when the overlay's strip-Y span [y, y+h) overlaps the slice's [top, top+height).
bool intersectsSlice(const LoadedOverlay& ov, int sliceTop, int sliceH)
{
    return ov.y < sliceTop + sliceH && ov.y + ov.h > sliceTop;
}

//! Maps the model's curated blend set to the matching libvips blend mode.
VipsBlendMode toVipsBlend(Models::BlendMode b)
{
    switch (b) {
        case Models::BlendMode::Multiply: return VIPS_BLEND_MODE_MULTIPLY;
        case Models::BlendMode::Screen:   return VIPS_BLEND_MODE_SCREEN;
        case Models::BlendMode::Overlay:  return VIPS_BLEND_MODE_OVERLAY;
        case Models::BlendMode::Darken:   return VIPS_BLEND_MODE_DARKEN;
        case Models::BlendMode::Lighten:  return VIPS_BLEND_MODE_LIGHTEN;
        case Models::BlendMode::Over:
        default:                          return VIPS_BLEND_MODE_OVER;
    }
}

} // namespace

std::vector<LoadedOverlay> StripOverlayCompositor::load(
    const std::vector<Models::StripOverlay>& overlays) const
{
    std::vector<LoadedOverlay> out;
    out.reserve(overlays.size());

    for (const auto& ov : overlays) {
        if (!ov.enabled || ov.bitmapPath.empty())
            continue;

        VipsImage* img = vips_image_new_from_file(
            ov.bitmapPath.c_str(), "access", VIPS_ACCESS_RANDOM, nullptr);
        if (!img) {
            PLATEMAKER_LOG(Log::StripOverlayCompositor,
                    "skip overlay '" + ov.uid + "': cannot load '" + ov.bitmapPath + "': "
                    + vips_error_buffer());
            vips_error_clear();
            continue;
        }

        // Promote to RGBA so compositing has a consistent 4-band layout (an opaque bubble gains a
        // fully-opaque alpha; an RGBA bubble is untouched).
        VipsImage* rgba = img;
        if (!vips_image_hasalpha(img)) {
            VipsImage* wa = nullptr;
            if (vips_addalpha(img, &wa, nullptr) != 0) {
                PLATEMAKER_LOG(Log::StripOverlayCompositor,
                        "skip overlay '" + ov.uid + "': vips_addalpha failed: " + vips_error_buffer());
                vips_error_clear();
                g_object_unref(img);
                continue;
            }
            g_object_unref(img);
            rgba = wa;
        }

        LoadedOverlay lo;
        lo.bitmap = PixelBuffer{rgba};
        lo.x = ov.x;
        lo.y = ov.y;
        lo.w = rgba->Xsize;
        lo.h = rgba->Ysize;
        lo.blend = ov.blend;
        PLATEMAKER_LOG(Log::StripOverlayCompositor,
                "loaded overlay '" + ov.uid + "' " + std::to_string(lo.w) + "x" + std::to_string(lo.h)
                + " @ (" + std::to_string(lo.x) + "," + std::to_string(lo.y) + ")");
        out.push_back(std::move(lo));
    }
    return out;
}

PixelBuffer StripOverlayCompositor::apply(
    PixelBuffer slice, int stripTopY, const std::vector<LoadedOverlay>& overlays) const
{
    if (!slice.isValid())
        throw std::runtime_error("StripOverlayCompositor::apply — slice is invalid (null VipsImage)");
    if (overlays.empty())
        return slice;

    VipsImage* const base   = slice.get();
    const int        sliceW = base->Xsize;
    const int        sliceH = base->Ysize;

    // Untouched (byte-identical) unless an overlay actually covers this slice.
    bool any = false;
    for (const auto& ov : overlays)
        if (intersectsSlice(ov, stripTopY, sliceH)) { any = true; break; }
    if (!any)
        return slice;

    // Promote the slice to RGBA so it can blend with the RGBA overlay layers.
    VipsImage* work = nullptr;
    if (vips_image_hasalpha(base)) {
        work = base;
        g_object_ref(work);
    } else if (vips_addalpha(base, &work, nullptr) != 0) {
        failVips(nullptr, "add alpha to slice");
    }

    for (const auto& ov : overlays) {
        if (!intersectsSlice(ov, stripTopY, sliceH))
            continue;

        // Embed the overlay into a slice-sized transparent layer at its slice-local position. Any part
        // outside the slice is clipped, so an overlay straddling a slice cut lands on both slices.
        const int ex = ov.x;
        const int ey = ov.y - stripTopY;
        double bg[4] = {0.0, 0.0, 0.0, 0.0};
        VipsArrayDouble* bgArr = vips_array_double_new(bg, 4);
        VipsImage* layer = nullptr;
        const int erc = vips_embed(ov.bitmap.get(), &layer, ex, ey, sliceW, sliceH,
                                   "extend", VIPS_EXTEND_BACKGROUND, "background", bgArr, nullptr);
        vips_area_unref(VIPS_AREA(bgArr));
        if (erc != 0)
            failVips(work, "embed overlay layer");

        // Blend the layer onto the slice; transparent regions of the layer leave the slice untouched.
        VipsImage* comp = nullptr;
        const int crc = vips_composite2(work, layer, &comp, toVipsBlend(ov.blend), nullptr);
        g_object_unref(layer);
        if (crc != 0)
            failVips(work, "composite overlay");
        g_object_unref(work);
        work = comp;
    }

    return PixelBuffer{work};
}

} // namespace Platemaker::Core
