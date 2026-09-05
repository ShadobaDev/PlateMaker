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
    EXPECT_EQ(vips_getpoint(buf.vipsImage(), &v, &n, 0, 0, nullptr), 0)
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
    const auto out = cc.applyToBuffer(makeSolid({128, 64, 200}), neutral());
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
    const auto p = pixel(cc.applyToBuffer(makeSolid({128, 128, 128}), g));
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
    EXPECT_NEAR(pixel(cc.applyToBuffer(makeSolid({128, 128, 128}), g))[0], 128.0, 1.0);
    // A bright value is pushed past the ceiling and clips at 255 (not wrapped).
    EXPECT_DOUBLE_EQ(pixel(cc.applyToBuffer(makeSolid({220, 220, 220}), g))[0], 255.0);
    // A dark value is pushed below the floor and clips at 0.
    EXPECT_DOUBLE_EQ(pixel(cc.applyToBuffer(makeSolid({40, 40, 40}), g))[0], 0.0);
}

TEST(ColourCorrectorTest, SaturationZeroProducesLuminanceGrey)
{
    Models::ColourCorrection g = neutral();
    g.saturation = 0.0;

    ColourCorrector cc;
    const auto p = pixel(cc.applyToBuffer(makeSolid({255, 0, 0}), g)); // pure red
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
    const auto p = pixel(cc.applyToBuffer(makeSolid({255, 0, 0, 200}), g)); // RGBA, opaque-ish red
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
    const auto p = pixel(cc.applyToBuffer(makeSolid({100, 150, 200}), g));
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
    const auto p = pixel(cc.applyToBuffer(makeSolid({128, 128, 128}), g)); // t≈0.5 → ≈0.8 → ≈204
    ASSERT_EQ(p.size(), 3u);
    EXPECT_NEAR(p[0], 204.0, 3.0);
    EXPECT_NEAR(p[1], 204.0, 3.0);
    EXPECT_NEAR(p[2], 204.0, 3.0);
}

TEST(ColourCorrectorTest, PerChannelCurveOnlyAffectsItsChannel)
{
    Models::ColourCorrection g = neutral();
    g.curves.red = {{0.0, 0.0}, {1.0, 0.5}}; // halve red; green/blue identity

    ColourCorrector cc;
    const auto p = pixel(cc.applyToBuffer(makeSolid({128, 128, 128}), g));
    ASSERT_EQ(p.size(), 3u);
    EXPECT_NEAR(p[0], 64.0,  2.0); // red halved
    EXPECT_NEAR(p[1], 128.0, 1.0); // green untouched
    EXPECT_NEAR(p[2], 128.0, 1.0); // blue untouched
}

TEST(ColourCorrectorTest, ApplyToRgbaGradesInPlaceLikeApply)
{
    // A 2x2 solid RGBA red (alpha 200). Fully desaturating must match apply(): luminance grey, alpha kept.
    std::vector<std::uint8_t> px(static_cast<std::size_t>(2) * 2 * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = 255; px[i + 1] = 0; px[i + 2] = 0; px[i + 3] = 200;
    }

    Models::ColourCorrection g = neutral();
    g.saturation = 0.0;

    ColourCorrector cc;
    cc.applyToRgba(px.data(), 2, 2, g);

    const double lum = 0.2126 * 255.0; // ≈ 54
    for (std::size_t i = 0; i < px.size(); i += 4) {
        EXPECT_NEAR(px[i + 0], lum, 1.5);
        EXPECT_NEAR(px[i + 1], lum, 1.5);
        EXPECT_NEAR(px[i + 2], lum, 1.5);
        EXPECT_EQ(px[i + 3], 200);     // alpha untouched
    }
}

TEST(ColourCorrectorTest, ApplyToRgbaNeutralLeavesBytesUntouched)
{
    std::vector<std::uint8_t> px = {10, 20, 30, 40,   50, 60, 70, 80,
                                    90, 100, 110, 120, 130, 140, 150, 160};
    const auto before = px;

    ColourCorrector cc;
    cc.applyToRgba(px.data(), 2, 2, neutral());
    EXPECT_EQ(px, before);
}

TEST(ColourCorrectorTest, ApplyToRgbaRejectsBadArgs)
{
    std::vector<std::uint8_t> px(2 * 2 * 4, 128);
    ColourCorrector cc;
    Models::ColourCorrection g = neutral();
    g.brightness = 0.1; // non-neutral so it actually tries to grade
    EXPECT_THROW(cc.applyToRgba(nullptr, 2, 2, g), std::runtime_error);
    EXPECT_THROW(cc.applyToRgba(px.data(), 0, 2, g), std::runtime_error);
}

} // namespace Platemaker::Core
