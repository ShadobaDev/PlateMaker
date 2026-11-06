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
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <gtest/gtest.h>

#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>

#include <vips/vips.h>

#include <chrono>
#include <cstdint>
#include <utility>
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

/// A right triangle filling the lower-left half of a \p w × \p h box — one long diagonal edge, which
/// is what makes "re-rendered" distinguishable from "upscaled". Square unless \p h says otherwise.
std::string triangleSvg(int w, int h = -1)
{
    if (h < 0)
        h = w;
    const std::string sw = std::to_string(w);
    const std::string sh = std::to_string(h);
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + sw + "\" height=\"" + sh +
           "\" viewBox=\"0 0 " + sw + " " + sh + "\"><polygon points=\"0,0 0," + sh + " " + sw +
           "," + sh + "\" fill=\"#000000\"/></svg>";
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

TEST(StripOverlayCompositorTest, RgbaFormGivesTheSamePixelsAsTheBuffer)
{
    // Deliberately NOT square: a transposed width/height or a mis-stepped row survives a square asset
    // unnoticed, and those are the two ways reading a raw buffer usually goes wrong.
    const TempSvg svg(triangleSvg(20, 50), "rgba");
    StripOverlayCompositor comp;

    const OverlayRaster r = comp.rasterizeOverlayRgba(svg.path(), 2.0);
    ASSERT_TRUE(r.isValid()) << vips_error_buffer();
    EXPECT_EQ(r.width, 40);
    EXPECT_EQ(r.height, 100);
    EXPECT_EQ(r.rgba.size(), static_cast<std::size_t>(40 * 100 * 4))
        << "four straight-alpha bytes per pixel";
    // Valid takes both sides and the bytes.
    for (const OverlayRaster& broken : {OverlayRaster{0, r.height, r.rgba}, OverlayRaster{r.width, 0, r.rgba},
                                        OverlayRaster{r.width, r.height, {}}})
        EXPECT_FALSE(broken.isValid());

    // The consumer form must agree with the compositing form pixel for pixel, or a preview drawn from
    // one would not be what the render bakes from the other.
    const PixelBuffer buf = comp.rasterizeOverlay(svg.path(), 2.0);
    ASSERT_TRUE(buf.isValid());
    for (const auto& at : {std::pair<int, int>{4, 90}, {34, 6}, {20, 50}}) {
        const auto  expected = pixel(buf, at.first, at.second);
        const auto* px = r.rgba.data() + (static_cast<std::size_t>(at.second) * r.width + at.first) * 4;
        for (int b = 0; b < 4; ++b)
            EXPECT_NEAR(px[b], expected[static_cast<std::size_t>(b)], 0.5)
                << "band " << b << " at (" << at.first << "," << at.second << ")";
    }
}

TEST(StripOverlayCompositorTest, RgbaFromBytesMatchesRgbaFromTheSameFile)
{
    // The preview path. It must agree with the file path exactly, or what an author approves on screen
    // is not what the render reads back off disk.
    const std::string doc = triangleSvg(20, 50);
    const TempSvg     svg(doc, "bytes");
    StripOverlayCompositor comp;

    const OverlayRaster fromFile  = comp.rasterizeOverlayRgba(svg.path(), 2.0);
    const OverlayRaster fromBytes = comp.rasterizeSvgRgba(doc, 2.0);
    ASSERT_TRUE(fromFile.isValid()) << vips_error_buffer();
    ASSERT_TRUE(fromBytes.isValid()) << vips_error_buffer();

    EXPECT_EQ(fromBytes.width, fromFile.width);
    EXPECT_EQ(fromBytes.height, fromFile.height);
    EXPECT_EQ(fromBytes.rgba, fromFile.rgba) << "the same document must rasterise identically either way";
}

TEST(StripOverlayCompositorTest, RgbaFromBytesRejectsWhatIsNotSvg)
{
    StripOverlayCompositor comp;
    EXPECT_FALSE(comp.rasterizeSvgRgba("").isValid());
    EXPECT_FALSE(comp.rasterizeSvgRgba("this is not markup at all").isValid());
    vips_error_clear();
}

TEST(StripOverlayCompositorTest, RgbaFormIsEmptyForAnUnloadableAsset)
{
    StripOverlayCompositor comp;
    EXPECT_FALSE(comp.rasterizeOverlayRgba("no-such-file-anywhere.svg").isValid());
    EXPECT_FALSE(comp.rasterizeOverlayRgba("").isValid());
    vips_error_clear();
}

// ---------------------------------------------------------------------------
// composite — guards and the RGBA-slice path
// ---------------------------------------------------------------------------

TEST(StripOverlayCompositorTest, CompositeRejectsInvalidSlice)
{
    StripOverlayCompositor comp;
    auto ovs = redOverlay(W, /*y*/ 0, /*h*/ 40);
    EXPECT_THROW((void)comp.composite(PixelBuffer{}, /*stripTopY*/ 0, ovs), std::runtime_error);
}

TEST(StripOverlayCompositorTest, CompositeWithNoOverlaysReturnsTheSliceUnchanged)
{
    StripOverlayCompositor comp;
    auto out = comp.composite(makeSolid(W, 100, {12, 34, 56}), /*stripTopY*/ 0, {});
    ASSERT_TRUE(out.isValid());
    const auto p = pixel(out, 5, 50);
    ASSERT_EQ(p.size(), 3u) << "an empty overlay list must leave the slice byte-identical (still RGB)";
    EXPECT_DOUBLE_EQ(p[0], 12.0);
    EXPECT_DOUBLE_EQ(p[1], 34.0);
    EXPECT_DOUBLE_EQ(p[2], 56.0);
}

TEST(StripOverlayCompositorTest, CompositeOntoAlreadyAlphaSlice)
{
    // An RGBA slice takes the "already has alpha" branch (no addalpha); the overlay must still land.
    StripOverlayCompositor comp;
    auto ovs = redOverlay(W, /*y*/ 20, /*h*/ 40);
    auto out = comp.composite(makeSolid(W, 100, {255, 255, 255, 255}), /*stripTopY*/ 0, ovs);

    ASSERT_TRUE(out.isValid());
    const auto inside = pixel(out, 5, 40);
    ASSERT_EQ(inside.size(), 4u);
    EXPECT_NEAR(inside[0], 255.0, 1.0);
    EXPECT_NEAR(inside[1], 0.0,   1.0);
    EXPECT_NEAR(inside[2], 0.0,   1.0);
}

TEST(StripOverlayCompositorTest, EveryBlendModeTakesItsOwnPath)
{
    // One opaque grey overlay (100) over a brighter grey base (200), covering the whole slice, graded
    // through each blend. The directional outcome of each confirms toVipsBlend mapped it to the right
    // libvips mode (strip_overlay_compositor.cpp:49-55), not merely that compositing ran.
    StripOverlayCompositor comp;
    const auto run = [&](Models::BlendMode b) {
        std::vector<LoadedOverlay> ovs;
        LoadedOverlay o;
        o.bitmap = makeSolid(W, 100, {100, 100, 100, 255});
        o.x = 0; o.y = 0; o.w = W; o.h = 100; o.blend = b;
        ovs.push_back(std::move(o));
        return pixel(comp.composite(makeSolid(W, 100, {200, 200, 200}), 0, ovs), 5, 50)[0];
    };

    EXPECT_GT(run(Models::BlendMode::Screen),  210.0) << "screen brightens past both inputs";
    EXPECT_LT(run(Models::BlendMode::Darken),  110.0) << "darken keeps the darker (overlay=100)";
    EXPECT_GT(run(Models::BlendMode::Lighten), 190.0) << "lighten keeps the lighter (base=200)";
    const double overlay = run(Models::BlendMode::Overlay);
    EXPECT_GT(overlay, 100.0);
    EXPECT_LT(overlay, 222.0) << "overlay lands between a pure multiply and a pure screen";
}

// ---------------------------------------------------------------------------
// rasterizeOverlay — raster (PNG) load paths
// ---------------------------------------------------------------------------

namespace {

/// Writes \p buf to a uniquely-named temp PNG; removes it when the handle leaves scope.
class TempPng {
public:
    explicit TempPng(const PixelBuffer& buf, const char* stem)
        : m_path(std::filesystem::temp_directory_path() /
                 ("pm-ovl-" + std::string(stem) + "-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png"))
    {
        EXPECT_EQ(vips_image_write_to_file(buf.vipsImage(), m_path.string().c_str(), nullptr), 0)
            << vips_error_buffer();
    }
    ~TempPng() { std::error_code ec; std::filesystem::remove(m_path, ec); }
    TempPng(const TempPng&)            = delete;
    TempPng& operator=(const TempPng&) = delete;
    [[nodiscard]] std::string path() const { return m_path.string(); }
private:
    std::filesystem::path m_path;
};

} // namespace

TEST(StripOverlayCompositorTest, OpaqueRasterOverlayGainsAnAlphaBand)
{
    // A 3-band PNG carries no alpha, so rasterizeOverlay must promote it with vips_addalpha
    // (strip_overlay_compositor.cpp:99-106); the compositor contract is a 4-band layer.
    const TempPng png(makeSolid(16, 16, {10, 20, 30}), "opaque"); // 3-band → no alpha on disk
    StripOverlayCompositor comp;

    const PixelBuffer buf = comp.rasterizeOverlay(png.path(), 1.0);
    ASSERT_TRUE(buf.isValid()) << vips_error_buffer();
    EXPECT_EQ(buf.vipsImage()->Bands, 4) << "an opaque overlay must be promoted to RGBA";
}

TEST(StripOverlayCompositorTest, SixteenBitOverlayIsFlattenedToEightBitRgba)
{
    // A 16-bit asset must come back as 8-bit RGBA bytes, its values scaled rather than clipped: 16-bit
    // 257 × v is 8-bit v, where a cast would clip every value above 255 to white. pngsave writes 16 bits
    // only for an RGB16 image, so the pixels are scaled up, cast, and tagged as one.
    VipsImage* wide  = nullptr;
    VipsImage* u16   = nullptr;
    VipsImage* rgb16 = nullptr;
    ASSERT_EQ(vips_linear1(makeSolid(16, 24, {1, 2, 3, 255}).vipsImage(), &wide, 257.0, 0.0, nullptr), 0)
        << vips_error_buffer();
    ASSERT_EQ(vips_cast(wide, &u16, VIPS_FORMAT_USHORT, nullptr), 0) << vips_error_buffer();
    ASSERT_EQ(vips_copy(u16, &rgb16, "interpretation", VIPS_INTERPRETATION_RGB16, nullptr), 0)
        << vips_error_buffer();
    g_object_unref(wide);
    g_object_unref(u16);
    const TempPng png(PixelBuffer{rgb16}, "u16");
    {
        VipsImage* onDisk = vips_image_new_from_file(png.path().c_str(), nullptr);
        ASSERT_NE(onDisk, nullptr) << vips_error_buffer();
        EXPECT_EQ(onDisk->BandFmt, VIPS_FORMAT_USHORT) << "the asset on disk must really be 16-bit";
        g_object_unref(onDisk);
    }

    StripOverlayCompositor comp;
    const OverlayRaster r = comp.rasterizeOverlayRgba(png.path(), 1.0);
    ASSERT_TRUE(r.isValid()) << vips_error_buffer();
    EXPECT_EQ(r.width, 16);
    EXPECT_EQ(r.height, 24);
    ASSERT_EQ(r.rgba.size(), static_cast<std::size_t>(16 * 24 * 4)) << "exactly four bytes per pixel";
    EXPECT_EQ(std::vector<unsigned char>(r.rgba.begin(), r.rgba.begin() + 4),
              (std::vector<unsigned char>{1, 2, 3, 255}))
        << "scaled from 16 bits, not clipped";
}

// ---------------------------------------------------------------------------
// rasterizeOverlayAtWidth — the target-width probe
// ---------------------------------------------------------------------------

TEST(StripOverlayCompositorTest, RasterizeAtWidthRendersToTheRequestedWidth)
{
    const TempSvg svg(triangleSvg(20), "atwidth");
    StripOverlayCompositor comp;

    const PixelBuffer buf = comp.rasterizeOverlayAtWidth(svg.path(), /*width=*/40);
    ASSERT_TRUE(buf.isValid()) << vips_error_buffer();
    EXPECT_EQ(buf.vipsImage()->Xsize, 40) << "natural 20 probed, rendered at ratio 2.0";
    EXPECT_EQ(buf.vipsImage()->Ysize, 40);
}

TEST(StripOverlayCompositorTest, RasterizeAtWidthIsEmptyWhenTheProbeFails)
{
    StripOverlayCompositor comp;
    EXPECT_FALSE(comp.rasterizeOverlayAtWidth("no-such-file-anywhere.svg", /*width=*/40).isValid());
    vips_error_clear();
}

TEST(StripOverlayCompositorTest, OnlyOverlaysCoveringTheSliceAreDrawn)
{
    // Two overlays, one on this slice and one far below it: the second is skipped, the first drawn.
    StripOverlayCompositor comp;
    auto ovs  = redOverlay(W, /*y*/ 20, /*h*/ 20);
    auto away = redOverlay(W, /*y*/ 5000, /*h*/ 20);
    ovs.push_back(std::move(away.front()));

    auto out = comp.composite(makeSolid(W, 100, {255, 255, 255}), /*stripTopY*/ 0, ovs);
    EXPECT_NEAR(pixel(out, 5, 30)[1], 0.0,   1.0) << "the covering overlay is drawn";
    EXPECT_NEAR(pixel(out, 5, 80)[1], 255.0, 1.0) << "nothing else changed";
}

TEST(StripOverlayCompositorTest, SliceThatCannotTakeAnAlphaBandThrows)
{
    // A LABQ-coded slice has no alpha and libvips cannot add one to it: composite must report the
    // failure as a runtime_error rather than draw onto a half-converted image.
    VipsImage* lab  = nullptr;
    VipsImage* labq = nullptr;
    ASSERT_EQ(vips_colourspace(makeSolid(W, 100, {200, 200, 200}).vipsImage(), &lab,
                               VIPS_INTERPRETATION_LAB, nullptr), 0) << vips_error_buffer();
    ASSERT_EQ(vips_Lab2LabQ(lab, &labq, nullptr), 0) << vips_error_buffer();
    g_object_unref(lab);

    StripOverlayCompositor comp;
    auto ovs = redOverlay(W, /*y*/ 0, /*h*/ 40);
    EXPECT_THROW((void)comp.composite(PixelBuffer{labq}, 0, ovs), std::runtime_error);
}

TEST(StripOverlayCompositorTest, SliceThatCannotBeBlendedThrows)
{
    // A complex-valued, untagged (multiband) slice takes an alpha band but has no colour route for
    // the blend, so vips_composite2 refuses it.
    VipsImage* black = nullptr;
    VipsImage* cx    = nullptr;
    ASSERT_EQ(vips_black(&black, W, 100, "bands", 3, nullptr), 0) << vips_error_buffer();
    ASSERT_EQ(vips_cast(black, &cx, VIPS_FORMAT_COMPLEX, nullptr), 0) << vips_error_buffer();
    g_object_unref(black);

    StripOverlayCompositor comp;
    auto ovs = redOverlay(W, /*y*/ 0, /*h*/ 40);
    EXPECT_THROW((void)comp.composite(PixelBuffer{cx}, 0, ovs), std::runtime_error);
}

TEST(StripOverlayCompositorTest, AssetWithAPngSignatureButNoImageIsEmpty)
{
    // The loader is picked from the first bytes, so a file that starts like a PNG gets as far as the
    // load itself — which must fail softly (invalid buffer), not throw out of a chapter render.
    const auto path = std::filesystem::temp_directory_path() /
        ("pm-ovl-truncated-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    {
        std::ofstream f(path, std::ios::binary);
        f << "\x89PNG\r\n\x1a\n" << "this is not an IHDR chunk";
    }
    StripOverlayCompositor comp;
    EXPECT_FALSE(comp.rasterizeOverlay(path.string(), 1.0).isValid());
    vips_error_clear();
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(StripOverlayCompositorTest, RasterScaleBeyondTheResamplerLimitIsEmpty)
{
    // Size limit: libvips resizes by at most 1e7. A raster asked for more must come back empty, not as
    // an attempt to allocate a billions-of-pixels layer.
    const TempPng png(makeSolid(2, 2, {1, 2, 3, 255}), "hugescale");
    StripOverlayCompositor comp;
    EXPECT_FALSE(comp.rasterizeOverlay(png.path(), 2.0e7).isValid());
    vips_error_clear();
}

TEST(StripOverlayCompositorTest, RasterOfAnotherColourCodingBecomesRgba)
{
    // A libvips-native (.v) asset can hold a LABQ-coded image. It is not sRGB, so it is converted, and
    // comes back as RGBA like any other asset.
    VipsImage* lab  = nullptr;
    VipsImage* labq = nullptr;
    ASSERT_EQ(vips_colourspace(makeSolid(4, 4, {200, 200, 200}).vipsImage(), &lab,
                               VIPS_INTERPRETATION_LAB, nullptr), 0) << vips_error_buffer();
    ASSERT_EQ(vips_Lab2LabQ(lab, &labq, nullptr), 0) << vips_error_buffer();
    g_object_unref(lab);
    const auto path = std::filesystem::temp_directory_path() /
        ("pm-ovl-labq-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".v");
    ASSERT_EQ(vips_image_write_to_file(labq, path.string().c_str(), nullptr), 0) << vips_error_buffer();
    g_object_unref(labq);

    StripOverlayCompositor comp;
    const OverlayRaster r = comp.rasterizeOverlayRgba(path.string(), 1.0);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    ASSERT_TRUE(r.isValid()) << vips_error_buffer();
    ASSERT_EQ(r.rgba.size(), static_cast<std::size_t>(4 * 4 * 4));
    // LABQ packs Lab into 32 bits, so the grey comes back near, not at, 200 (197 measured on 8.18).
    EXPECT_NEAR(r.rgba[0], 200, 4);
    EXPECT_NEAR(r.rgba[1], 200, 4);
    EXPECT_NEAR(r.rgba[2], 200, 4);
    EXPECT_EQ(r.rgba[3], 255) << "opaque: the asset had no alpha";

    // Generic RGB is colour already: it is taken as it is.
    VipsImage* rgb = nullptr;
    ASSERT_EQ(vips_copy(makeSolid(4, 4, {10, 20, 30}).vipsImage(), &rgb,
                        "interpretation", VIPS_INTERPRETATION_RGB, nullptr), 0) << vips_error_buffer();
    ASSERT_EQ(vips_image_write_to_file(rgb, path.string().c_str(), nullptr), 0) << vips_error_buffer();
    g_object_unref(rgb);
    const OverlayRaster asIs = comp.rasterizeOverlayRgba(path.string(), 1.0);
    std::filesystem::remove(path, ec);
    ASSERT_TRUE(asIs.isValid()) << vips_error_buffer();
    EXPECT_EQ(asIs.rgba[0], 10);
    EXPECT_EQ(asIs.rgba[1], 20);
    EXPECT_EQ(asIs.rgba[2], 30);
}

TEST(StripOverlayCompositorTest, GreyAlphaOverlayIsPromotedToRgba)
{
    // LoadedOverlay documents the layer as "promoted to 4-band on load", and rasterizeOverlayRgba as
    // RGBA bytes. A grey+alpha PNG (2 bands) already has alpha: it must still become sRGB, or it stays
    // 2-band and the RGBA form fails its own size check and comes back empty.
    const TempPng png(makeSolid(6, 4, {90, 200}), "greyalpha");   // 2-band B_W + alpha
    StripOverlayCompositor comp;
    const PixelBuffer buf = comp.rasterizeOverlay(png.path(), 1.0);
    ASSERT_TRUE(buf.isValid());
    EXPECT_EQ(buf.vipsImage()->Bands, 4);
    const OverlayRaster r = comp.rasterizeOverlayRgba(png.path(), 1.0);
    EXPECT_TRUE(r.isValid());
    EXPECT_EQ(r.rgba.size(), static_cast<std::size_t>(6 * 4 * 4));
}

TEST(StripOverlayCompositorTest, RasterizeOverlaysSkipsUnusableEntriesAndKeepsOrder)
{
    const TempSvg svg(triangleSvg(20), "batch");

    // Already-resolved placements: rasterizeOverlays() takes pixels, never fractions, so the size and
    // the position can no longer come from two different scales.
    std::vector<Models::PlacedOverlay> defs(5);
    defs[0].uid = "ok-1";     defs[0].assetPath = svg.path();
    defs[1].uid = "disabled"; defs[1].assetPath = svg.path(); defs[1].enabled = false;
    defs[2].uid = "missing";  defs[2].assetPath = "no-such-file-anywhere.svg";
    defs[3].uid = "ok-2";     defs[3].assetPath = svg.path();  defs[3].x = 7; defs[3].y = 9;
    defs[4].uid = "no-asset";

    StripOverlayCompositor comp;
    const auto loaded = comp.rasterizeOverlays(defs);

    // A broken bubble must not abort a chapter render, and the survivors keep their input order.
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].x, 0);
    EXPECT_EQ(loaded[1].x, 7);
    EXPECT_EQ(loaded[1].y, 9);
    EXPECT_EQ(loaded[1].w, 20);
    EXPECT_EQ(loaded[1].h, 20);
}

} // namespace Platemaker::Core
