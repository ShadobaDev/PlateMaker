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
#include <platemaker/infrastructure/image_io/output_locked_error.hpp>
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

/// A genuine sRGB RGBA buffer (vips_image_hasalpha() is true), half transparent: an encoder may drop an
/// alpha band that is opaque throughout (libwebp does), and this one must not be droppable.
Core::PixelBuffer rgbaImage(int w, int h)
{
    VipsImage* black = nullptr;
    EXPECT_EQ(vips_black(&black, w, h, "bands", 3, nullptr), 0) << vips_error_buffer();
    VipsImage* srgb = nullptr;
    EXPECT_EQ(vips_copy(black, &srgb, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr), 0)
        << vips_error_buffer();
    g_object_unref(black);
    VipsImage* rgba = nullptr;
    EXPECT_EQ(vips_bandjoin_const1(srgb, &rgba, 128.0, nullptr), 0) << vips_error_buffer();
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
TEST(ImageIoSaveTest, PngAndWebpPreserveAlpha)
{
    const fs::path dir = fs::temp_directory_path() / "pm-io-alpha-png";
    fs::remove_all(dir); fs::create_directories(dir);

    ImageIO io;
    auto    webp = profileFor(Models::OutputFormat::WebP);
    webp.webpOptions.lossless = true;
    for (const auto& [prof, name] : {std::pair{profileFor(Models::OutputFormat::PNG), "slice.png"},
                                     std::pair{webp, "slice.webp"}}) {
        const std::string path = (dir / name).string();
        ASSERT_NO_THROW(io.encode(rgbaImage(64, 64), path, prof));
        EXPECT_EQ(savedBands(path), 4) << name << " must preserve the RGBA alpha channel";
    }

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

// ---------------------------------------------------------------------------
// decode / encode — inputs that cannot be read or written
// ---------------------------------------------------------------------------

/// A fresh, empty scratch directory removed when the test leaves scope.
class ScratchDir {
public:
    explicit ScratchDir(const char* tag) : m_dir(fs::temp_directory_path() / tag)
    {
        fs::remove_all(m_dir);
        fs::create_directories(m_dir);
    }
    ~ScratchDir() { std::error_code ec; fs::remove_all(m_dir, ec); }
    ScratchDir(const ScratchDir&)            = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    [[nodiscard]] fs::path operator/(const char* name) const { return m_dir / name; }
private:
    fs::path m_dir;
};

TEST(ImageIoDecodeTest, MissingFileThrows)
{
    const ScratchDir dir("pm-io-decode-missing");
    EXPECT_THROW((void)ImageIO{}.decode((dir / "absent.png").string()), std::runtime_error);
    vips_error_clear();
}

TEST(ImageIoSaveTest, EmptyBufferIsRefused)
{
    const ScratchDir dir("pm-io-empty");
    EXPECT_THROW(ImageIO{}.encode(Core::PixelBuffer{}, (dir / "slice.png").string(),
                                  profileFor(Models::OutputFormat::PNG)),
                 std::runtime_error);
    EXPECT_FALSE(fs::exists(dir / "slice.png"));
}

/// The chroma subsampling the JPEG at \p path was written with, as libjpeg recorded it.
std::string savedSubsampling(const std::string& path)
{
    VipsImage* r = vips_image_new_from_file(path.c_str(), nullptr);
    EXPECT_NE(r, nullptr) << vips_error_buffer();
    if (!r) return {};
    const char* s = nullptr;
    EXPECT_EQ(vips_image_get_string(r, "jpeg-chroma-subsample", &s), 0) << vips_error_buffer();
    const std::string out = s ? s : "";
    g_object_unref(r);
    return out;
}

TEST(ImageIoSaveTest, JpegSubsamplingOptionReachesTheEncoder)
{
    // Each JpegSubsampling value maps to a libjpeg mode. 4:4:4 and 4:2:0 are forced; YUV_422 is the
    // documented best effort — libjpeg's own choice, which below quality 90 is 4:2:0.
    const ScratchDir dir("pm-io-subsample");
    const auto save = [&](Models::JpegSubsampling s, const char* name) {
        auto prof = profileFor(Models::OutputFormat::JPEG);
        prof.jpegOptions.quality     = 50;
        prof.jpegOptions.subsampling = s;
        const std::string path = (dir / name).string();
        ImageIO{}.encode(rgbaImage(32, 32), path, prof);
        return savedSubsampling(path);
    };
    EXPECT_EQ(save(Models::JpegSubsampling::YUV_444, "444.jpg"), "4:4:4");
    EXPECT_EQ(save(Models::JpegSubsampling::YUV_420, "420.jpg"), "4:2:0");
    EXPECT_EQ(save(Models::JpegSubsampling::YUV_422, "422.jpg"), "4:2:0");
}

TEST(ImageIoSaveTest, InterlaceAndProgressiveOptionsReachTheFile)
{
    // The PNG interlace flag and the JPEG progressive flag are written, not just accepted.
    const ScratchDir dir("pm-io-interlace");
    const auto interlaced = [](const std::string& path) {
        VipsImage* r = vips_image_new_from_file(path.c_str(), nullptr);
        EXPECT_NE(r, nullptr) << vips_error_buffer();
        int v = 0;
        if (r && vips_image_get_typeof(r, "interlaced")) {
            EXPECT_EQ(vips_image_get_int(r, "interlaced", &v), 0);
        }
        if (r) g_object_unref(r);
        return v;
    };

    auto png = profileFor(Models::OutputFormat::PNG);
    png.pngOptions.interlaced = true;
    const std::string pngPath = (dir / "i.png").string();
    ImageIO{}.encode(rgbaImage(16, 16), pngPath, png);
    EXPECT_EQ(interlaced(pngPath), 1);

    auto jpg = profileFor(Models::OutputFormat::JPEG);
    jpg.jpegOptions.progressive = true;
    jpg.jpegOptions.optimize    = false;
    const std::string jpgPath = (dir / "p.jpg").string();
    ImageIO{}.encode(rgbaImage(16, 16), jpgPath, jpg);
    EXPECT_EQ(interlaced(jpgPath), 1);
}

TEST(ImageIoSaveTest, JpegFlattenFailureLeavesNothingBehind)
{
    // A complex-valued RGBA buffer has alpha but cannot be flattened (libvips refuses complex input):
    // the JPEG path must throw and leave neither the slice nor its temporary sibling on disk.
    VipsImage* rgba = nullptr;
    ASSERT_EQ(vips_cast(rgbaImage(8, 8).vipsImage(), &rgba, VIPS_FORMAT_COMPLEX, nullptr), 0)
        << vips_error_buffer();
    const Core::PixelBuffer complexRgba{rgba};
    ASSERT_TRUE(vips_image_hasalpha(complexRgba.vipsImage()));

    const ScratchDir  dir("pm-io-flatten-fail");
    const std::string path = (dir / "slice.jpg").string();
    EXPECT_THROW(ImageIO{}.encode(complexRgba, path, profileFor(Models::OutputFormat::JPEG)),
                 std::runtime_error);
    EXPECT_FALSE(fs::exists(path));
    EXPECT_FALSE(fs::exists(path + ".pmtmp"));
    vips_error_clear();
}

TEST(ImageIoSaveTest, EncoderFailureRemovesTheTemporaryFile)
{
    // The encoder writes to "<out>.pmtmp" first. Occupy that name with a directory so the save fails:
    // encode() must throw, publish nothing, and clean up the temporary name.
    const ScratchDir  dir("pm-io-write-fail");
    const std::string path = (dir / "slice.png").string();
    fs::create_directory(path + ".pmtmp");

    EXPECT_THROW(ImageIO{}.encode(rgbaImage(8, 8), path, profileFor(Models::OutputFormat::PNG)),
                 std::runtime_error);
    EXPECT_FALSE(fs::exists(path));
    EXPECT_FALSE(fs::exists(path + ".pmtmp")) << "a failed save must not leave its temp behind";
    vips_error_clear();
}

TEST(ImageIoSaveTest, DestinationThatCannotBeReplacedIsReportedAsLocked)
{
    // The rename over the destination is the step a locked file breaks. A non-empty directory in the
    // destination's place makes it fail portably: the error is the typed OutputLockedError the
    // pipeline maps to ProcessingErrorCode::OutputLocked, and the encoded temp is removed.
    const ScratchDir  dir("pm-io-locked");
    const std::string path = (dir / "slice.png").string();
    fs::create_directories(fs::path(path) / "occupied");

    EXPECT_THROW(ImageIO{}.encode(rgbaImage(8, 8), path, profileFor(Models::OutputFormat::PNG)),
                 OutputLockedError);
    EXPECT_FALSE(fs::exists(path + ".pmtmp"));
}

} // namespace
} // namespace Platemaker::Infrastructure
