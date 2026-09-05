/**
 * \file
 * \brief Unit tests for ScaledStrip.
 *
 * Tests verify the accumulator state machine (empty strip guards, width tracking)
 * and — using synthetic libvips images — the memory contract: that sliceAll()
 * releases each source once the slice cursor has passed it.  Tests that require
 * real image files are stubbed as GTEST_SKIP until Stage 1 fixtures are added.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-02
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/common_types.hpp>

#include <vips/vips.h>

#include <string>
#include <vector>

namespace Platemaker::Core {
namespace {

// ---------------------------------------------------------------------------
// libvips lifecycle — VIPS_INIT must run once per process, before any vips call.
// ---------------------------------------------------------------------------

class VipsEnvironment : public ::testing::Environment {
public:
    void SetUp() override
    {
        ASSERT_EQ(VIPS_INIT("platemaker-lib-tests"), 0)
            << "libvips init failed: " << vips_error_buffer();

        // Disable the operation cache: it would hold extra references to our
        // synthetic sources and mask the very releases these tests assert on.
        vips_cache_set_max(0);
    }

    void TearDown() override { vips_shutdown(); }
};

const auto* const g_vipsEnv =
    ::testing::AddGlobalTestEnvironment(new VipsEnvironment);

/**
 * \brief A synthetic strip source plus a borrowed reference used to observe its lifetime.
 *
 * The strip owns one reference to the image. This helper keeps a second one, so
 * `refCount() == 1` means "the strip has let go of its pixels" — which is exactly
 * what the memory contract promises.
 */
struct TrackedSource {
    VipsImage*  image = nullptr;  //!< Borrowed — the strip owns the other reference.
    std::string path;

    [[nodiscard]] int refCount() const
    {
        return static_cast<int>(G_OBJECT(image)->ref_count);
    }

    [[nodiscard]] bool releasedByStrip() const { return refCount() == 1; }
};

/// Appends a synthetic black image of the given size and returns a handle for tracking it.
TrackedSource appendSource(ScaledStrip& strip, int width, int height, std::string path)
{
    VipsImage* img = nullptr;
    EXPECT_EQ(vips_black(&img, width, height, nullptr), 0)
        << "vips_black failed: " << vips_error_buffer();

    TrackedSource src{img, std::move(path)};
    g_object_ref(img);  // our observation reference; PixelBuffer takes the original
    strip.append(ScaledImage{PixelBuffer{img}, src.path});
    return src;
}

/// Appends a synthetic sRGB source with an explicit band count: 3 = RGB, 4 = RGBA (genuine
/// alpha, so vips_image_hasalpha() is true). Returns a handle that observes the image the strip
/// stores — for the RGB case that is exactly the image normalizeBandCounts() wraps with addalpha,
/// so releasing the wrapper cascades to releasing it, which is what releasedByStrip() detects.
TrackedSource appendRgbSource(ScaledStrip& strip, int width, int height, int bands, std::string path)
{
    VipsImage* black = nullptr;
    EXPECT_EQ(vips_black(&black, width, height, "bands", 3, nullptr), 0)
        << "vips_black failed: " << vips_error_buffer();

    VipsImage* srgb = nullptr;
    EXPECT_EQ(vips_copy(black, &srgb, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr), 0)
        << "vips_copy failed: " << vips_error_buffer();
    g_object_unref(black);

    VipsImage* stored = srgb;
    if (bands >= 4) {
        VipsImage* withAlpha = nullptr;
        EXPECT_EQ(vips_addalpha(srgb, &withAlpha, nullptr), 0)
            << "vips_addalpha failed: " << vips_error_buffer();
        g_object_unref(srgb);
        stored = withAlpha;
    }

    TrackedSource src{stored, std::move(path)};
    g_object_ref(stored);  // observation reference; PixelBuffer takes the original
    strip.append(ScaledImage{PixelBuffer{stored}, src.path});
    return src;
}

} // namespace

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
    ScaledStrip strip;
    Infrastructure::CancellationToken cancel;
    EXPECT_THROW(
        strip.sliceAll(1280, Models::LastSlicePolicy::KeepAsIs, cancel,
                       [](SliceResult&&) { return true; }),
        std::runtime_error
    );
}

