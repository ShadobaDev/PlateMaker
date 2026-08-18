/**
 * \file
 * \brief Unit tests for PixelBuffer and the Scaler / MarginCropper components.
 *
 * These tests cover the low-level pixel-buffer abstraction (RAII, move
 * semantics, accessor correctness) and the pure-crop MarginCropper.
 * Scaler tests require libvips to be initialised with a real file on disk;
 * those cases are currently marked GTEST_SKIP until Stage 1 integration
 * tests are added.
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
#include <string>

namespace Platemaker::Core {

// ---------------------------------------------------------------------------
// PixelBuffer — default construction
// ---------------------------------------------------------------------------

TEST(PixelBufferTest, DefaultConstructorCreatesInvalidBuffer)
{
    PixelBuffer buf;
    EXPECT_FALSE(buf.isValid());
    EXPECT_EQ(buf.get(),    nullptr);
    EXPECT_EQ(buf.width(),  0);
    EXPECT_EQ(buf.height(), 0);
}

// ---------------------------------------------------------------------------
// PixelBuffer — move semantics
// ---------------------------------------------------------------------------

TEST(PixelBufferTest, MoveConstructorTransfersOwnership)
{
    // TODO Stage 1 tests: wrap a real VipsImage* (created via vips_image_new_matrix)
    // and verify that move-construction leaves the source invalid.
    GTEST_SKIP() << "Requires libvips init — implement in Stage 1 integration tests";
}

TEST(PixelBufferTest, MoveAssignmentTransfersOwnership)
{
    GTEST_SKIP() << "Requires libvips init — implement in Stage 1 integration tests";
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
    GTEST_SKIP() << "Requires libvips init — implement in Stage 1 integration tests";
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
    EXPECT_EQ(vips_image_get_typeof(scaled.buffer.get(), VIPS_META_ORIENTATION), 0)
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
