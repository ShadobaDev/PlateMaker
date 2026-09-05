/**
 * \file
 * \brief Unit tests for implicit rendering of unmatched input pages (SPECIFICATION §7.5.1 4a/4b).
 *
 * A page whose W×H matches no canvas profile is no longer dropped: the pipeline renders it implicitly
 * (scaled to targetWidth, no margins) and flags the input. These pin that behaviour end-to-end:
 *   - NotFoundAnywhere      → the page is appended (not in skippedPages), onInput = AppendedWithoutProfile.
 *   - FoundInWorkspaceOnly  → still appended, but loud: onInput = AppendedProfileNotLinked with the
 *                             unlinked candidate ids.
 *   - no profiles at all    → every page Appended (guards the quick-start path).
 *
 * VIPS is initialised once for the whole test binary by test_scaled_strip.cpp's global environment, so
 * these tests just synthesise small PNGs on disk and run the pipeline over them.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-28
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/core/processing_callbacks/processing_callbacks.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

#include <vips/vips.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Platemaker::Core {
namespace {

namespace fs = std::filesystem;

/// A temp directory unique to one test, removed on destruction.
class TempDir {
public:
    explicit TempDir(const std::string& tag)
        : m_path(fs::temp_directory_path() /
                 ("platemaker-implicit-" + tag + "-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed())))
    {
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(m_path, ec); }

    [[nodiscard]] std::string file(const std::string& name) const
    {
        return (m_path / name).string();
    }
    [[nodiscard]] std::string dir() const { return m_path.string(); }

private:
    fs::path m_path;
};

/// Writes a synthetic black PNG of the given size and returns its absolute path.
std::string writePng(const TempDir& tmp, const std::string& name, int w, int h)
{
    const std::string path = tmp.file(name);
    VipsImage* img = nullptr;
    EXPECT_EQ(vips_black(&img, w, h, nullptr), 0)
        << "vips_black failed: " << vips_error_buffer();
    EXPECT_EQ(vips_image_write_to_file(img, path.c_str(), nullptr), 0)
        << "write failed: " << vips_error_buffer();
    g_object_unref(img);
    return path;
}

Models::CanvasProfile profile(const std::string& id, const std::string& name, int w, int h)
{
    Models::CanvasProfile cp;
    cp.id   = id;
    cp.name = name;
    cp.canvasSize = {w, h};
    cp.margins    = {0, 0, 0, 0}; // zero margins → the matched page is a plain scale, no crop needed.
    return cp;
}

/// Writes a synthetic solid-colour sRGB PNG and returns its absolute path (for grade tests, where a
/// black image would make saturation a no-op).
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

/// Reads pixel (x,y) of an image file as band values.
std::vector<double> readPixel(const std::string& path, int x, int y)
{
    VipsImage* im = vips_image_new_from_file(path.c_str(), nullptr);
    EXPECT_NE(im, nullptr) << vips_error_buffer();
    double* v = nullptr; int n = 0;
    EXPECT_EQ(vips_getpoint(im, &v, &n, x, y, nullptr), 0) << vips_error_buffer();
    std::vector<double> out(v, v + n);
    g_free(v);
    g_object_unref(im);
    return out;
}

Models::InputFile input(const std::string& path)
{
    Models::InputFile f;
    f.filePath = path;
    f.status   = Models::FileStatus::Pending;
    return f;
}

Models::OutputProfile smallOutput()
{
    Models::OutputProfile op;
    op.targetWidth  = 100;
    op.sliceHeight  = 100;
    op.outputFormat = Models::OutputFormat::PNG;
    op.startIndex   = 1;
    return op;
}

/// The four fields every test here sets; the optional steps stay at their defaults (off).
RenderRequest request(std::vector<Models::InputFile>     inputs,
                      std::vector<Models::CanvasProfile> palette,
                      std::vector<std::string>           projectIds,
                      std::string                        outputDir)
{
    RenderRequest r;
    r.inputs           = std::move(inputs);
    r.outputProfile    = smallOutput();
    r.canvasProfiles   = std::move(palette);
    r.canvasProfileIds = std::move(projectIds);
    r.outputDirectory  = std::move(outputDir);
    return r;
}

/// Collects onInput results keyed by input path.
struct InputCapture {
    std::unordered_map<std::string, InputResult> byPath;

    ProcessingCallbacks callbacks()
    {
        ProcessingCallbacks cb;
        cb.onInput = [this](const InputResult& r) { byPath[r.inputPath] = r; };
        return cb;
    }
};

bool contains(const std::vector<std::string>& v, const std::string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

// A page matching no profile anywhere is rendered implicitly, not dropped.
TEST(ImplicitRenderTest, UnmatchedPageIsAppendedNotSkipped)
{
    TempDir tmp("notfound");
    const std::string matched   = writePng(tmp, "a.png", 200, 300);
    const std::string unmatched = writePng(tmp, "b.png", 400, 500);

    const auto linked = profile("cp-linked", "Linked", 200, 300);
    const std::vector<Models::CanvasProfile> palette{linked};
    const std::vector<std::string>           projectIds{linked.id};

    InputCapture cap;
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(
        request({input(matched), input(unmatched)}, palette, projectIds, tmp.dir()),
        cancel, cap.callbacks());

    EXPECT_FALSE(outcome.failed);
    EXPECT_TRUE(outcome.skippedPages.empty()) << "the unmatched page must not be skipped";
    EXPECT_FALSE(outcome.records.empty())     << "slices should have been produced";

    // Both pages appear in appliedProfiles; the unmatched one carries an empty profile id.
    const auto findApplied = [&](const std::string& p) -> const Models::AppliedCanvasProfile* {
        for (const auto& ap : outcome.appliedProfiles)
            if (ap.sourceFilePath == p) return &ap;
        return nullptr;
    };
    ASSERT_NE(findApplied(matched), nullptr);
    ASSERT_NE(findApplied(unmatched), nullptr);
    EXPECT_EQ(findApplied(matched)->profileId, "cp-linked");
    EXPECT_TRUE(findApplied(unmatched)->profileId.empty());

    ASSERT_TRUE(cap.byPath.count(matched));
    ASSERT_TRUE(cap.byPath.count(unmatched));
    EXPECT_EQ(cap.byPath[matched].status,   InputStatus::Appended);
    EXPECT_EQ(cap.byPath[unmatched].status, InputStatus::AppendedWithoutProfile);
}

// A same-size profile that exists in the workspace but is not linked → rendered loudly (still appended).
TEST(ImplicitRenderTest, UnlinkedSameSizeProfileRendersLoudly)
{
    TempDir tmp("unlinked");
    const std::string page = writePng(tmp, "a.png", 400, 500);

    const auto linked   = profile("cp-linked",   "Linked",   200, 300);
    const auto unlinked = profile("cp-unlinked", "Unlinked", 400, 500);
    const std::vector<Models::CanvasProfile> palette{linked, unlinked};
    const std::vector<std::string>           projectIds{linked.id}; // unlinked is NOT in the project.

    InputCapture cap;
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(
        request({input(page)}, palette, projectIds, tmp.dir()), cancel, cap.callbacks());

    EXPECT_FALSE(outcome.failed);
    EXPECT_TRUE(outcome.skippedPages.empty());

    ASSERT_TRUE(cap.byPath.count(page));
    EXPECT_EQ(cap.byPath[page].status, InputStatus::AppendedProfileNotLinked);
    EXPECT_TRUE(contains(cap.byPath[page].unlinkedCandidateProfileIds, "cp-unlinked"))
        << "the loud status must carry the unlinked candidate id";
}

// A workspace with no canvas profiles renders every page normally (quick-start path).
TEST(ImplicitRenderTest, NoProfilesAppendsEveryPage)
{
    TempDir tmp("noprofiles");
    const std::string a = writePng(tmp, "a.png", 200, 300);
    const std::string b = writePng(tmp, "b.png", 400, 500);

    InputCapture cap;
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(
        request({input(a), input(b)}, /*palette*/ {}, /*projectIds*/ {}, tmp.dir()),
        cancel, cap.callbacks());

    EXPECT_FALSE(outcome.failed);
    EXPECT_TRUE(outcome.skippedPages.empty());
    ASSERT_TRUE(cap.byPath.count(a));
    ASSERT_TRUE(cap.byPath.count(b));
    EXPECT_EQ(cap.byPath[a].status, InputStatus::Appended);
    EXPECT_EQ(cap.byPath[b].status, InputStatus::Appended);
}

