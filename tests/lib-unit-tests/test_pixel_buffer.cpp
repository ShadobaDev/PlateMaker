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
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <gtest/gtest.h>

#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/core/margin_cropper/margin_cropper.hpp>
#include <platemaker/core/scaler/scaler.hpp>
#include <platemaker/models/common_types.hpp>

#include <vips/vips.h>

#include <filesystem>
#include <limits>
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

    // Onto itself, as a swap through a reference can: the image stays, and stays owned once.
    PixelBuffer& same = dst;
    dst = std::move(same);
    EXPECT_EQ(dst.vipsImage(), raw);
    EXPECT_EQ(G_OBJECT(raw)->ref_count, 1u);
}

// ---------------------------------------------------------------------------
// PixelBuffer — release() / clone()
// ---------------------------------------------------------------------------

TEST(PixelBufferTest, ReleaseHandsOverOwnershipAndEmptiesBuffer)
{
    // release() must return the raw VipsImage and leave the buffer empty, so the destructor does not
    // unref an image the caller now owns. We re-wrap the released pointer to balance the reference.
    VipsImage* img = nullptr;
    ASSERT_EQ(vips_black(&img, 5, 7, nullptr), 0) << vips_error_buffer();
    PixelBuffer buf{img};
    VipsImage* const raw = buf.vipsImage();

    VipsImage* const taken = buf.release();
    EXPECT_EQ(taken, raw) << "release must hand back the very image it held";
    EXPECT_FALSE(buf.isValid()) << "released buffer must be left empty";
    EXPECT_EQ(buf.vipsImage(), nullptr);

    PixelBuffer{taken}; // re-adopt so the single reference is dropped exactly once
}

TEST(PixelBufferTest, CloneOfEmptyBufferIsEmpty)
{
    const PixelBuffer empty;
    const PixelBuffer copy = empty.clone();
    EXPECT_FALSE(copy.isValid());
    EXPECT_EQ(copy.vipsImage(), nullptr);
}

