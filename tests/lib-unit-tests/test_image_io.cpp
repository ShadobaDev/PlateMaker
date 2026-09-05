/**
 * \file
 * \brief Unit tests for ImageIO save-path band/format handling.
 *
 * The strip may reach save as 4-band RGBA whenever any source carried transparency (ScaledStrip
 * promotes to the widest band layout so mixed RGB/RGBA inputs join cleanly). JPEG cannot store an
 * alpha channel, so ImageIO::encode flattens it over white for that format only — the one place
 * dropping alpha is unavoidable. PNG/WebP keep the alpha, so RGBA sources survive intact.
 *
 * VIPS is initialised once for the whole test binary by test_scaled_strip.cpp's global environment.
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/output_profile.hpp>

#include <vips/vips.h>

#include <filesystem>
#include <string>

namespace Platemaker::Infrastructure {
namespace {

namespace fs = std::filesystem;

/// A genuine sRGB RGBA buffer (vips_image_hasalpha() is true).
Core::PixelBuffer rgbaImage(int w, int h)
{
    VipsImage* black = nullptr;
    EXPECT_EQ(vips_black(&black, w, h, "bands", 3, nullptr), 0) << vips_error_buffer();
    VipsImage* srgb = nullptr;
    EXPECT_EQ(vips_copy(black, &srgb, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr), 0)
        << vips_error_buffer();
    g_object_unref(black);
    VipsImage* rgba = nullptr;
    EXPECT_EQ(vips_addalpha(srgb, &rgba, nullptr), 0) << vips_error_buffer();
    g_object_unref(srgb);
    return Core::PixelBuffer{rgba};
}

Models::OutputProfile profileFor(Models::OutputFormat fmt)
{
    Models::OutputProfile p;
    p.outputFormat = fmt;
    p.jpegOptions  = {90, Models::JpegSubsampling::YUV_444, true, false};
    return p;
}

int savedBands(const std::string& path)
{
    VipsImage* r = vips_image_new_from_file(path.c_str(), nullptr);
    EXPECT_NE(r, nullptr) << vips_error_buffer();
    if (!r) return -1;
    const int b = vips_image_get_bands(r);
    g_object_unref(r);
    return b;
}

// JPEG output: an RGBA buffer must still save (no abort), flattened to 3-band RGB.
TEST(ImageIoSaveTest, JpegFlattensAlphaToThreeBands)
{
    const fs::path dir = fs::temp_directory_path() / "pm-io-alpha-jpg";
    fs::remove_all(dir); fs::create_directories(dir);
    const std::string path = (dir / "slice.jpg").string();

    ImageIO    io;
    const auto prof = profileFor(Models::OutputFormat::JPEG);
    ASSERT_NO_THROW(io.encode(rgbaImage(64, 64), path, prof));
    EXPECT_EQ(savedBands(path), 3) << "JPEG cannot carry alpha — output must be flattened to RGB";

    fs::remove_all(dir);
}

// PNG output: the same RGBA buffer keeps its alpha band end-to-end.
TEST(ImageIoSaveTest, PngPreservesAlpha)
{
    const fs::path dir = fs::temp_directory_path() / "pm-io-alpha-png";
    fs::remove_all(dir); fs::create_directories(dir);
    const std::string path = (dir / "slice.png").string();

    ImageIO    io;
    const auto prof = profileFor(Models::OutputFormat::PNG);
    ASSERT_NO_THROW(io.encode(rgbaImage(64, 64), path, prof));
    EXPECT_EQ(savedBands(path), 4) << "PNG must preserve the RGBA alpha channel";

    fs::remove_all(dir);
}

} // namespace
} // namespace Platemaker::Infrastructure
