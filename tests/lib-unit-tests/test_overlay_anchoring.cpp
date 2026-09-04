/**
 * \file
 * \brief Unit + end-to-end tests for page-anchored overlays — the placement that survives editing.
 *
 * An overlay stores either an absolute strip-Y or a page anchor (\c StripOverlay::anchorInputUid plus a
 * page-relative Y).  \c Models::resolveOverlayAnchors() is the bridge between the two, and the pipeline
 * calls it once the strip layout is settled.
 *
 * The end-to-end tests are the ones that matter: they render a chapter, insert a page *above* the
 * anchored bubble, re-render, and check the bubble is still on its own artwork.  The absolute-placement
 * test is the control — it pins the drift the anchor exists to prevent, so a change that quietly stopped
 * resolving anchors would make the first test fail rather than both tests pass vacuously.
 *
 * libvips is initialised once for the whole test binary by test_scaled_strip.cpp's global environment.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-04
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/core/processing_callbacks/processing_callbacks.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/processing_steps.hpp>
#include <platemaker/models/project_item.hpp>

#include <vips/vips.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Platemaker::Core {
namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// resolveOverlayAnchors — the pure placement bridge
// ---------------------------------------------------------------------------

Models::StripOverlay overlay(const std::string& uid, const std::string& anchor, int x, int y)
{
    Models::StripOverlay o;
    o.uid            = uid;
    o.anchorInputUid = anchor;
    o.x              = x;
    o.y              = y;
    return o;
}

const std::unordered_map<std::string, int> k_layout{{"file-a", 0}, {"file-b", 100}, {"file-c", 250}};

} // namespace

TEST(ResolveOverlayAnchorsTest, AddsTheAnchorPagesTopToThePageRelativeY)
{
    const auto out = Models::resolveOverlayAnchors({overlay("ovl-1", "file-c", 40, 20)}, k_layout);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].y, 270) << "20px down page C, whose top is at strip-Y 250";
    EXPECT_EQ(out[0].x, 40) << "x is untouched — every page shares the strip's x origin";
    EXPECT_TRUE(out[0].anchorInputUid.empty()) << "the result is absolute, so it resolves to itself";
}

TEST(ResolveOverlayAnchorsTest, LeavesAnUnanchoredOverlayExactlyAsItIs)
{
    const auto out = Models::resolveOverlayAnchors({overlay("ovl-1", "", 40, 900)}, k_layout);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].y, 900);
}

TEST(ResolveOverlayAnchorsTest, IsIdempotent)
{
    const auto once  = Models::resolveOverlayAnchors({overlay("ovl-1", "file-b", 0, 10)}, k_layout);
    const auto twice = Models::resolveOverlayAnchors(once, k_layout);

    ASSERT_EQ(twice.size(), 1u);
    EXPECT_EQ(twice[0].y, once[0].y) << "resolving an already-absolute overlay must not add the top twice";
}

TEST(ResolveOverlayAnchorsTest, DropsAndReportsAnOverlayWhoseAnchorPageIsNotInTheLayout)
{
    std::vector<std::string> orphans;
    const auto out = Models::resolveOverlayAnchors(
        {overlay("ovl-keep", "file-b", 0, 10), overlay("ovl-gone", "file-deleted", 0, 10)},
        k_layout, &orphans);

    ASSERT_EQ(out.size(), 1u) << "an overlay with nothing to sit on must not fall through to strip-Y 10";
    EXPECT_EQ(out[0].uid, "ovl-keep");
    EXPECT_EQ(orphans, (std::vector<std::string>{"ovl-gone"}));
}

// ---------------------------------------------------------------------------
// End-to-end: the drift an anchor prevents
// ---------------------------------------------------------------------------

namespace {

/// A temp directory unique to one test, removed on destruction.
class TempDir {
public:
    explicit TempDir(const std::string& tag)
        : m_path(fs::temp_directory_path() /
                 ("platemaker-ovl-anchor-" + tag + "-" +
                  std::to_string(::testing::UnitTest::GetInstance()->random_seed())))
    {
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(m_path, ec); }

    [[nodiscard]] std::string file(const std::string& name) const { return (m_path / name).string(); }
    [[nodiscard]] std::string dir() const { return m_path.string(); }

private:
    fs::path m_path;
};

/// Writes a solid PNG of \p bands channels (3 = RGB page, 4 = RGBA overlay) and returns its path.
std::string writeSolid(const TempDir& tmp, const std::string& name, int w, int h,
                       const std::vector<std::uint8_t>& colour)
{
    const int bands = static_cast<int>(colour.size());
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * bands);
    for (std::size_t i = 0; i < px.size(); i += bands)
        for (int b = 0; b < bands; ++b)
            px[i + static_cast<std::size_t>(b)] = colour[static_cast<std::size_t>(b)];

    VipsImage* raw = vips_image_new_from_memory_copy(px.data(), px.size(), w, h, bands, VIPS_FORMAT_UCHAR);
    EXPECT_NE(raw, nullptr) << vips_error_buffer();
    VipsImage* typed = nullptr;
    EXPECT_EQ(vips_copy(raw, &typed, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr), 0)
        << vips_error_buffer();
    g_object_unref(raw);

    const std::string path = tmp.file(name);
    EXPECT_EQ(vips_image_write_to_file(typed, path.c_str(), nullptr), 0) << vips_error_buffer();
    g_object_unref(typed);
    return path;
}

/// The RGB of pixel (x,y) in an output slice — the alpha band a composite adds is ignored.
std::vector<int> rgbAt(const std::string& path, int x, int y)
{
    VipsImage* im = vips_image_new_from_file(path.c_str(), nullptr);
    EXPECT_NE(im, nullptr) << vips_error_buffer();
    double* v = nullptr;
    int     n = 0;
    EXPECT_EQ(vips_getpoint(im, &v, &n, x, y, nullptr), 0) << vips_error_buffer();
    std::vector<int> rgb;
    for (int i = 0; i < n && i < 3; ++i)
        rgb.push_back(static_cast<int>(v[i] + 0.5));
    g_free(v);
    g_object_unref(im);
    return rgb;
}

Models::InputFile input(const std::string& uid, const std::string& path)
{
    Models::InputFile f;
    f.uid      = uid;
    f.filePath = path;
    f.status   = Models::FileStatus::Pending;
    return f;
}

/// 100px wide, 100px slices — with 100px-tall pages, one page is exactly one slice.
Models::OutputProfile output()
{
    Models::OutputProfile op;
    op.targetWidth  = 100;
    op.sliceHeight  = 100;
    op.outputFormat = Models::OutputFormat::PNG;
    op.startIndex   = 1;
    return op;
}

// Page colours — each page is a distinct flat colour, so "which page is this pixel on?" is decidable
// from the pixel alone. That is the whole assertion: the bubble must stay on the yellow one.
const std::vector<std::uint8_t> k_red    {200,  30,  30};
const std::vector<std::uint8_t> k_green  { 30, 200,  30};
const std::vector<std::uint8_t> k_yellow {200, 200,  30};   // page C — the anchor page
const std::vector<std::uint8_t> k_magenta{200,  30, 200};
const std::vector<std::uint8_t> k_cyan   { 30, 200, 200};   // the page inserted above C
const std::vector<std::uint8_t> k_white  {255, 255, 255, 255}; // the bubble (opaque RGBA)

/**
 * \brief One rendered chapter: the four pages A/B/C/D, optionally with a 50px page inserted at index 1.
 *
 * With no insert every page is exactly one slice, so page C occupies strip 200..300 → output_003.png.
 * With the insert everything below it drops 50px and C occupies 250..350, which straddles the same
 * output_003.png (strip 200..300) — the slice a *drifting* overlay would also land on, so the two cases
 * are compared on the same file and the bubble's Y within it is the only thing that moves.
 */