TEST(ScaledStripTest, SliceAllWithZeroSliceHeightThrows)
{
    ScaledStrip strip;
    (void)appendSource(strip, 800, 100, "a.png");

    Infrastructure::CancellationToken cancel;
    EXPECT_THROW(
        strip.sliceAll(0, Models::LastSlicePolicy::KeepAsIs, cancel,
                       [](SliceResult&&) { return true; }),
        std::runtime_error
    );
}

TEST(ScaledStripTest, SliceAllWithEmptyCallbackThrows)
{
    ScaledStrip strip;
    (void)appendSource(strip, 800, 100, "a.png");

    Infrastructure::CancellationToken cancel;
    EXPECT_THROW(
        strip.sliceAll(100, Models::LastSlicePolicy::KeepAsIs, cancel, {}),
        std::runtime_error
    );
}

// ---------------------------------------------------------------------------
// ScaledStrip — memory contract
//
// The class promises at most two decoded sources alive at once. Nothing in a
// successful render would notice if that broke — output bytes stay identical and
// only peak RSS grows — so it is asserted here directly.
// ---------------------------------------------------------------------------

TEST(ScaledStripTest, SliceAllReleasesSourcesTheCursorHasPassed)
{
    // Three sources, one slice each: slice i draws only from source i.
    ScaledStrip strip;
    const TrackedSource a = appendSource(strip, 800, 100, "a.png");
    const TrackedSource b = appendSource(strip, 800, 100, "b.png");
    const TrackedSource c = appendSource(strip, 800, 100, "c.png");
    ASSERT_EQ(strip.totalHeight(), 300);

    Infrastructure::CancellationToken cancel;
    int seen = 0;

    strip.sliceAll(100, Models::LastSlicePolicy::KeepAsIs, cancel,
        [&](SliceResult&& slice) {
            EXPECT_EQ(slice.index, seen);

            // While slice i is in flight, sources before it are gone and source i is not.
            switch (seen) {
                case 0:
                    EXPECT_FALSE(a.releasedByStrip()) << "source 0 is in the current slice";
                    break;
                case 1:
                    EXPECT_TRUE(a.releasedByStrip())  << "source 0 is behind the cursor";
                    EXPECT_FALSE(b.releasedByStrip()) << "source 1 is in the current slice";
                    break;
                case 2:
                    EXPECT_TRUE(b.releasedByStrip())  << "source 1 is behind the cursor";
                    EXPECT_FALSE(c.releasedByStrip()) << "source 2 is in the current slice";
                    break;
                default:
                    ADD_FAILURE() << "unexpected slice index " << seen;
            }

            ++seen;
            return true;
        });

    EXPECT_EQ(seen, 3);
}

TEST(ScaledStripTest, SliceAllKeepsSourceStraddlingASliceBoundary)
{
    // Slice height 150 over two 100px sources: slice 0 spans both, so neither may
    // be released while it is being built. This is the "at most two" case.
    ScaledStrip strip;
    const TrackedSource a = appendSource(strip, 800, 100, "a.png");
    const TrackedSource b = appendSource(strip, 800, 100, "b.png");

    Infrastructure::CancellationToken cancel;
    int seen = 0;

    strip.sliceAll(150, Models::LastSlicePolicy::KeepAsIs, cancel,
        [&](SliceResult&& slice) {
            if (seen == 0) {
                EXPECT_EQ(slice.sourceMap.size(), 2u) << "slice 0 straddles the boundary";
                EXPECT_FALSE(a.releasedByStrip());
                EXPECT_FALSE(b.releasedByStrip());
            } else {
                // Slice 1 (150..200) lives entirely in source b; a is now behind it.
                EXPECT_TRUE(a.releasedByStrip());
                EXPECT_FALSE(b.releasedByStrip());
            }
            ++seen;
            return true;
        });

    EXPECT_EQ(seen, 2);
}

