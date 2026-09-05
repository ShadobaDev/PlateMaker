/**
 * \file
 * \brief Unit tests for PixelBuffer and the Scaler / MarginCropper components.
 *
 * These tests cover the low-level pixel-buffer abstraction (RAII, move
 * semantics, accessor correctness) and the pure-crop MarginCropper.
 * libvips is initialised once for the whole test binary by test_scaled_strip.cpp's
 * global environment, so the move-semantics and Scaler cases synthesise images in
 * RAM (vips_black) rather than needing on-disk fixtures.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-02
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/core/margin_cropper/margin_cropper.hpp>
#include <platemaker/core/scaler/scaler.hpp>
#include <platemaker/models/common_types.hpp>

#include <vips/vips.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace Platemaker::Core {

// ---------------------------------------------------------------------------
// PixelBuffer — default construction
// ---------------------------------------------------------------------------

TEST(PixelBufferTest, DefaultConstructorCreatesInvalidBuffer)
{
    PixelBuffer buf;
    EXPECT_FALSE(buf.isValid());
    EXPECT_EQ(buf.vipsImage(),    nullptr);
    EXPECT_EQ(buf.width(),  0);
    EXPECT_EQ(buf.height(), 0);
}

// ---------------------------------------------------------------------------
// PixelBuffer — move semantics
// ---------------------------------------------------------------------------

TEST(PixelBufferTest, MoveConstructorTransfersOwnership)
{
    // Wrap a real VipsImage (synthesised in RAM) and move-construct from it: the destination must own the
    // very same image (transfer, not copy) and the moved-from buffer must be left empty, not double-freeing.
    VipsImage* img = nullptr;
    ASSERT_EQ(vips_black(&img, 4, 2, nullptr), 0) << vips_error_buffer();
    PixelBuffer src{img};                       // takes ownership
    ASSERT_TRUE(src.isValid());
    VipsImage* const raw = src.vipsImage();

    PixelBuffer moved{std::move(src)};
    EXPECT_TRUE(moved.isValid());
    EXPECT_EQ(moved.vipsImage(), raw) << "move must transfer the same VipsImage, not copy it";
    EXPECT_EQ(moved.width(),  4);
    EXPECT_EQ(moved.height(), 2);
    EXPECT_FALSE(src.isValid()) << "moved-from buffer must be left empty";  // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(src.vipsImage(), nullptr);
}

TEST(PixelBufferTest, MoveAssignmentTransfersOwnership)
{
    VipsImage* img = nullptr;
    ASSERT_EQ(vips_black(&img, 6, 3, nullptr), 0) << vips_error_buffer();
    PixelBuffer src{img};
    VipsImage* const raw = src.vipsImage();

    PixelBuffer dst;                            // starts empty
    ASSERT_FALSE(dst.isValid());
    dst = std::move(src);

    EXPECT_TRUE(dst.isValid());
    EXPECT_EQ(dst.vipsImage(), raw) << "move-assignment must transfer the same VipsImage";
    EXPECT_EQ(dst.width(),  6);
    EXPECT_EQ(dst.height(), 3);
    EXPECT_FALSE(src.isValid()) << "moved-from buffer must be left empty";  // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(src.vipsImage(), nullptr);
}

// ---------------------------------------------------------------------------
// MarginCropper — invalid-buffer guard
// ---------------------------------------------------------------------------

TEST(MarginCropperTest, ThrowsOnInvalidSourceBuffer)
{
    const MarginCropper cropper;
    const PixelBuffer   empty;
    const Models::Margins margins{10, 10, 10, 10};

    EXPECT_THROW((void)cropper.crop(empty, margins), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Scaler — construction only (no file I/O)
// ---------------------------------------------------------------------------

TEST(ScalerTest, ConstructorDefaultWidth)
{
    // Scaler is stateless — constructing one should never throw.
    EXPECT_NO_THROW({ Scaler s; (void)s; });
}

TEST(ScalerTest, ScaleNonExistentFileThrows)
{
    // A path that does not exist: vips_image_new_from_file fails, so scale() must throw (scaler.cpp:69),
    // never crash or hand back an invalid buffer. Uses a unique temp path that is never created.
    namespace fs = std::filesystem;
    const std::string missing =
        (fs::temp_directory_path() / "pm-does-not-exist-1a2b3c4d.png").string();
    ASSERT_FALSE(fs::exists(missing));
    EXPECT_THROW((void)Scaler{}.scale(missing, /*targetWidth=*/100), std::runtime_error);
}

// --- EXIF autorotation (Step 2b) — VIPS is initialised by test_scaled_strip's global env -------------

TEST(ScalerTest, AutorotatesOrientation6ToPortrait)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pm-scaler-orient6";
    fs::remove_all(dir); fs::create_directories(dir);
    const std::string src = (dir / "stored_landscape.jpg").string();

    // A 400x200 landscape image tagged EXIF Orientation 6 (rotate 90° cw → displays portrait 200x400).
    {
        VipsImage* img = nullptr;
        ASSERT_EQ(vips_black(&img, 400, 200, nullptr), 0) << vips_error_buffer();
        vips_image_set_int(img, VIPS_META_ORIENTATION, 6);
        ASSERT_EQ(vips_jpegsave(img, src.c_str(), nullptr), 0) << vips_error_buffer();
        g_object_unref(img);
    }

    const ScaledImage scaled = Scaler{}.scale(src, /*targetWidth=*/100);

    // Autorotated to display orientation (200x400 portrait) before scaling → 100x200. Without autorot it
    // would be 100x50 (landscape). The tag is also dropped, so the scaled buffer carries no orientation.
    EXPECT_EQ(scaled.buffer.width(), 100);
    EXPECT_GT(scaled.buffer.height(), scaled.buffer.width())
        << "Orientation 6 must render portrait; got "
        << scaled.buffer.width() << "x" << scaled.buffer.height();
    EXPECT_EQ(vips_image_get_typeof(scaled.buffer.vipsImage(), VIPS_META_ORIENTATION), 0)
        << "autorot should strip the orientation tag";

    fs::remove_all(dir);
}

TEST(ScalerTest, NoOrientationTagLeavesAspectUnchanged)
{
    // Idempotent for the Procreate / untagged case: a plain 400x200 landscape stays landscape.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pm-scaler-noorient";
    fs::remove_all(dir); fs::create_directories(dir);
    const std::string src = (dir / "plain.png").string();
    {
        VipsImage* img = nullptr;
        ASSERT_EQ(vips_black(&img, 400, 200, nullptr), 0) << vips_error_buffer();
        ASSERT_EQ(vips_image_write_to_file(img, src.c_str(), nullptr), 0) << vips_error_buffer();
        g_object_unref(img);
    }

    const ScaledImage scaled = Scaler{}.scale(src, /*targetWidth=*/100);
    EXPECT_EQ(scaled.buffer.width(), 100);
    EXPECT_EQ(scaled.buffer.height(), 50) << "no orientation tag → unchanged landscape aspect";

    fs::remove_all(dir);
}

} // namespace Platemaker::Core
