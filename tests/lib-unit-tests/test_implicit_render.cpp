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
    const auto outcome = ProcessingPipeline::run(
        {input(matched), input(unmatched)}, smallOutput(), palette, projectIds,
        tmp.dir(), cancel, cap.callbacks());

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
    const auto outcome = ProcessingPipeline::run(
        {input(page)}, smallOutput(), palette, projectIds,
        tmp.dir(), cancel, cap.callbacks());

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
    const auto outcome = ProcessingPipeline::run(
        {input(a), input(b)}, smallOutput(), /*palette*/ {}, /*projectIds*/ {},
        tmp.dir(), cancel, cap.callbacks());

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
    const auto outcome = ProcessingPipeline::run(
        {missing}, smallOutput(), /*palette*/ {}, /*projectIds*/ {},
        tmp.dir(), cancel, cap.callbacks());

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
    const auto outcome = ProcessingPipeline::run(
        {input(good), input(bad)}, smallOutput(), /*palette*/ {}, /*projectIds*/ {},
        tmp.dir(), cancel, cap.callbacks());

    EXPECT_FALSE(outcome.failed) << "one good page keeps the run alive";
    EXPECT_TRUE(contains(outcome.skippedPages, bad));
    ASSERT_TRUE(cap.byPath.count(bad));
    EXPECT_EQ(cap.byPath[bad].status,        InputStatus::SkippedError);
    EXPECT_EQ(cap.byPath[bad].errorCode,     Models::ProcessingErrorCode::InputLoadFailed);
    EXPECT_EQ(cap.byPath[bad].errorCategory, Models::ProcessingErrorCategory::Load);
}

} // namespace Platemaker::Core