TEST(ScaledStripTest, SliceAllKeepsEverySourceFeedingTheCurrentSlice)
{
    // Sources may be shorter than a slice — e.g. a leftover tail, a small extra
    // panel, and a spacer added without redrawing the surrounding pages. Then one
    // output slice is fed by several inputs at once, and the live set is however
    // many that takes, not two.
    //
    // 200 + 500 + 100 + 480 = 1280 → slice 0 draws from all four sources.
    ScaledStrip strip;
    const TrackedSource tail    = appendSource(strip, 800, 200, "tail.png");
    const TrackedSource panel   = appendSource(strip, 800, 500, "panel.png");
    const TrackedSource spacer  = appendSource(strip, 800, 100, "spacer.png");
    const TrackedSource nextTop = appendSource(strip, 800, 900, "next.png");
    ASSERT_EQ(strip.totalHeight(), 1700);

    Infrastructure::CancellationToken cancel;
    int seen = 0;

    strip.sliceAll(1280, Models::LastSlicePolicy::KeepAsIs, cancel,
        [&](SliceResult&& slice) {
            if (seen == 0) {
                EXPECT_EQ(slice.sourceMap.size(), 4u)
                    << "slice 0 must be composed from all four sources";
                // None of them may have been released — every one is still needed.
                EXPECT_FALSE(tail.releasedByStrip());
                EXPECT_FALSE(panel.releasedByStrip());
                EXPECT_FALSE(spacer.releasedByStrip());
                EXPECT_FALSE(nextTop.releasedByStrip());
            } else {
                // Slice 1 (1280..1700) lives in next.png alone; the first three are
                // now behind the cursor and must all be gone.
                EXPECT_EQ(slice.sourceMap.size(), 1u);
                EXPECT_TRUE(tail.releasedByStrip());
                EXPECT_TRUE(panel.releasedByStrip());
                EXPECT_TRUE(spacer.releasedByStrip());
                EXPECT_FALSE(nextTop.releasedByStrip());
            }
            ++seen;
            return true;
        });

    EXPECT_EQ(seen, 2);
}

TEST(ScaledStripTest, SliceAllStopsWhenCallbackReturnsFalse)
{
    ScaledStrip strip;
    (void)appendSource(strip, 800, 100, "a.png");
    (void)appendSource(strip, 800, 100, "b.png");

    Infrastructure::CancellationToken cancel;
    int seen = 0;

    strip.sliceAll(100, Models::LastSlicePolicy::KeepAsIs, cancel,
        [&](SliceResult&&) {
            ++seen;
            return false;   // e.g. a save error in the pipeline
        });

    EXPECT_EQ(seen, 1) << "slicing must stop at the first refusal";
}

TEST(ScaledStripTest, SliceAllStopsWhenCancelled)
{
    ScaledStrip strip;
    (void)appendSource(strip, 800, 100, "a.png");
    (void)appendSource(strip, 800, 100, "b.png");

    Infrastructure::CancellationToken cancel;
    cancel.cancel();

    int seen = 0;
    strip.sliceAll(100, Models::LastSlicePolicy::KeepAsIs, cancel,
        [&](SliceResult&&) { ++seen; return true; });

    EXPECT_EQ(seen, 0);
}

// ---------------------------------------------------------------------------
// ScaledStrip — band-count normalisation
//
// vips_join demands equal band counts. A strip mixing RGB (3-band) and RGBA
// (4-band) sources used to abort the whole render at the first slice straddling
// the boundary. sliceAll() now promotes to the widest layout first (opaque alpha
// added, never flattened), so the join succeeds and alpha is preserved.
// ---------------------------------------------------------------------------

TEST(ScaledStripTest, SliceAllNormalisesMixedRgbAndRgbaStrip)
{
    // 3-band RGB then 4-band RGBA; slice height 150 over two 100px sources → slice 0 straddles the
    // boundary and would join a 3-band part with a 4-band part. Every emitted slice must be RGBA.
    ScaledStrip strip;
    appendRgbSource(strip, 800, 100, 3, "rgb.jpg");
    appendRgbSource(strip, 800, 100, 4, "rgba.png");

    Infrastructure::CancellationToken cancel;
    int seen = 0;
    EXPECT_NO_THROW(
        strip.sliceAll(150, Models::LastSlicePolicy::KeepAsIs, cancel,
            [&](SliceResult&& s) {
                EXPECT_EQ(vips_image_get_bands(s.image.vipsImage()), 4)
                    << "mixed strip must be promoted to RGBA (slice " << s.index << ")";
                ++seen;
                return true;
            }));
    EXPECT_EQ(seen, 2);
}

TEST(ScaledStripTest, SliceAllLeavesUniformRgbStripThreeBand)
{
    // All-RGB strip: promotion is a no-op — no phantom alpha is added, output stays 3-band.
    ScaledStrip strip;
    appendRgbSource(strip, 800, 100, 3, "a.jpg");
    appendRgbSource(strip, 800, 100, 3, "b.jpg");

    Infrastructure::CancellationToken cancel;
    int seen = 0;
    strip.sliceAll(150, Models::LastSlicePolicy::KeepAsIs, cancel,
        [&](SliceResult&& s) {
            EXPECT_EQ(vips_image_get_bands(s.image.vipsImage()), 3)
                << "uniform RGB strip must remain 3-band";
            ++seen;
            return true;
        });
    EXPECT_EQ(seen, 2);
}

