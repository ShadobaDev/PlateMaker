/**
 * \file
 * \brief Unit tests for ColourCorrector — brightness / contrast / saturation point grade.
 *
 * Builds tiny synthetic solid-colour images, grades them, and reads pixels back. libvips is
 * initialised once for the whole test executable by the VipsEnvironment in test_scaled_strip.cpp.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-30
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/core/colour_corrector/colour_corrector.hpp>
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/processing_steps.hpp>

#include <vips/vips.h>

#include <cstdint>
#include <vector>

namespace Platemaker::Core {

namespace {

/// A 2x2 solid-colour uchar image with the given per-band values (3 = RGB, 4 = RGBA).
PixelBuffer makeSolid(const std::vector<std::uint8_t>& colour)
{
    const int w = 2, h = 2;
    const int bands = static_cast<int>(colour.size());
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * bands);
    for (std::size_t i = 0; i < px.size(); i += bands)
        for (int b = 0; b < bands; ++b)
            px[i + static_cast<std::size_t>(b)] = colour[static_cast<std::size_t>(b)];

    VipsImage* raw = vips_image_new_from_memory_copy(
        px.data(), px.size(), w, h, bands, VIPS_FORMAT_UCHAR);
    EXPECT_NE(raw, nullptr) << "vips_image_new_from_memory_copy failed: " << vips_error_buffer();

    // Stamp a realistic interpretation (as a decoded PNG/JPEG would carry) so vips_image_hasalpha
    // classifies a 4th band as alpha — the pipeline never sees a bare MULTIBAND image.
    const VipsInterpretation interp =
        bands >= 3 ? VIPS_INTERPRETATION_sRGB : VIPS_INTERPRETATION_B_W;
    VipsImage* typed = nullptr;
    EXPECT_EQ(vips_copy(raw, &typed, "interpretation", interp, nullptr), 0)
        << "vips_copy failed: " << vips_error_buffer();
    g_object_unref(raw);
    return PixelBuffer{typed};
}

/// Reads pixel (0,0) as a vector of band values.
std::vector<double> pixel(const PixelBuffer& buf)
{
    double* v = nullptr;
    int     n = 0;
    EXPECT_EQ(vips_getpoint(buf.get(), &v, &n, 0, 0, nullptr), 0)
        << "vips_getpoint failed: " << vips_error_buffer();
    std::vector<double> out(v, v + n);
    g_free(v);
    return out;
}

Models::ColourCorrection neutral()
{
    Models::ColourCorrection cc;
    cc.enabled = true; // apply() itself does not check enabled; the pipeline gates it
    return cc;         // brightness 0, contrast 1, saturation 1
}

} // namespace

TEST(ColourCorrectorTest, NeutralGradeLeavesPixelsUnchanged)
{
    ColourCorrector cc;
    const auto out = cc.apply(makeSolid({128, 64, 200}), neutral());
    const auto p   = pixel(out);
    ASSERT_EQ(p.size(), 3u);
    EXPECT_DOUBLE_EQ(p[0], 128.0);
    EXPECT_DOUBLE_EQ(p[1], 64.0);
    EXPECT_DOUBLE_EQ(p[2], 200.0);
}

TEST(ColourCorrectorTest, BrightnessLiftsUniformly)
{
    Models::ColourCorrection g = neutral();
    g.brightness = 0.2; // +0.2 * 255 ≈ +51

    ColourCorrector cc;
    const auto p = pixel(cc.apply(makeSolid({128, 128, 128}), g));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_NEAR(p[0], 179.0, 1.0);
    EXPECT_NEAR(p[1], 179.0, 1.0);
    EXPECT_NEAR(p[2], 179.0, 1.0);
}

TEST(ColourCorrectorTest, ContrastPivotsAroundMidGreyAndClips)
{
    Models::ColourCorrection g = neutral();
    g.contrast = 2.0;

    ColourCorrector cc;
    // Mid-grey is the pivot → stays ~mid.
    EXPECT_NEAR(pixel(cc.apply(makeSolid({128, 128, 128}), g))[0], 128.0, 1.0);
    // A bright value is pushed past the ceiling and clips at 255 (not wrapped).
    EXPECT_DOUBLE_EQ(pixel(cc.apply(makeSolid({220, 220, 220}), g))[0], 255.0);
    // A dark value is pushed below the floor and clips at 0.
    EXPECT_DOUBLE_EQ(pixel(cc.apply(makeSolid({40, 40, 40}), g))[0], 0.0);
}

TEST(ColourCorrectorTest, SaturationZeroProducesLuminanceGrey)
{
    Models::ColourCorrection g = neutral();
    g.saturation = 0.0;

    ColourCorrector cc;
    const auto p = pixel(cc.apply(makeSolid({255, 0, 0}), g)); // pure red
    ASSERT_EQ(p.size(), 3u);
    const double lum = 0.2126 * 255.0; // ≈ 54.2
    EXPECT_NEAR(p[0], lum, 1.0);
    EXPECT_NEAR(p[1], lum, 1.0);
    EXPECT_NEAR(p[2], lum, 1.0);
    EXPECT_NEAR(p[0], p[1], 1.0); // fully desaturated → all channels equal
    EXPECT_NEAR(p[1], p[2], 1.0);
}

TEST(ColourCorrectorTest, AlphaIsPreservedThroughGrade)
{
    Models::ColourCorrection g = neutral();
    g.saturation = 0.0;

    ColourCorrector cc;
    const auto p = pixel(cc.apply(makeSolid({255, 0, 0, 200}), g)); // RGBA, opaque-ish red
    ASSERT_EQ(p.size(), 4u);
    const double lum = 0.2126 * 255.0;
    EXPECT_NEAR(p[0], lum, 1.0);
    EXPECT_NEAR(p[1], lum, 1.0);
    EXPECT_NEAR(p[2], lum, 1.0);
    EXPECT_DOUBLE_EQ(p[3], 200.0); // alpha untouched by the grade
}

TEST(ColourCorrectorTest, IdentityCurveIsNoOp)
{
    Models::ColourCorrection g = neutral();
    g.curves.master = {{0.0, 0.0}, {1.0, 1.0}}; // identity line

    ColourCorrector cc;
    const auto p = pixel(cc.apply(makeSolid({100, 150, 200}), g));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_DOUBLE_EQ(p[0], 100.0);
    EXPECT_DOUBLE_EQ(p[1], 150.0);
    EXPECT_DOUBLE_EQ(p[2], 200.0);
}

TEST(ColourCorrectorTest, MasterCurveMapsAllChannels)
{
    Models::ColourCorrection g = neutral();
    g.curves.master = {{0.0, 0.0}, {0.5, 0.8}, {1.0, 1.0}}; // lift mid-tones

    ColourCorrector cc;
    const auto p = pixel(cc.apply(makeSolid({128, 128, 128}), g)); // t≈0.5 → ≈0.8 → ≈204
    ASSERT_EQ(p.size(), 3u);
    EXPECT_NEAR(p[0], 204.0, 3.0);
    EXPECT_NEAR(p[1], 204.0, 3.0);
    EXPECT_NEAR(p[2], 204.0, 3.0);
}

TEST(ColourCorrectorTest, PerChannelCurveOnlyAffectsItsChannel)
{
    Models::ColourCorrection g = neutral();
    g.curves.r = {{0.0, 0.0}, {1.0, 0.5}}; // halve red; green/blue identity

    ColourCorrector cc;
    const auto p = pixel(cc.apply(makeSolid({128, 128, 128}), g));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_NEAR(p[0], 64.0,  2.0); // red halved
    EXPECT_NEAR(p[1], 128.0, 1.0); // green untouched
    EXPECT_NEAR(p[2], 128.0, 1.0); // blue untouched
}

} // namespace Platemaker::Core