// A run where nothing loads (all inputs Missing) fails with a typed NoPagesLoaded error, not a string.
TEST(ProcessingErrorTest, NoPagesLoadedSetsTypedError)
{
    TempDir tmp("nopages");
    Models::InputFile missing;
    missing.filePath = tmp.file("gone.png"); // never created on disk
    missing.status   = Models::FileStatus::Missing;

    InputCapture cap;
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(
        request({missing}, /*palette*/ {}, /*projectIds*/ {}, tmp.dir()),
        cancel, cap.callbacks());

    EXPECT_TRUE(outcome.failed);
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code,     Models::ProcessingErrorCode::NoPagesLoaded);
    EXPECT_EQ(outcome.error->category, Models::ProcessingErrorCategory::Load);
    EXPECT_FALSE(outcome.error->message.empty());
}

// A single input that fails to load is a non-fatal skip carrying the typed InputLoadFailed tag.
TEST(ProcessingErrorTest, PerInputLoadFailureCarriesTypedCode)
{
    TempDir tmp("badload");
    const std::string good = writePng(tmp, "good.png", 200, 300);
    const std::string bad  = tmp.file("bad.png");
    { std::ofstream(bad, std::ios::binary) << "not a real image"; }

    InputCapture cap;
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(
        request({input(good), input(bad)}, /*palette*/ {}, /*projectIds*/ {}, tmp.dir()),
        cancel, cap.callbacks());

    EXPECT_FALSE(outcome.failed) << "one good page keeps the run alive";
    EXPECT_TRUE(contains(outcome.skippedPages, bad));
    ASSERT_TRUE(cap.byPath.count(bad));
    EXPECT_EQ(cap.byPath[bad].status,        InputStatus::SkippedError);
    EXPECT_EQ(cap.byPath[bad].errorCode,     Models::ProcessingErrorCode::InputLoadFailed);
    EXPECT_EQ(cap.byPath[bad].errorCategory, Models::ProcessingErrorCategory::Load);
}