struct Chapter {
    std::vector<Models::InputFile> inputs;

    static Chapter build(const TempDir& tmp, bool withInsertedPage)
    {
        Chapter c;
        c.inputs.push_back(input("file-a", writeSolid(tmp, "a.png", 100, 100, k_red)));
        if (withInsertedPage)
            c.inputs.push_back(input("file-x", writeSolid(tmp, "x.png", 100, 50, k_cyan)));
        c.inputs.push_back(input("file-b", writeSolid(tmp, "b.png", 100, 100, k_green)));
        c.inputs.push_back(input("file-c", writeSolid(tmp, "c.png", 100, 100, k_yellow)));
        c.inputs.push_back(input("file-d", writeSolid(tmp, "d.png", 100, 100, k_magenta)));
        return c;
    }
};

/// Renders \p inputs with \p overlays into a fresh subdirectory of \p tmp and returns that directory.
std::string render(const TempDir& tmp, const std::string& tag,
                   const std::vector<Models::InputFile>&     inputs,
                   const std::vector<Models::StripOverlay>&  overlays,
                   std::vector<std::string>*                 warnings = nullptr)
{
    const std::string outDir = tmp.file(tag);
    fs::create_directories(outDir);

    ProcessingCallbacks cb;
    if (warnings) {
        cb.onLog = [warnings](ProcessingLogLevel level, const std::string& msg) {
            if (level == ProcessingLogLevel::Warning)
                warnings->push_back(msg);
        };
    }

    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::run(
        inputs, output(), {}, {}, outDir, cancel, cb, nullptr, {}, {}, overlays);

    EXPECT_FALSE(outcome.failed) << (outcome.error ? outcome.error->message : "");
    return outDir;
}

