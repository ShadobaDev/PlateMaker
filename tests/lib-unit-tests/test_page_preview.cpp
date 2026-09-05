/**
 * \file
 * \brief Unit tests for the page-domain API (layoutPagesFromHeaders / decodePageToRgba).
 *
 * These exist to pin ONE property: **the preview cannot disagree with the render**. A consumer builds
 * its strip from layoutPagesFromHeaders()'s heights and paints decodePageToRgba()'s pixels; if either drifts
 * what run() would produce, the drift is silent — both still "work", the numbers just stop agreeing,
 * and every page below the first disagreement sits at the wrong strip offset. So the assertions here
 * compare the preview against a *real render* of the same inputs rather than against constants.
 *
 * VIPS is initialised once for the whole test binary by test_scaled_strip.cpp's global environment.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-03
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

#include <vips/vips.h>

#include <cstdint>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

namespace Platemaker::Core {
namespace {

namespace fs = std::filesystem;

/// A temp directory unique to one test, removed on destruction.
class TempDir {
public:
    explicit TempDir(const std::string& tag)
        : m_path(fs::temp_directory_path() /
                 ("platemaker-preview-" + tag + "-" +
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

/// Writes a solid-colour sRGB PNG (3-band, no alpha) and returns its absolute path.
std::string writeSolidPng(const TempDir& tmp, const std::string& name, int w, int h,
                          std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    const std::string path = tmp.file(name);
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 3);
    for (std::size_t i = 0; i < px.size(); i += 3) { px[i] = r; px[i + 1] = g; px[i + 2] = b; }
    VipsImage* raw = vips_image_new_from_memory_copy(px.data(), px.size(), w, h, 3, VIPS_FORMAT_UCHAR);
    EXPECT_NE(raw, nullptr) << vips_error_buffer();
    VipsImage* srgb = nullptr;
    EXPECT_EQ(vips_copy(raw, &srgb, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr), 0)
        << vips_error_buffer();
    g_object_unref(raw);
    EXPECT_EQ(vips_image_write_to_file(srgb, path.c_str(), nullptr), 0) << vips_error_buffer();
    g_object_unref(srgb);
    return path;
}

Models::InputFile input(const std::string& path, const std::string& uid = {})
{
    Models::InputFile f;
    f.filePath = path;
    f.uid      = uid;
    f.status   = Models::FileStatus::Pending;
    return f;
}

Models::CanvasProfile profile(const std::string& id, const std::string& name,
                              int w, int h, Models::Margins margins)
{
    Models::CanvasProfile cp;
    cp.id         = id;
    cp.name       = name;
    cp.canvasSize = {w, h};
    cp.margins    = margins;
    return cp;
}

Models::OutputProfile output(int targetWidth, int sliceHeight)
{
    Models::OutputProfile op;
    op.targetWidth  = targetWidth;
    op.sliceHeight  = sliceHeight;
    op.outputFormat = Models::OutputFormat::PNG;
    op.startIndex   = 1;
    return op;
}

/// Reads an image file's dimensions from its header.
std::pair<int, int> sizeOf(const std::string& path)
{
    VipsImage* im = vips_image_new_from_file(path.c_str(), nullptr);
    EXPECT_NE(im, nullptr) << vips_error_buffer();
    const std::pair<int, int> wh{im->Xsize, im->Ysize};
    g_object_unref(im);
    return wh;
}

} // namespace

// ---------------------------------------------------------------------------
// layoutPagesFromHeaders
// ---------------------------------------------------------------------------

/**
 * The anti-drift test. A margin-cropped page and an uncropped one, both scaled: the sum of the
 * layout's heights must equal the strip the render actually builds, which is observable as the total
 * height of the slices it saved. If page geometry ever diverges between the two paths, this fails.
 */