TEST(PixelBufferTest, CloneProducesAnIndependentImageOfTheSameShape)
{
    VipsImage* img = nullptr;
    ASSERT_EQ(vips_black(&img, 8, 4, nullptr), 0) << vips_error_buffer();
    const PixelBuffer src{img};

    const PixelBuffer copy = src.clone();
    ASSERT_TRUE(copy.isValid());
    EXPECT_EQ(copy.width(),  8);
    EXPECT_EQ(copy.height(), 4);
    EXPECT_NE(copy.vipsImage(), src.vipsImage())
        << "clone must be a distinct VipsImage, not an aliased reference to the source";
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
// MarginCropper — safe-area geometry
// ---------------------------------------------------------------------------

namespace {

/// A synthetic RGB image of the given size, for crop geometry tests.
PixelBuffer blackImage(int w, int h)
{
    VipsImage* img = nullptr;
    EXPECT_EQ(vips_black(&img, w, h, nullptr), 0) << vips_error_buffer();
    return PixelBuffer{img};
}

} // namespace

TEST(MarginCropperTest, CropsToTheSafeAreaRectangle)
{
    const MarginCropper cropper;
    // 100x80 source, margins top=5 right=10 bottom=15 left=20 → safe area 70x60.
    const Models::Margins margins{5, 10, 15, 20};
    const PixelBuffer out = cropper.crop(blackImage(100, 80), margins);
    EXPECT_EQ(out.width(),  100 - 20 - 10);
    EXPECT_EQ(out.height(), 80 - 5 - 15);
}

TEST(MarginCropperTest, ThrowsWhenHorizontalMarginsConsumeTheWidth)
{
    const MarginCropper cropper;
    // left + right == srcW → w == 0 → the first operand of (w <= 0 || h <= 0) fires.
    const Models::Margins margins{0, 50, 0, 50};
    EXPECT_THROW((void)cropper.crop(blackImage(100, 80), margins), std::invalid_argument);
}

TEST(MarginCropperTest, NegativeMarginIsAnInvalidArgument)
{
    // A negative margin keeps the area's size positive but would start it outside the image: refused as
    // the margins' fault, each side on its own, before libvips is asked.
    const MarginCropper cropper;
    for (const Models::Margins margins : {Models::Margins{-1, 0, 0, 0}, Models::Margins{0, -1, 0, 0},
                                          Models::Margins{0, 0, -1, 0}, Models::Margins{0, 0, 0, -10}})
        EXPECT_THROW((void)cropper.crop(blackImage(100, 80), margins), std::invalid_argument);
}

TEST(MarginCropperTest, MarginsThatOverflowAnIntLeaveNoSafeArea)
{
    // 100 - INT_MAX - INT_MAX wraps to a positive int; the area is computed in 64 bits instead.
    const MarginCropper cropper;
    const int big = std::numeric_limits<int>::max();
    EXPECT_THROW((void)cropper.crop(blackImage(100, 80), Models::Margins{0, big, 0, big}), std::invalid_argument);
    EXPECT_THROW((void)cropper.crop(blackImage(100, 80), Models::Margins{big, 0, big, 0}), std::invalid_argument);
}

TEST(MarginCropperTest, ThrowsWhenVerticalMarginsExceedTheHeight)
{
    const MarginCropper cropper;
    // top + bottom > srcH → h < 0 → the second operand of (w <= 0 || h <= 0) fires.
    const Models::Margins margins{50, 0, 40, 0};
    EXPECT_THROW((void)cropper.crop(blackImage(100, 80), margins), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Scaler — construction only (no file I/O)
// ---------------------------------------------------------------------------

TEST(ScalerTest, ConstructorDefaultWidth)
{
    // Scaler is stateless — constructing one should never throw.
    EXPECT_NO_THROW({ Scaler s; (void)s; });
}

TEST(ScalerTest, ScaleFileRejectsNonPositiveTargetWidth)
{
    // The file overload must reject a zero or negative target before it ever touches libvips
    // (scaler.cpp:56-59). A unique path that is never created proves the guard runs first.
    namespace fs = std::filesystem;
    const std::string any = (fs::temp_directory_path() / "pm-unused-width-guard.png").string();
    EXPECT_THROW((void)Scaler{}.scale(any, /*targetWidth=*/0),  std::runtime_error);
    EXPECT_THROW((void)Scaler{}.scale(any, /*targetWidth=*/-5), std::runtime_error);
}

TEST(ScalerTest, ScaleBufferRejectsNonPositiveTargetWidth)
{
    VipsImage* img = nullptr;
    ASSERT_EQ(vips_black(&img, 40, 20, nullptr), 0) << vips_error_buffer();
    PixelBuffer buf{img};
    EXPECT_THROW((void)Scaler{}.scale(std::move(buf), "src.png", /*targetWidth=*/0),
                 std::runtime_error);
}

TEST(ScalerTest, ScaleBufferRejectsInvalidBuffer)
{
    // An empty buffer (null VipsImage) with a positive width must throw, never dereference null.
    EXPECT_THROW((void)Scaler{}.scale(PixelBuffer{}, "src.png", /*targetWidth=*/100),
                 std::runtime_error);
}

TEST(ScalerTest, ScaleBufferResizesToTargetWidthPreservingAspect)
{
    // The in-memory overload: a 40x20 buffer scaled to width 100 → 100x50 (aspect preserved).
    VipsImage* img = nullptr;
    ASSERT_EQ(vips_black(&img, 40, 20, nullptr), 0) << vips_error_buffer();
    PixelBuffer buf{img};

    const ScaledImage scaled = Scaler{}.scale(std::move(buf), "memory.png", /*targetWidth=*/100);
    EXPECT_EQ(scaled.buffer.width(),  100);
    EXPECT_EQ(scaled.buffer.height(), 50);
    EXPECT_EQ(scaled.sourceFilePath, "memory.png") << "the source path must be carried through";
}

TEST(ScalerTest, ScaleBufferBeyondTheWidestImageThrows)
{
    // Size limit: no image is wider than VIPS_MAX_COORD, and past it libvips refuses the resize factor
    // with only "parameter scale not set". One pixel more is refused first, saying which bound.
    VipsImage* img = nullptr;
    ASSERT_EQ(vips_black(&img, 1, 1, nullptr), 0) << vips_error_buffer();
    try {
        (void)Scaler{}.scale(PixelBuffer{img}, "src.png", VIPS_MAX_COORD + 1);
        ADD_FAILURE() << "a width past VIPS_MAX_COORD was accepted";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("targetWidth must be 1 to"), std::string::npos) << e.what();
    }
}

TEST(ScalerTest, ScaleFileBeyondTheWidestImageThrows)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "pm-scaler-limit";
    fs::remove_all(dir); fs::create_directories(dir);
    const std::string src = (dir / "one-pixel.png").string();
    {
        VipsImage* img = nullptr;
        ASSERT_EQ(vips_black(&img, 1, 1, nullptr), 0) << vips_error_buffer();
        ASSERT_EQ(vips_image_write_to_file(img, src.c_str(), nullptr), 0) << vips_error_buffer();
        g_object_unref(img);
    }
    EXPECT_THROW((void)Scaler{}.scale(src, VIPS_MAX_COORD + 1), std::runtime_error);
    vips_error_clear();
    fs::remove_all(dir);
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