TEST(ScaledStripTest, MixedBandStripStillReleasesSourcesTheCursorHasPassed)
{
    // The band-count path must not break the memory contract. normalizeBandCounts() only reads
    // headers and appends lazy colourspace/addalpha nodes — it never pulls pixels — so sources are
    // still decoded lazily and released once the slice cursor passes them. Here a 3-band + 4-band
    // strip forces the promotion path (not the early-out): the RGB source is wrapped with addalpha,
    // and the wrapper must not pin the underlying image. One 100px source per 100px slice, so slice i
    // draws only from source i — exactly the release pattern of the uniform-strip test, but proven
    // through the wrapping path. Every slice comes out RGBA, confirming promotion actually ran.
    ScaledStrip strip;
    const TrackedSource rgb  = appendRgbSource(strip, 800, 100, 3, "rgb.jpg");   // wrapped by addalpha
    const TrackedSource rgba = appendRgbSource(strip, 800, 100, 4, "rgba.png");  // already widest
    ASSERT_EQ(strip.totalHeight(), 200);

    Infrastructure::CancellationToken cancel;
    int seen = 0;
    strip.sliceAll(100, Models::LastSlicePolicy::KeepAsIs, cancel,
        [&](SliceResult&& slice) {
            EXPECT_EQ(slice.index, seen);
            EXPECT_EQ(vips_image_get_bands(slice.image.vipsImage()), 4)
                << "mixed strip must be promoted to RGBA (slice " << seen << ")";

            switch (seen) {
                case 0:
                    EXPECT_FALSE(rgb.releasedByStrip())  << "source 0 is in the current slice";
                    break;
                case 1:
                    EXPECT_TRUE(rgb.releasedByStrip())
                        << "source 0 is behind the cursor — its addalpha wrapper must have been released";
                    EXPECT_FALSE(rgba.releasedByStrip()) << "source 1 is in the current slice";
                    break;
                default:
                    ADD_FAILURE() << "unexpected slice index " << seen;
            }

            ++seen;
            return true;
        });

    EXPECT_EQ(seen, 2);
}

// ---------------------------------------------------------------------------
// ScaledStrip — slicing with all three LastSlicePolicy values
// ---------------------------------------------------------------------------

TEST(ScaledStripTest, SliceAllCropPolicyDiscardsRemainder)
{
    // 250px of strip, 100px slices → 2 full slices, 50px tail dropped.
    ScaledStrip strip;
    (void)appendSource(strip, 800, 250, "a.png");

    Infrastructure::CancellationToken cancel;
    std::vector<int> indices;
    strip.sliceAll(100, Models::LastSlicePolicy::Crop, cancel,
        [&](SliceResult&& s) { indices.push_back(s.index); return true; });

    EXPECT_EQ(indices, (std::vector<int>{0, 1}));
}

TEST(ScaledStripTest, SliceAllPadWhiteProducesFullHeightTailSlice)
{
    ScaledStrip strip;
    (void)appendSource(strip, 800, 250, "a.png");

    Infrastructure::CancellationToken cancel;
    int tailHeight = -1;
    int count      = 0;
    strip.sliceAll(100, Models::LastSlicePolicy::PadWhite, cancel,
        [&](SliceResult&& s) {
            if (s.index == 2) tailHeight = s.image.height();
            ++count;
            return true;
        });

    EXPECT_EQ(count, 3);
    EXPECT_EQ(tailHeight, 100) << "PadWhite must extend the tail to the full slice height";
}

TEST(ScaledStripTest, SliceAllKeepAsIsPreservesShortTailSlice)
{
    ScaledStrip strip;
    (void)appendSource(strip, 800, 250, "a.png");

    Infrastructure::CancellationToken cancel;
    int tailHeight = -1;
    int count      = 0;
    strip.sliceAll(100, Models::LastSlicePolicy::KeepAsIs, cancel,
        [&](SliceResult&& s) {
            if (s.index == 2) tailHeight = s.image.height();
            ++count;
            return true;
        });

    EXPECT_EQ(count, 3);
    EXPECT_EQ(tailHeight, 50) << "KeepAsIs must preserve the natural tail height";
}

} // namespace Platemaker::Core
