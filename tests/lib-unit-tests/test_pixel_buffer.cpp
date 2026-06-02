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

    EXPECT_THROW(cropper.crop(empty, margins), std::invalid_argument);
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

} // namespace Platemaker::Core