// Colour correction with a per-page exclusion: the excluded input is left ungraded while its
// neighbour is graded — pinning the pipeline's excludedInputUids path end-to-end.
TEST(ColourCorrectionPipelineTest, ExcludedInputIsNotGraded)
{
    TempDir tmp("cc-exclude");
    // Two identical red pages (100x100 → one 100px slice each at smallOutput()).
    const std::string a = writeSolidPng(tmp, "a.png", 100, 100, 220, 30, 30);
    const std::string b = writeSolidPng(tmp, "b.png", 100, 100, 220, 30, 30);

    Models::InputFile fa = input(a); fa.uid = "in-a";
    Models::InputFile fb = input(b); fb.uid = "in-b";

    Models::ColourCorrection cc;
    cc.enabled           = true;
    cc.saturation        = 0.0;   // desaturate to grey — unless the page is excluded
    cc.excludedInputUids = {"in-a"};

    Infrastructure::CancellationToken cancel;
    RenderRequest req = request({fa, fb}, /*palette*/ {}, /*projectIds*/ {}, tmp.dir());
    req.colourCorrection = cc;
    const auto outcome = ProcessingPipeline::render(req, cancel);

    ASSERT_FALSE(outcome.failed);
    ASSERT_EQ(outcome.records.size(), 2u);

    // slice 0 = page A (excluded) → keeps its colour; slice 1 = page B (graded) → grey.
    const auto pa = readPixel(tmp.file("output_001.png"), 50, 50);
    const auto pb = readPixel(tmp.file("output_002.png"), 50, 50);
    ASSERT_GE(pa.size(), 3u);
    ASSERT_GE(pb.size(), 3u);

    EXPECT_GT(pa[0], pa[1] + 100.0) << "excluded page must keep its colour (red >> green)";
    EXPECT_NEAR(pb[0], pb[1], 2.0) << "graded page must be desaturated (grey)";
    EXPECT_NEAR(pb[1], pb[2], 2.0);
    EXPECT_LT(pb[0], 120.0)        << "graded page must be the luminance grey of red";
}

} // namespace Platemaker::Core
