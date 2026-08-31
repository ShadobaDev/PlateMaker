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
StripOverlay overlay(std::string uid, std::string sha, int x, int y, bool enabled = true)
{
    StripOverlay o;
    o.uid = std::move(uid);
    o.bitmapPath = "/tmp/" + o.uid + ".png";
    o.sha256 = std::move(sha);
    o.x = x; o.y = y; o.enabled = enabled;
    return o;
}
} // namespace

TEST(ProcessingSignatureTest, EmptyWhenNothingIsConfigured)
{
    ColourCorrection cc;                       // disabled by default
    EXPECT_TRUE(processingConfigSignature(cc, {}).empty());

    // A disabled grade and a disabled overlay contribute nothing → still empty (matches a pre-feature
    // workspace, so it never forces a needless re-render).
    cc.enabled = false;
    cc.brightness = 0.5;                        // set but disabled → ignored
    std::vector<StripOverlay> ovs{ overlay("o1", "aaa", 0, 0, /*enabled*/ false) };
    EXPECT_TRUE(processingConfigSignature(cc, ovs).empty());
}

TEST(ProcessingSignatureTest, EnablingColourCorrectionChangesTheSignature)
{
    ColourCorrection off;                       // disabled
    ColourCorrection on = off;
    on.enabled = true;
    EXPECT_NE(processingConfigSignature(off, {}), processingConfigSignature(on, {}));

    ColourCorrection brighter = on;
    brighter.brightness += 0.1;
    EXPECT_NE(processingConfigSignature(on, {}), processingConfigSignature(brighter, {}));
}

TEST(ProcessingSignatureTest, ExcludedUidsAreOrderInsensitive)
{
    ColourCorrection a; a.enabled = true; a.excludedInputUids = {"file-1", "file-9"};
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

TEST(ProcessingSignatureTest, OverlayOrderIsSignificant)
{
    ColourCorrection none;
    const auto ab = processingConfigSignature(none, { overlay("a", "sA", 0, 0), overlay("b", "sB", 0, 0) });
    const auto ba = processingConfigSignature(none, { overlay("b", "sB", 0, 0), overlay("a", "sA", 0, 0) });
    EXPECT_NE(ab, ba); // composite order is z-order — it affects the output
}

} // namespace Platemaker::Models