/// The bubble: 20×20 opaque white, 10px in from the left, 20px down page C.
Models::StripOverlay bubble(const std::string& bitmapPath, const std::string& anchor, int y)
{
    Models::StripOverlay o;
    o.uid            = "ovl-bubble";
    o.bitmapPath     = bitmapPath;
    o.anchorInputUid = anchor;
    o.x              = 10;
    o.y              = y;
    return o;
}

} // namespace

/**
 * The point of the whole feature: inserting a page above the anchor page moves the bubble down with its
 * own artwork instead of leaving it behind on someone else's.
 */
TEST(OverlayAnchoringTest, AnAnchoredBubbleStaysOnItsPageWhenAPageIsInsertedAbove)
{
    TempDir tmp("anchored");
    const std::string bmp = writeSolid(tmp, "bubble.png", 20, 20, k_white);

    // Before: page C spans strip 200..300 → it *is* output_003.png, bubble at local y 20..40.
    const std::string before =
        render(tmp, "before", Chapter::build(tmp, false).inputs, {bubble(bmp, "file-c", 20)});
    EXPECT_EQ(rgbAt(before + "/output_003.png", 15, 25), (std::vector<int>{255, 255, 255}));
    EXPECT_EQ(rgbAt(before + "/output_003.png", 15, 60), (std::vector<int>{200, 200, 30}))
        << "below the bubble is page C's own yellow";

    // After: a 50px page slots in at index 1, so page C spans 250..350. The bubble must ride down with
    // it — same file, 50px lower — and must still be sitting on yellow.
    const std::string after =
        render(tmp, "after", Chapter::build(tmp, true).inputs, {bubble(bmp, "file-c", 20)});
    EXPECT_EQ(rgbAt(after + "/output_003.png", 15, 75), (std::vector<int>{255, 255, 255}))
        << "the bubble did not follow its page down";
    EXPECT_EQ(rgbAt(after + "/output_003.png", 15, 95), (std::vector<int>{200, 200, 30}))
        << "the bubble is no longer on page C's artwork";
    EXPECT_EQ(rgbAt(after + "/output_003.png", 15, 25), (std::vector<int>{30, 200, 30}))
        << "where the bubble used to be is now plain page B";
}

/**
 * The control. An overlay with no anchor is absolute by definition, so it must *not* follow — this is
 * the drift the anchor exists to prevent, and pinning it here is what stops the test above from passing
 * for the wrong reason.
 */
TEST(OverlayAnchoringTest, AnUnanchoredBubbleStaysAtItsStripYAndEndsUpOnAnotherPage)
{
    TempDir tmp("absolute");
    const std::string bmp = writeSolid(tmp, "bubble.png", 20, 20, k_white);

    // Strip-Y 220 = the same spot the anchored bubble started at.
    const std::string after =
        render(tmp, "after", Chapter::build(tmp, true).inputs, {bubble(bmp, "", 220)});

    EXPECT_EQ(rgbAt(after + "/output_003.png", 15, 25), (std::vector<int>{255, 255, 255}))
        << "an unanchored overlay must stay exactly where it was put";
    EXPECT_EQ(rgbAt(after + "/output_003.png", 15, 45), (std::vector<int>{30, 200, 30}))
        << "and it is now stranded on page B — the reason anchoring is the default a GUI should use";
    EXPECT_EQ(rgbAt(after + "/output_003.png", 15, 75), (std::vector<int>{200, 200, 30}))
        << "page C, where the author put the bubble, came out bare";
}

/**
 * A bubble whose page is gone is dropped and said out loud. Silently treating its page-relative Y as a
 * strip-Y would drop it near the top of the chapter, on whatever page happens to be there.
 */
TEST(OverlayAnchoringTest, ABubbleWhoseAnchorPageIsGoneIsSkippedAndReported)
{
    TempDir tmp("orphan");
    const std::string bmp = writeSolid(tmp, "bubble.png", 20, 20, k_white);

    std::vector<std::string> warnings;
    const std::string out = render(tmp, "out", Chapter::build(tmp, false).inputs,
                                   {bubble(bmp, "file-deleted", 20)}, &warnings);

    EXPECT_EQ(rgbAt(out + "/output_001.png", 15, 25), (std::vector<int>{200, 30, 30}))
        << "the orphan fell through as an absolute strip-Y and landed on page A";

    bool told = false;
    for (const auto& w : warnings)
        told = told || w.find("ovl-bubble") != std::string::npos;
    EXPECT_TRUE(told) << "dropping the user's bubble must be reported, not silent";
}

} // namespace Platemaker::Core
