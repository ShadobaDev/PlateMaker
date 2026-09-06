/**
 * \file
 * \brief Unit tests for StripOverlayCompositor — placement, alpha, and straddle-across-cut clipping.
 *
 * Builds synthetic slices and in-memory overlays for the compositing cases; the rasterising cases at
 * the end go through real files, since loading is the whole thing they are about. libvips is
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

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
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
    EXPECT_EQ(vips_getpoint(b.vipsImage(), &v, &n, x, y, nullptr), 0) << vips_error_buffer();
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
    auto out = comp.composite(makeSolid(W, 100, {255, 255, 255}), /*stripTopY*/ 0, ovs);

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
    auto out = comp.composite(makeSolid(W, 100, {255, 255, 255}), /*stripTopY*/ 0, ovs);

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
    auto s0 = comp.composite(makeSolid(W, 100, {255, 255, 255}), /*stripTopY*/ 0, ovs);
    EXPECT_NEAR(pixel(s0, 5, 90)[0], 255.0, 1.0); // red present
    EXPECT_NEAR(pixel(s0, 5, 90)[1], 0.0,   1.0);
    EXPECT_NEAR(pixel(s0, 5, 50)[1], 255.0, 1.0); // above the overlay → still white (green≈255)

    // Slice 1 (strip 100..199): the overlay's bottom half shows in rows 0..19 (clipped from the top).
    auto s1 = comp.composite(makeSolid(W, 100, {255, 255, 255}), /*stripTopY*/ 100, ovs);
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
    const auto pOver = pixel(comp.composite(makeSolid(W, 100, {128, 128, 128}), 0, over), 5, 50);
    const auto pMult = pixel(comp.composite(makeSolid(W, 100, {128, 128, 128}), 0, mult), 5, 50);

    EXPECT_NEAR(pOver[0], 128.0, 1.0);            // opaque source-over → the overlay grey
    EXPECT_NEAR(pMult[0], 128.0 * 128.0 / 255.0, 2.0); // multiply darkens (≈64)
    EXPECT_LT(pMult[0], pOver[0] - 40.0);         // clearly different from Over
}

// ---------------------------------------------------------------------------
// Vector assets
//
// The overlay pipeline takes whatever libvips can load, and an SVG is the case the whole
// vector-bubble design rests on: it is the only asset that can answer a re-profiled chapter with a
// genuinely re-rendered layer instead of a resampled one. These pin that difference rather than just
// pinning "an SVG loads".
// ---------------------------------------------------------------------------

