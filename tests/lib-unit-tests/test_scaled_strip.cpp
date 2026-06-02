/**
 * \file
 * \brief Unit tests for ScaledStrip and Slicer.
 *
 * Tests verify the accumulator state machine (empty strip guards, width
 * tracking) and the Slicer configuration accessors.  Tests that require
 * actual pixel data (sliceAll, buildSlice) are stubbed as GTEST_SKIP until
 * Stage 1 integration tests are added.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-02
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/core/slicer/slicer.hpp>
#include <platemaker/models/common_types.hpp>

namespace Platemaker::Core {

// ---------------------------------------------------------------------------
// ScaledStrip — initial state
// ---------------------------------------------------------------------------

TEST(ScaledStripTest, DefaultConstructorEmptyStrip)
{
    ScaledStrip strip;
    EXPECT_EQ(strip.totalHeight(), 0);
    EXPECT_EQ(strip.width(),       0);
}

TEST(ScaledStripTest, SliceAllOnEmptyStripThrows)
{
    const ScaledStrip strip;
    EXPECT_THROW(
        (void)strip.sliceAll(1280, Models::LastSlicePolicy::KeepAsIs),
        std::runtime_error
    );
}

TEST(ScaledStripTest, SliceAllWithZeroSliceHeightThrows)
{
    GTEST_SKIP()
        << "Requires an appended ScaledImage — implement in Stage 1 integration tests";
}

// ---------------------------------------------------------------------------
// ScaledStrip — slicing with all three LastSlicePolicy values
// ---------------------------------------------------------------------------

TEST(ScaledStripTest, SliceAllCropPolicyDiscardsRemainder)
{
    GTEST_SKIP() << "Requires libvips init and real pixel data";
}

TEST(ScaledStripTest, SliceAllPadWhiteProducesFullHeightTailSlice)
{
    GTEST_SKIP() << "Requires libvips init and real pixel data";
}

TEST(ScaledStripTest, SliceAllKeepAsIsPreservesShortTailSlice)
{
    GTEST_SKIP() << "Requires libvips init and real pixel data";
}

// ---------------------------------------------------------------------------
// Slicer — configuration accessors
// ---------------------------------------------------------------------------

TEST(SlicerTest, ConstructorStoresParameters)
{
    const Slicer s{1280, Models::LastSlicePolicy::PadWhite};
    EXPECT_EQ(s.sliceHeight(), 1280);
    EXPECT_EQ(s.policy(), Models::LastSlicePolicy::PadWhite);
}

TEST(SlicerTest, AllPoliciesConstructWithoutThrow)
{
    // Use () constructor syntax — commas inside () are safe from macro argument splitting.
    using P = Models::LastSlicePolicy;
    EXPECT_NO_THROW((void)Slicer(1280, P::Crop));
    EXPECT_NO_THROW((void)Slicer(1280, P::PadWhite));
    EXPECT_NO_THROW((void)Slicer(1280, P::KeepAsIs));
}

} // namespace Platemaker::Core
