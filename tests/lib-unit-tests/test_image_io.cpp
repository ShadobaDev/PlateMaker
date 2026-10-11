/**
 * \file
 * \brief Unit tests for ImageIO save-path band/format handling.
 *
 * The strip may reach save as 4-band RGBA whenever any source carried transparency (ScaledStrip
 * promotes to the widest band layout so mixed RGB/RGBA inputs join cleanly). JPEG cannot store an
 * alpha channel, so ImageIO::encode flattens it over white for that format only — the one place
 * dropping alpha is unavoidable. PNG/WebP keep the alpha, so RGBA sources survive intact.
 *
 * Every format records the output profile's resolution — not the source's — and none carries the
 * source's EXIF (camera data, orientation), though JPEG and WebP keep EXIF to hold that resolution.
 *
 * VIPS is initialised once for the whole test binary by test_scaled_strip.cpp's global environment.
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/models/output_profile.hpp>

#include <vips/vips.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
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

/// The RGBA buffer, as a source header would leave it: 350 DPI, rotated, from a camera.
Core::PixelBuffer imageFromACamera(int w, int h)
{
    Core::PixelBuffer src = rgbaImage(w, h);
    VipsImage* out = nullptr;
    EXPECT_EQ(vips_copy(src.vipsImage(), &out, "xres", 350 / 25.4, "yres", 350 / 25.4, nullptr), 0)
        << vips_error_buffer();
    vips_image_set_int(out, VIPS_META_ORIENTATION, 6);
    vips_image_set_string(out, "exif-ifd0-Make", "TestCam (TestCam, ASCII, 8 components, 8 bytes)");
    return Core::PixelBuffer{out};
}

/// The resolution a saved slice records. libvips' WebP loader does not read the EXIF resolution into
/// xres (its JPEG loader does), so for WebP the EXIF field itself is read: "72/1 (72, Rational, ...)".
double savedDpi(const std::string& path, Models::OutputFormat fmt)
{
    VipsImage* r = vips_image_new_from_file(path.c_str(), nullptr);
    EXPECT_NE(r, nullptr) << vips_error_buffer();
    if (!r) return -1;
    double dpi = vips_image_get_xres(r) * 25.4;
    if (fmt == Models::OutputFormat::WebP) {
        const char* field = nullptr;
        dpi = -1;
        if (vips_image_get_typeof(r, "exif-ifd0-XResolution") &&
            vips_image_get_string(r, "exif-ifd0-XResolution", &field) == 0) {
            char* end = nullptr;
            const double num = std::strtod(field, &end);
            const double den = *end == '/' ? std::strtod(end + 1, nullptr) : 0.0;
            if (den > 0) dpi = num / den;
        }
    }
    g_object_unref(r);
    return dpi;
}

class ImageIoDpiTest : public ::testing::TestWithParam<Models::OutputFormat> {};

TEST_P(ImageIoDpiTest, RecordsTheProfileDpiNotTheSources)
{
    const fs::path dir = fs::temp_directory_path() / "pm-io-dpi";
    fs::remove_all(dir); fs::create_directories(dir);
    auto prof = profileFor(GetParam());
    prof.dpi  = 72.0;
    const std::string path = (dir / ("slice" + Models::outputFormatExtension(GetParam()))).string();

    ImageIO io;
    ASSERT_NO_THROW(io.encode(imageFromACamera(32, 32), path, prof));
    EXPECT_NEAR(savedDpi(path, GetParam()), 72.0, 0.05) << "the source said 350";

    VipsImage* r = vips_image_new_from_file(path.c_str(), nullptr);
    ASSERT_NE(r, nullptr) << vips_error_buffer();
    EXPECT_EQ(vips_image_get_orientation(r), 1) << "the source's orientation does not travel";
    const char* make = nullptr;
    if (vips_image_get_typeof(r, "exif-ifd0-Make"))
        vips_image_get_string(r, "exif-ifd0-Make", &make);
    EXPECT_TRUE(make == nullptr || std::string(make).find("TestCam") == std::string::npos)
        << "the source's camera does not travel: " << make;
    g_object_unref(r);

    fs::remove_all(dir);
}

INSTANTIATE_TEST_SUITE_P(EveryFormat, ImageIoDpiTest,
                         ::testing::Values(Models::OutputFormat::PNG, Models::OutputFormat::JPEG,
                                           Models::OutputFormat::WebP));

TEST(ImageIoSaveTest, NonPositiveDpiIsRefused)
{
    const fs::path dir = fs::temp_directory_path() / "pm-io-dpi-bad";
    fs::remove_all(dir); fs::create_directories(dir);
    auto prof = profileFor(Models::OutputFormat::PNG);
    prof.dpi  = 0.0;

    ImageIO io;
    EXPECT_THROW(io.encode(rgbaImage(8, 8), (dir / "slice.png").string(), prof), std::invalid_argument);
    EXPECT_FALSE(fs::exists(dir / "slice.png"));
    fs::remove_all(dir);
}

} // namespace
} // namespace Platemaker::Infrastructure
