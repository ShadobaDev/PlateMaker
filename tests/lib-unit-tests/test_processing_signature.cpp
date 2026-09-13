/**
 * \file
 * \brief Unit tests for processingConfigSignature() — the colour-correction / overlay staleness axis.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-31
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/models/processing_steps.hpp>

#include <string>
#include <vector>

namespace Platemaker::Models {

namespace {
StripOverlay overlay(std::string uid, std::string sha, double xFrac, double yFrac,
                     bool enabled = true)
{
    StripOverlay o;
    o.uid = std::move(uid);
    o.assetPath = "/tmp/" + o.uid + ".png";
    o.sha256 = std::move(sha);
    o.xFrac = xFrac; o.yFrac = yFrac; o.enabled = enabled;
    return o;
}
} // namespace

TEST(ProcessingSignatureTest, EmptyWhenNothingIsConfigured)
{
    ColourCorrection cc;                       // neutral by default
    EXPECT_TRUE(processingConfigSignature(cc, {}).empty());

    // A neutral grade and a disabled overlay contribute nothing → still empty (matches a pre-feature
    // workspace, so it never forces a needless re-render).
    std::vector<StripOverlay> ovs{ overlay("o1", "aaa", 0, 0, /*enabled*/ false) };
    EXPECT_TRUE(processingConfigSignature(cc, ovs).empty());

    // Excluding pages from a grade that changes nothing excludes them from nothing, so it is still not
    // a configured step — the same rule isNeutral() applies, checked here because this is the caller
    // whose answer decides whether a chapter re-renders.
    cc.excludedInputUids = {"file-1"};
    EXPECT_TRUE(processingConfigSignature(cc, {}).empty());
}

TEST(ProcessingSignatureTest, GradingChangesTheSignature)
{
    ColourCorrection neutral;
    ColourCorrection graded = neutral;
    graded.brightness = 0.1;
    EXPECT_NE(processingConfigSignature(neutral, {}), processingConfigSignature(graded, {}));

    ColourCorrection brighter = graded;
    brighter.brightness += 0.1;
    EXPECT_NE(processingConfigSignature(graded, {}), processingConfigSignature(brighter, {}));

    // Each of the four knobs on its own is a grade: whichever one is touched, the chapter is stale.
    for (const ColourCorrection& one : {
             [] { ColourCorrection c; c.contrast   = 1.2; return c; }(),
             [] { ColourCorrection c; c.saturation = 0.0; return c; }(),
             [] { ColourCorrection c; c.curves.master = {{0.0, 0.0}, {1.0, 0.5}}; return c; }(),
         })
        EXPECT_NE(processingConfigSignature(neutral, {}), processingConfigSignature(one, {}));
}

TEST(ProcessingSignatureTest, ExcludedUidsAreOrderInsensitive)
{
    ColourCorrection a; a.brightness = 0.2; a.excludedInputUids = {"file-1", "file-9"};
    ColourCorrection b = a;              b.excludedInputUids = {"file-9", "file-1"}; // reordered
    EXPECT_EQ(processingConfigSignature(a, {}), processingConfigSignature(b, {}));

    ColourCorrection c = a;              c.excludedInputUids = {"file-1"};           // different set
    EXPECT_NE(processingConfigSignature(a, {}), processingConfigSignature(c, {}));
}

TEST(ProcessingSignatureTest, OverlayContentPositionAndEnabledMatter)
{
    ColourCorrection none;                              // disabled — isolate the overlay axis
    const auto base = processingConfigSignature(none, { overlay("o1", "sha-A", 10, 20) });

    // Moving it changes the signature.
    EXPECT_NE(base, processingConfigSignature(none, { overlay("o1", "sha-A", 11, 20) }));
    // Swapping its bitmap (new content hash) changes the signature.
    EXPECT_NE(base, processingConfigSignature(none, { overlay("o1", "sha-B", 10, 20) }));
    // Disabling it removes its contribution → empty again.
    EXPECT_TRUE(processingConfigSignature(none, { overlay("o1", "sha-A", 10, 20, false) }).empty());
}

TEST(ProcessingSignatureTest, OverlayBlendModeMatters)
{
    ColourCorrection none;
    StripOverlay over = overlay("o1", "sha-A", 0, 0);
    StripOverlay mult = over;
    over.blend = BlendMode::Over;
    mult.blend = BlendMode::Multiply;
    EXPECT_NE(processingConfigSignature(none, {over}), processingConfigSignature(none, {mult}));
}

TEST(ProcessingSignatureTest, OverlayOrderIsSignificant)
{
    ColourCorrection none;
    const auto ab = processingConfigSignature(none, { overlay("a", "sA", 0, 0), overlay("b", "sB", 0, 0) });
    const auto ba = processingConfigSignature(none, { overlay("b", "sB", 0, 0), overlay("a", "sA", 0, 0) });
    EXPECT_NE(ab, ba); // composite order is z-order — it affects the output
}

} // namespace Platemaker::Models