TEST(PagePreviewTest, LayoutHeightsMatchTheRenderedStrip)
{
    TempDir tmp("layout");
    // 200x400 with 20px margins -> safe area 160x360 -> scaled to width 80 -> 80x180.
    const std::string cropped = writeSolidPng(tmp, "a.png", 200, 400, 200, 100, 50);
    // 160x320, no profile of that size -> rendered implicitly -> scaled to width 80 -> 80x160.
    const std::string plain   = writeSolidPng(tmp, "b.png", 160, 320, 50, 100, 200);

    const auto cp = profile("cp-1", "Margined", 200, 400, {20, 20, 20, 20});
    const std::vector<Models::CanvasProfile> palette{cp};
    const std::vector<std::string>           ids{cp.id};
    const auto op = output(/*targetWidth=*/80, /*sliceHeight=*/20);

    const std::vector<Models::InputFile> inputs{input(cropped), input(plain)};

    const auto layout = ProcessingPipeline::layoutPagesFromHeaders(inputs, op, palette, ids);
    ASSERT_EQ(layout.size(), 2u);

    EXPECT_TRUE(layout[0].readable);
    EXPECT_EQ(layout[0].canvasProfileId, cp.id);
    EXPECT_EQ(layout[0].status, InputStatus::Appended);
    EXPECT_EQ(layout[0].sourceWidth, 200);
    EXPECT_EQ(layout[0].sourceHeight, 400);

    EXPECT_TRUE(layout[1].readable);
    EXPECT_TRUE(layout[1].canvasProfileId.empty());
    EXPECT_EQ(layout[1].status, InputStatus::AppendedWithoutProfile);

    // Every page is scaled to the profile's target width.
    for (const auto& g : layout) EXPECT_EQ(g.width, op.targetWidth);

    // Now render for real and compare against the strip the pipeline actually built.
    Infrastructure::CancellationToken cancel;
    RenderRequest req;
    req.inputs           = inputs;
    req.outputProfile    = op;
    req.canvasProfiles   = palette;
    req.canvasProfileIds = ids;
    req.outputDirectory  = tmp.dir();
    const auto outcome = ProcessingPipeline::render(req, cancel);
    ASSERT_FALSE(outcome.failed) << (outcome.error ? outcome.error->message : "");

    int renderedHeight = 0;
    for (const auto& rec : outcome.records)
        renderedHeight += sizeOf((fs::path(tmp.dir()) / rec.fileName).string()).second;

    const int layoutHeight = std::accumulate(
        layout.begin(), layout.end(), 0,
        [](int acc, const PagePreviewGeometry& g) { return acc + g.height; });

    EXPECT_EQ(layoutHeight, renderedHeight)
        << "preview layout and the rendered strip disagree on the strip's height";
}

/// A page the render would skip must not fail the whole layout — it comes back unusable instead.
TEST(PagePreviewTest, LayoutFlagsPagesTheRenderWouldSkip)
{
    TempDir tmp("skip");
    const std::string good = writeSolidPng(tmp, "good.png", 100, 200, 10, 20, 30);

    Models::InputFile missing = input(tmp.file("gone.png"));
    missing.status = Models::FileStatus::Missing;

    // Present in the list, but not on disk at all.
    const Models::InputFile broken = input(tmp.file("nosuchfile.png"));

    const auto op = output(50, 25);
    const auto layout = ProcessingPipeline::layoutPagesFromHeaders(
        {input(good), missing, broken}, op, {}, {});

    ASSERT_EQ(layout.size(), 3u);
    EXPECT_TRUE(layout[0].readable);
    EXPECT_GT(layout[0].height, 0);
    EXPECT_FALSE(layout[1].readable);
    EXPECT_EQ(layout[1].height, 0);
    EXPECT_FALSE(layout[2].readable);
    EXPECT_EQ(layout[2].height, 0);
}

// ---------------------------------------------------------------------------
// decodePageToRgba
// ---------------------------------------------------------------------------

/// The preview's pixels are the page the render appends: same crop, same scale, same colour.
TEST(PagePreviewTest, PageRgbaMatchesTheRenderedPage)
{
    TempDir tmp("pixels");
    // 200x400 with 20px margins -> 160x360 -> width 80 -> 80x180. Slice height 180 makes the render's
    // single output slice exactly that scaled page, so the two are directly comparable.
    const std::string page = writeSolidPng(tmp, "a.png", 200, 400, 200, 100, 50);

    const auto cp = profile("cp-1", "Margined", 200, 400, {20, 20, 20, 20});
    const std::vector<Models::CanvasProfile> palette{cp};
    const std::vector<std::string>           ids{cp.id};

    const auto layout = ProcessingPipeline::layoutPagesFromHeaders({input(page)}, output(80, 180), palette, ids);
    ASSERT_EQ(layout.size(), 1u);
    ASSERT_TRUE(layout[0].readable);
    const int w = layout[0].width;
    const int h = layout[0].height;

    std::vector<unsigned char> rgba(static_cast<std::size_t>(w) * h * 4, 0);
    ProcessingPipeline::decodePageToRgba(input(page), output(80, 180), palette, ids,
                                        rgba.data(), w, h);

    // A source without alpha comes back fully opaque, and the colour survives the round trip.
    EXPECT_EQ(rgba[0], 200);
    EXPECT_EQ(rgba[1], 100);
    EXPECT_EQ(rgba[2], 50);
    for (std::size_t i = 3; i < rgba.size(); i += 4)
        ASSERT_EQ(rgba[i], 255) << "alpha must be opaque at byte " << i;

    // The render of the same page produces one slice of exactly these dimensions.
    Infrastructure::CancellationToken cancel;
    RenderRequest req;
    req.inputs           = {input(page)};
    req.outputProfile    = output(80, 180);
    req.canvasProfiles   = palette;
    req.canvasProfileIds = ids;
    req.outputDirectory  = tmp.dir();
    const auto outcome = ProcessingPipeline::render(req, cancel);
    ASSERT_FALSE(outcome.failed) << (outcome.error ? outcome.error->message : "");
    ASSERT_EQ(outcome.records.size(), 1u);
    const auto rendered = sizeOf((fs::path(tmp.dir()) / outcome.records[0].fileName).string());
    EXPECT_EQ(rendered.first,  w);
    EXPECT_EQ(rendered.second, h);
}

