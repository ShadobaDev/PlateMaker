/**
 * \file
 * \brief Unit tests for StripOverlayCompositor — placement, alpha, and straddle-across-cut clipping.
 *
 * Builds synthetic slices and in-memory overlays (bypassing load(), which needs a file). libvips is
 * initialised once for the whole test executable by the VipsEnvironment in test_scaled_strip.cpp.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-31
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>

#include <vips/vips.h>

#include <cstdint>
#include <vector>

namespace Platemaker::Core {

namespace {

/// A solid-colour uchar image of the given size and per-band values (3 = RGB, 4 = RGBA).
PixelBuffer makeSolid(int w, int h, const std::vector<std::uint8_t>& colour)
{
    const int bands = static_cast<int>(colour.size());
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * bands);
    for (std::size_t i = 0; i < px.size(); i += bands)
        for (int b = 0; b < bands; ++b)
            px[i + static_cast<std::size_t>(b)] = colour[static_cast<std::size_t>(b)];

    VipsImage* raw = vips_image_new_from_memory_copy(px.data(), px.size(), w, h, bands, VIPS_FORMAT_UCHAR);
    EXPECT_NE(raw, nullptr) << vips_error_buffer();
    // Realistic interpretation so vips_image_hasalpha classifies a 4th band as alpha.
    const VipsInterpretation interp = bands >= 3 ? VIPS_INTERPRETATION_sRGB : VIPS_INTERPRETATION_B_W;
    VipsImage* typed = nullptr;
    EXPECT_EQ(vips_copy(raw, &typed, "interpretation", interp, nullptr), 0) << vips_error_buffer();
    g_object_unref(raw);
    return PixelBuffer{typed};
}

std::vector<double> pixel(const PixelBuffer& b, int x, int y)
{
    double* v = nullptr;
    int     n = 0;
    EXPECT_EQ(vips_getpoint(b.get(), &v, &n, x, y, nullptr), 0) << vips_error_buffer();
    std::vector<double> out(v, v + n);
    g_free(v);
    return out;
}

/// A single opaque-red RGBA overlay of width \p w, height \p h, placed at strip (0, \p y).
std::vector<LoadedOverlay> redOverlay(int w, int y, int h)
{
    std::vector<LoadedOverlay> ovs;
    LoadedOverlay o;
    o.bitmap = makeSolid(w, h, {255, 0, 0, 255});
    o.x = 0; o.y = y; o.w = w; o.h = h;
    ovs.push_back(std::move(o));
    return ovs;
}

constexpr int W = 10;

} // namespace

TEST(StripOverlayCompositorTest, NonIntersectingSliceIsUntouched)
{
    StripOverlayCompositor comp;
    auto ovs = redOverlay(W, /*y*/ 1000, /*h*/ 40);      // far below this slice
    auto out = comp.apply(makeSolid(W, 100, {255, 255, 255}), /*stripTopY*/ 0, ovs);

    const auto p = pixel(out, 5, 50);
    ASSERT_EQ(p.size(), 3u);                             // unchanged → still RGB, no alpha added
    EXPECT_DOUBLE_EQ(p[0], 255.0);
    EXPECT_DOUBLE_EQ(p[1], 255.0);
    EXPECT_DOUBLE_EQ(p[2], 255.0);
}

TEST(StripOverlayCompositorTest, OpaqueOverlayReplacesWithinItsBox)
{
    StripOverlayCompositor comp;
    auto ovs = redOverlay(W, /*y*/ 20, /*h*/ 40);        // covers rows 20..59
    auto out = comp.apply(makeSolid(W, 100, {255, 255, 255}), /*stripTopY*/ 0, ovs);

    const auto inside  = pixel(out, 5, 40);              // under the overlay
    const auto outside = pixel(out, 5, 5);               // above the overlay
    ASSERT_EQ(inside.size(), 4u);                        // composited → RGBA
    EXPECT_NEAR(inside[0], 255.0, 1.0);
    EXPECT_NEAR(inside[1], 0.0,   1.0);
    EXPECT_NEAR(inside[2], 0.0,   1.0);
    EXPECT_NEAR(outside[0], 255.0, 1.0);
    EXPECT_NEAR(outside[1], 255.0, 1.0);
    EXPECT_NEAR(outside[2], 255.0, 1.0);
}

TEST(StripOverlayCompositorTest, OverlayStraddlingACutLandsOnBothSlices)
{
    StripOverlayCompositor comp;
    // Overlay spans strip-Y 80..119 — it crosses the cut at 100 between two 100px slices.
    auto ovs = redOverlay(W, /*y*/ 80, /*h*/ 40);

    // Slice 0 (strip 0..99): the overlay's top half shows in rows 80..99.
    auto s0 = comp.apply(makeSolid(W, 100, {255, 255, 255}), /*stripTopY*/ 0, ovs);
    EXPECT_NEAR(pixel(s0, 5, 90)[0], 255.0, 1.0); // red present
    EXPECT_NEAR(pixel(s0, 5, 90)[1], 0.0,   1.0);
    EXPECT_NEAR(pixel(s0, 5, 50)[1], 255.0, 1.0); // above the overlay → still white (green≈255)

    // Slice 1 (strip 100..199): the overlay's bottom half shows in rows 0..19 (clipped from the top).
    auto s1 = comp.apply(makeSolid(W, 100, {255, 255, 255}), /*stripTopY*/ 100, ovs);
    EXPECT_NEAR(pixel(s1, 5, 10)[0], 255.0, 1.0); // red present
    EXPECT_NEAR(pixel(s1, 5, 10)[1], 0.0,   1.0);
    EXPECT_NEAR(pixel(s1, 5, 50)[1], 255.0, 1.0); // below the overlay → still white
}

TEST(StripOverlayCompositorTest, BlendModeChangesTheResult)
{
    StripOverlayCompositor comp;
    // Opaque mid-grey overlay covering the whole slice, over a mid-grey base.
    const auto make = [](Models::BlendMode b) {
        std::vector<LoadedOverlay> ovs;
        LoadedOverlay o;
        o.bitmap = makeSolid(W, 100, {128, 128, 128, 255});
        o.x = 0; o.y = 0; o.w = W; o.h = 100; o.blend = b;
        ovs.push_back(std::move(o));
        return ovs;
    };

    auto over = make(Models::BlendMode::Over);
    auto mult = make(Models::BlendMode::Multiply);
    const auto pOver = pixel(comp.apply(makeSolid(W, 100, {128, 128, 128}), 0, over), 5, 50);
    const auto pMult = pixel(comp.apply(makeSolid(W, 100, {128, 128, 128}), 0, mult), 5, 50);

    EXPECT_NEAR(pOver[0], 128.0, 1.0);            // opaque source-over → the overlay grey
    EXPECT_NEAR(pMult[0], 128.0 * 128.0 / 255.0, 2.0); // multiply darkens (≈64)
    EXPECT_LT(pMult[0], pOver[0] - 40.0);         // clearly different from Over
}

} // namespace Platemaker::Core