namespace {

/// Writes \p svg to a uniquely-named temp file and removes it when the test leaves scope.
class TempSvg {
public:
    explicit TempSvg(const std::string& svg, const char* stem)
        : m_path(std::filesystem::temp_directory_path() /
                 ("pm-ovl-" + std::string(stem) + "-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".svg"))
    {
        std::ofstream f(m_path, std::ios::binary);
        f << svg;
    }
    ~TempSvg() { std::error_code ec; std::filesystem::remove(m_path, ec); }

    TempSvg(const TempSvg&)            = delete;
    TempSvg& operator=(const TempSvg&) = delete;

    [[nodiscard]] std::string path() const { return m_path.string(); }

private:
    std::filesystem::path m_path;
};

/// A right triangle filling the lower-left half of a \p side × \p side box — one long diagonal edge,
/// which is what makes "re-rendered" distinguishable from "upscaled".
std::string triangleSvg(int side)
{
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(side) +
           "\" height=\"" + std::to_string(side) + "\" viewBox=\"0 0 " + std::to_string(side) +
           " " + std::to_string(side) + "\"><polygon points=\"0,0 0," + std::to_string(side) + " " +
           std::to_string(side) + "," + std::to_string(side) + "\" fill=\"#000000\"/></svg>";
}

/// Number of pixels along scanline \p y whose alpha is neither fully on nor fully off — the width of
/// the antialiased edge, which is small for a real render and wide for an upscale.
int partialAlphaOnRow(const PixelBuffer& b, int y, int width)
{
    int n = 0;
    for (int x = 0; x < width; ++x) {
        const double a = pixel(b, x, y).back();
        if (a > 2.0 && a < 253.0)
            ++n;
    }
    return n;
}

} // namespace

TEST(StripOverlayCompositorTest, SvgOverlayLoadsAsRgbaAtItsDeclaredSize)
{
    const TempSvg svg(triangleSvg(20), "size");
    StripOverlayCompositor comp;

    const PixelBuffer buf = comp.rasterizeOverlay(svg.path(), 1.0);
    ASSERT_TRUE(buf.isValid()) << "svgload is not available in this libvips build: " << vips_error_buffer();
    EXPECT_EQ(buf.vipsImage()->Xsize, 20);
    EXPECT_EQ(buf.vipsImage()->Ysize, 20);
    EXPECT_EQ(buf.vipsImage()->Bands, 4) << "the compositor needs a 4-band layer";

    EXPECT_NEAR(pixel(buf, 2, 17).back(), 255.0, 1.0) << "inside the triangle is opaque";
    EXPECT_NEAR(pixel(buf, 17, 2).back(), 0.0,   1.0) << "outside it is fully transparent";
}

TEST(StripOverlayCompositorTest, SvgOverlayReRendersAtScaleRatherThanResampling)
{
    const TempSvg svg(triangleSvg(20), "scale");
    StripOverlayCompositor comp;

    const PixelBuffer big = comp.rasterizeOverlay(svg.path(), 8.0);
    ASSERT_TRUE(big.isValid()) << vips_error_buffer();
    EXPECT_EQ(big.vipsImage()->Xsize, 160);
    EXPECT_EQ(big.vipsImage()->Ysize, 160);

    // The point of vectors. An 8x upscale of the 20x20 render would smear that render's own
    // antialiasing across ~8 pixels; re-rendering the geometry at 160x160 leaves the edge as narrow as
    // it was, in the new pixels. Measured on a mid-height scanline, well away from the corners.
    EXPECT_LE(partialAlphaOnRow(big, 80, 160), 4)
        << "the diagonal is smeared — the asset looks resampled, not re-rendered";
}

TEST(StripOverlayCompositorTest, ScaleOneIsTheUntouchedPath)
{
    const TempSvg svg(triangleSvg(20), "identity");
    StripOverlayCompositor comp;

    // The default and the explicit 1.0 must agree — every existing project renders through here.
    const PixelBuffer a = comp.rasterizeOverlay(svg.path());
    const PixelBuffer b = comp.rasterizeOverlay(svg.path(), 1.0);
    ASSERT_TRUE(a.isValid());
    ASSERT_TRUE(b.isValid());
    EXPECT_EQ(a.vipsImage()->Xsize, b.vipsImage()->Xsize);
    EXPECT_EQ(a.vipsImage()->Ysize, b.vipsImage()->Ysize);
}

TEST(StripOverlayCompositorTest, RasterOverlayStillLoadsAndScales)
{
    // A raster asset must keep working, including under a scale — the loader takes no "scale" option,
    // so it is resized after loading instead. Getting this wrong breaks every existing PNG overlay,
    // silently, because a failed load is only logged.
    const auto tmp = std::filesystem::temp_directory_path() /
        ("pm-ovl-raster-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    {
        const PixelBuffer src = makeSolid(20, 20, {255, 0, 0, 255});
        ASSERT_EQ(vips_image_write_to_file(src.vipsImage(), tmp.string().c_str(), nullptr), 0)
            << vips_error_buffer();
    }

    StripOverlayCompositor comp;
    const PixelBuffer one = comp.rasterizeOverlay(tmp.string(), 1.0);
    ASSERT_TRUE(one.isValid()) << vips_error_buffer();
    EXPECT_EQ(one.vipsImage()->Xsize, 20);

    const PixelBuffer two = comp.rasterizeOverlay(tmp.string(), 2.0);
    ASSERT_TRUE(two.isValid()) << vips_error_buffer();
    EXPECT_EQ(two.vipsImage()->Xsize, 40);
    EXPECT_EQ(two.vipsImage()->Ysize, 40);

    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST(StripOverlayCompositorTest, RasterizeOverlaysSkipsUnusableEntriesAndKeepsOrder)
{
    const TempSvg svg(triangleSvg(20), "batch");

    std::vector<Models::StripOverlay> defs(4);
    defs[0].uid = "ok-1";     defs[0].assetPath = svg.path();
    defs[1].uid = "disabled"; defs[1].assetPath = svg.path(); defs[1].enabled = false;
    defs[2].uid = "missing";  defs[2].assetPath = "no-such-file-anywhere.svg";
    defs[3].uid = "ok-2";     defs[3].assetPath = svg.path();  defs[3].x = 7; defs[3].y = 9;

    StripOverlayCompositor comp;
    const auto loaded = comp.rasterizeOverlays(defs, 1.0);

    // A broken bubble must not abort a chapter render, and the survivors keep their input order.
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].x, 0);
    EXPECT_EQ(loaded[1].x, 7);
    EXPECT_EQ(loaded[1].y, 9);
    EXPECT_EQ(loaded[1].w, 20);
    EXPECT_EQ(loaded[1].h, 20);
}

} // namespace Platemaker::Core