/// The preview is the ungraded baseline the consumer grades itself — a render of the same page with
/// the grade on must therefore differ from it. This is the property the whole design rests on: the
/// viewer can re-grade resident pixels on every slider move without ever re-reading the file.
TEST(PagePreviewTest, PageRgbaIsTheUngradedBaselineTheRenderGrades)
{
    TempDir tmp("ungraded");
    const std::string page = writeSolidPng(tmp, "a.png", 100, 200, 120, 120, 120);

    const auto op     = output(50, 100);
    const auto layout = ProcessingPipeline::layoutPagesFromHeaders({input(page)}, op, {}, {});
    ASSERT_EQ(layout.size(), 1u);
    const int w = layout[0].width, h = layout[0].height;

    std::vector<unsigned char> rgba(static_cast<std::size_t>(w) * h * 4, 0);
    ProcessingPipeline::decodePageToRgba(input(page), op, {}, {}, rgba.data(), w, h);

    EXPECT_EQ(rgba[0], 120) << "decodePageToRgba must return the page ungraded";
    EXPECT_EQ(rgba[1], 120);
    EXPECT_EQ(rgba[2], 120);

    // The same page rendered WITH the grade lands somewhere else entirely.
    Models::ColourCorrection cc;
    cc.enabled    = true;
    cc.brightness = 60.0;

    Infrastructure::CancellationToken cancel;
    RenderRequest req;
    req.inputs           = {input(page)};
    req.outputProfile    = op;
    req.outputDirectory  = tmp.dir();
    req.colourCorrection = cc;
    const auto outcome = ProcessingPipeline::render(req, cancel);
    ASSERT_FALSE(outcome.failed) << (outcome.error ? outcome.error->message : "");
    ASSERT_FALSE(outcome.records.empty());

    const std::string out = (fs::path(tmp.dir()) / outcome.records[0].fileName).string();
    VipsImage* im = vips_image_new_from_file(out.c_str(), nullptr);
    ASSERT_NE(im, nullptr) << vips_error_buffer();
    double* v = nullptr; int n = 0;
    ASSERT_EQ(vips_getpoint(im, &v, &n, 0, 0, nullptr), 0) << vips_error_buffer();
    const double rendered = v[0];
    g_free(v);
    g_object_unref(im);

    EXPECT_GT(rendered, 120.0) << "the render must bake the grade the preview leaves out";
}

/// Bad arguments fail loudly rather than writing a plausible-looking wrong image.
TEST(PagePreviewTest, PageRgbaRejectsBadArgs)
{
    TempDir tmp("badargs");
    const std::string page = writeSolidPng(tmp, "a.png", 100, 200, 10, 20, 30);
    const auto op = output(50, 100);

    const auto layout = ProcessingPipeline::layoutPagesFromHeaders({input(page)}, op, {}, {});
    ASSERT_EQ(layout.size(), 1u);
    const int w = layout[0].width, h = layout[0].height;
    std::vector<unsigned char> rgba(static_cast<std::size_t>(w) * h * 4, 0);

    EXPECT_THROW(ProcessingPipeline::decodePageToRgba(input(page), op, {}, {}, nullptr, w, h),
                 std::runtime_error);
    EXPECT_THROW(ProcessingPipeline::decodePageToRgba(input(page), op, {}, {}, rgba.data(), 0, h),
                 std::runtime_error);
    // A stale layout — the caller's dimensions no longer match the page.
    EXPECT_THROW(ProcessingPipeline::decodePageToRgba(input(page), op, {}, {}, rgba.data(), w, h + 1),
                 std::runtime_error);
    // An unreadable page.
    EXPECT_THROW(ProcessingPipeline::decodePageToRgba(input(tmp.file("nope.png")), op, {}, {},
                                                     rgba.data(), w, h),
                 std::runtime_error);
}

} // namespace Platemaker::Core
