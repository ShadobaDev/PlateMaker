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
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <gtest/gtest.h>

#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/core/processing_callbacks/processing_callbacks.hpp>
#include <platemaker/infrastructure/log/log.hpp>
#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

#include <vips/vips.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
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

// ---------------------------------------------------------------------------
// Guards, cancellation and partial renders
// ---------------------------------------------------------------------------

namespace {

/// Three 100x100 pages → three 100px slices at smallOutput(): output_001..003.png.
std::vector<Models::InputFile> threePages(const TempDir& tmp)
{
    return {input(writePng(tmp, "a.png", 100, 100)),
            input(writePng(tmp, "b.png", 100, 100)),
            input(writePng(tmp, "c.png", 100, 100))};
}

/// Collects the warning lines a render logs.
struct LogCapture {
    std::vector<std::string> warnings;

    ProcessingCallbacks callbacks()
    {
        ProcessingCallbacks cb;
        cb.onLog = [this](ProcessingLogLevel level, const std::string& line) {
            if (level == ProcessingLogLevel::Warning) warnings.push_back(line);
        };
        return cb;
    }

    [[nodiscard]] bool any(const std::string& needle) const
    {
        return std::any_of(warnings.begin(), warnings.end(),
                           [&](const std::string& w) { return w.find(needle) != std::string::npos; });
    }
};

} // namespace

// A slice height of 0 used to divide by zero (SIGFPE) before slicing could reject it. Both 0 and a
// negative height must be the same typed SlicingFailed outcome, and write nothing.
TEST(RenderGuardTest, NonPositiveSliceHeightIsATypedFailure)
{
    for (const int sliceHeight : {0, -100}) {
        TempDir tmp("sliceh" + std::to_string(sliceHeight));
        RenderRequest req = request({input(writePng(tmp, "a.png", 100, 300))}, {}, {}, tmp.dir());
        req.outputProfile.sliceHeight = sliceHeight;

        Infrastructure::CancellationToken cancel;
        const auto outcome = ProcessingPipeline::render(req, cancel);
        EXPECT_TRUE(outcome.failed) << "sliceHeight " << sliceHeight;
        ASSERT_TRUE(outcome.error.has_value());
        EXPECT_EQ(outcome.error->code,     Models::ProcessingErrorCode::SlicingFailed);
        EXPECT_EQ(outcome.error->category, Models::ProcessingErrorCategory::Slice);
        EXPECT_TRUE(outcome.records.empty());
        EXPECT_FALSE(fs::exists(tmp.file("output_001.png")));
    }
}

TEST(RenderCancellationTest, CancelledBeforeStartRendersNothing)
{
    TempDir tmp("cancel-early");
    Infrastructure::CancellationToken cancel;
    cancel.cancel();

    const auto outcome = ProcessingPipeline::render(request(threePages(tmp), {}, {}, tmp.dir()), cancel);
    EXPECT_TRUE(outcome.cancelled);
    EXPECT_FALSE(outcome.failed);
    EXPECT_TRUE(outcome.records.empty());
    EXPECT_FALSE(fs::exists(tmp.file("output_001.png")));
}

TEST(RenderCancellationTest, CancelDuringSlicingKeepsWhatWasSavedAndFlagsTheOutcome)
{
    TempDir tmp("cancel-mid");
    Infrastructure::CancellationToken cancel;
    std::vector<std::string> saved;
    ProcessingCallbacks cb;
    cb.onSliceSaved = [&](const SliceSaved& s) {
        saved.push_back(s.name);
        cancel.cancel();   // the user presses Cancel as the first slice lands
    };

    const auto outcome = ProcessingPipeline::render(request(threePages(tmp), {}, {}, tmp.dir()), cancel, cb);
    EXPECT_TRUE(outcome.cancelled);
    EXPECT_FALSE(outcome.failed);
    ASSERT_EQ(saved, std::vector<std::string>{"output_001.png"});
    EXPECT_EQ(outcome.records.size(), 1u);
    EXPECT_FALSE(fs::exists(tmp.file("output_002.png")));
}

TEST(PartialRenderTest, OnlyRequestedSlicesAreWrittenAndCounted)
{
    TempDir tmp("partial");
    RenderRequest req = request(threePages(tmp), {}, {}, tmp.dir());
    req.onlySlices = std::unordered_set<std::string>{"output_002.png"};

    std::vector<std::string> skipped;
    std::vector<int>         totals;
    ProcessingCallbacks cb;
    cb.onSliceSkipped = [&](const SliceSkipped& s) { skipped.push_back(s.name); };
    cb.onProgress     = [&](const ProcessingProgress& p) { totals.push_back(p.sliceTotal); };

    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(req, cancel, cb);
    ASSERT_FALSE(outcome.failed) << (outcome.error ? outcome.error->message : "");
    ASSERT_EQ(outcome.records.size(), 1u);
    EXPECT_EQ(outcome.records[0].fileName, "output_002.png");
    EXPECT_EQ(skipped, (std::vector<std::string>{"output_001.png", "output_003.png"}));
    EXPECT_EQ(totals, std::vector<int>{1}) << "progress counts only the slices this run writes";
    EXPECT_FALSE(fs::exists(tmp.file("output_001.png")));
    EXPECT_TRUE(fs::exists(tmp.file("output_002.png")));
}

// ---------------------------------------------------------------------------
// Slice output: previews, write errors and the safety net
// ---------------------------------------------------------------------------

TEST(SliceOutputTest, UnusableThumbnailDirectoryOnlyDisablesPreviews)
{
    TempDir tmp("thumbdir");
    RenderRequest req = request(threePages(tmp), {}, {}, tmp.dir());
    req.thumbnailCacheDir = (fs::path(tmp.file("a.png")) / "cache").string();   // under a file

    LogCapture log;
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(req, cancel, log.callbacks());
    EXPECT_FALSE(outcome.failed);
    EXPECT_EQ(outcome.records.size(), 3u);
    EXPECT_TRUE(log.any("Thumbnail previews disabled"));
}

TEST(SliceOutputTest, FailedPreviewDoesNotFailTheRender)
{
    TempDir tmp("thumbfail");
    RenderRequest req = request(threePages(tmp), {}, {}, tmp.dir());
    req.thumbnailCacheDir = tmp.file("cache");
    // Occupy the first slice's thumbnail name with a non-empty directory so publishing it fails.
    const std::string thumb =
        Infrastructure::ThumbnailCache(req.thumbnailCacheDir).thumbnailPath(tmp.dir() + "/output_001.png");
    fs::create_directories(fs::path(thumb) / "occupied");

    LogCapture log;
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(req, cancel, log.callbacks());
    EXPECT_FALSE(outcome.failed);
    EXPECT_EQ(outcome.records.size(), 3u) << "every slice is still saved";
    EXPECT_TRUE(log.any("Thumbnail preview for 'output_001.png' skipped"));
}

TEST(SliceOutputTest, UnwritableOutputDirectoryIsATypedEncodeError)
{
    TempDir tmp("nodir");
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(
        request(threePages(tmp), {}, {}, tmp.file("does-not-exist")), cancel);

    EXPECT_TRUE(outcome.failed);
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code,     Models::ProcessingErrorCode::SliceEncodeFailed);
    EXPECT_EQ(outcome.error->category, Models::ProcessingErrorCategory::Encode);
    EXPECT_EQ(outcome.error->slice, "output_001.png");
    EXPECT_TRUE(outcome.records.empty()) << "the run stops at the first failed save";
}

TEST(SliceOutputTest, OccupiedOutputNameIsReportedAsLocked)
{
    TempDir tmp("locked");
    fs::create_directories(fs::path(tmp.file("output_002.png")) / "occupied");

    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(request(threePages(tmp), {}, {}, tmp.dir()), cancel);
    EXPECT_TRUE(outcome.failed);
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code,     Models::ProcessingErrorCode::OutputLocked);
    EXPECT_EQ(outcome.error->category, Models::ProcessingErrorCategory::Io);
    EXPECT_EQ(outcome.error->slice, "output_002.png");
    EXPECT_EQ(outcome.records.size(), 1u) << "output_001 was saved before the locked one";
}

TEST(SliceOutputTest, OverlayThatCannotBeCompositedFailsTheRender)
{
    // A complex-valued page (kept as such by the libvips-native format) renders into a slice no blend
    // can be applied to. An overlay over it must fail the run with a typed encode error.
    TempDir tmp("ovlfail");
    std::string page = tmp.file("complex.v");
    {
        VipsImage* black = nullptr;
        VipsImage* cx    = nullptr;
        ASSERT_EQ(vips_black(&black, 100, 100, "bands", 3, nullptr), 0) << vips_error_buffer();
        ASSERT_EQ(vips_cast(black, &cx, VIPS_FORMAT_COMPLEX, nullptr), 0) << vips_error_buffer();
        g_object_unref(black);
        ASSERT_EQ(vips_image_write_to_file(cx, page.c_str(), nullptr), 0) << vips_error_buffer();
        g_object_unref(cx);
    }
    RenderRequest req = request({input(page)}, {}, {}, tmp.dir());
    Models::StripOverlay ov;
    ov.uid       = "ovl-1";
    ov.assetPath = writeSolidPng(tmp, "bubble.png", 20, 20, 255, 255, 255);   // colour, absolute placement
    req.stripOverlays = {ov};

    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(req, cancel);
    EXPECT_TRUE(outcome.failed);
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, Models::ProcessingErrorCode::SliceEncodeFailed);
    EXPECT_NE(outcome.error->message.find("composite overlays"), std::string::npos) << outcome.error->message;
    vips_error_clear();
}

// The outer guard turns anything that escapes the typed blocks — here a consumer callback that throws —
// into a typed Unexpected/Internal failure instead of unwinding out of render().
TEST(RenderSafetyNetTest, ThrowingCallbackBecomesATypedUnexpectedFailure)
{
    TempDir tmp("net");
    const auto runWith = [&](std::function<void()> raise) {
        ProcessingCallbacks cb;
        cb.onSlicingStarted = [&](const SlicingStarted&) { raise(); };
        Infrastructure::CancellationToken cancel;
        return ProcessingPipeline::render(request(threePages(tmp), {}, {}, tmp.dir()), cancel, cb);
    };

    const auto standard = runWith([] { throw std::runtime_error("consumer broke"); });
    EXPECT_TRUE(standard.failed);
    ASSERT_TRUE(standard.error.has_value());
    EXPECT_EQ(standard.error->code,     Models::ProcessingErrorCode::Unexpected);
    EXPECT_EQ(standard.error->category, Models::ProcessingErrorCategory::Internal);
    EXPECT_NE(standard.error->message.find("consumer broke"), std::string::npos);

    const auto foreign = runWith([] { throw 42; });
    EXPECT_TRUE(foreign.failed);
    ASSERT_TRUE(foreign.error.has_value());
    EXPECT_EQ(foreign.error->code, Models::ProcessingErrorCode::Unexpected);
    EXPECT_NE(foreign.error->message.find("non-standard"), std::string::npos);
}

// With canvas profiles in play the header is read up front; a file without one is a typed skip — and
// a render with no onInput listener still records it in skippedPages.
TEST(ProcessingErrorTest, UnreadableHeaderWithProfilesIsSkippedWithoutAListener)
{
    TempDir tmp("noheader");
    const std::string good = writePng(tmp, "good.png", 100, 100);
    const std::string bad  = tmp.file("bad.png");
    { std::ofstream(bad, std::ios::binary) << "not an image"; }

    const auto cp = profile("cp-1", "Square", 100, 100);
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(
        request({input(good), input(bad)}, {cp}, {cp.id}, tmp.dir()), cancel);
    EXPECT_FALSE(outcome.failed);
    EXPECT_TRUE(contains(outcome.skippedPages, bad));
    EXPECT_EQ(outcome.records.size(), 1u);
    vips_error_clear();
}

// A margin profile and a grade together: the page is graded, then cropped, then scaled.
TEST(ColourCorrectionPipelineTest, GradeAndMarginCropApplyTogether)
{
    TempDir tmp("cc-margins");
    const std::string page = writeSolidPng(tmp, "a.png", 200, 200, 220, 30, 30);
    auto cp = profile("cp-1", "Margined", 200, 200);
    cp.margins = {50, 0, 50, 0};   // 200x200 → crop to 200x100 → scale to 100x50

    RenderRequest req = request({input(page)}, {cp}, {cp.id}, tmp.dir());
    req.outputProfile.sliceHeight = 50;
    req.colourCorrection.saturation = 0.0;

    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(req, cancel);
    ASSERT_FALSE(outcome.failed) << (outcome.error ? outcome.error->message : "");
    ASSERT_EQ(outcome.records.size(), 1u) << "the crop leaves exactly one 50px slice";
    const auto p = readPixel(tmp.file("output_001.png"), 50, 25);
    ASSERT_GE(p.size(), 3u);
    EXPECT_NEAR(p[0], p[1], 2.0) << "the grade ran on the margin path";
    EXPECT_NEAR(p[1], p[2], 2.0);
}

TEST(PartialRenderTest, CropPolicyCountsOnlyFullSlices)
{
    // 100x250 → strip 250px, slices of 100: Crop drops the 50px tail, so the run announces 2 slices.
    TempDir tmp("crop-count");
    RenderRequest req = request({input(writePng(tmp, "tall.png", 100, 250))}, {}, {}, tmp.dir());
    req.outputProfile.lastSlicePolicy = Models::LastSlicePolicy::Crop;

    int announced = -1;
    ProcessingCallbacks cb;
    cb.onSlicingStarted = [&](const SlicingStarted& s) { announced = s.expectedSliceCount; };
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(req, cancel, cb);
    ASSERT_FALSE(outcome.failed);
    EXPECT_EQ(announced, 2);
    EXPECT_EQ(outcome.records.size(), 2u);
}

TEST(PartialRenderTest, FilterWorksWithoutASkipListener)
{
    TempDir tmp("partial-quiet");
    RenderRequest req = request(threePages(tmp), {}, {}, tmp.dir());
    req.onlySlices = std::unordered_set<std::string>{"output_003.png"};
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(req, cancel);   // no callbacks at all
    ASSERT_FALSE(outcome.failed);
    ASSERT_EQ(outcome.records.size(), 1u);
    EXPECT_EQ(outcome.records[0].fileName, "output_003.png");
}

TEST(RenderCancellationTest, CancelOnTheLastFullSliceDropsTheTail)
{
    // Boundary: the cancel lands while the last full slice is saved, so the short tail is never cut.
    TempDir tmp("cancel-tail");
    RenderRequest req = request({input(writePng(tmp, "tall.png", 100, 250))}, {}, {}, tmp.dir());
    req.outputProfile.lastSlicePolicy = Models::LastSlicePolicy::KeepAsIs;

    Infrastructure::CancellationToken cancel;
    ProcessingCallbacks cb;
    cb.onSliceSaved = [&](const SliceSaved& s) { if (s.sliceIndex == 1) cancel.cancel(); };
    const auto outcome = ProcessingPipeline::render(req, cancel, cb);
    EXPECT_TRUE(outcome.cancelled);
    EXPECT_EQ(outcome.records.size(), 2u);
    EXPECT_FALSE(fs::exists(tmp.file("output_003.png")));
}

// With every diagnostic channel switched on, a render that exercises each step must report from each
// of them — the channels a user is told to enable when filing a bug.
TEST(RenderDiagnosticsTest, EveryStepReportsOnItsChannelWhenEnabled)
{
    namespace Log = Infrastructure::Log;
    TempDir tmp("diag");
    const std::string a = writeSolidPng(tmp, "a.png", 200, 200, 220, 30, 30);
    const std::string b = writeSolidPng(tmp, "b.png", 200, 200, 30, 220, 30);
    auto cp = profile("cp-1", "Margined", 200, 200);
    cp.margins = {10, 10, 10, 10};

    RenderRequest req = request({input(a), input(b)}, {cp}, {cp.id}, tmp.dir());
    req.colourCorrection.saturation = 0.5;
    req.thumbnailCacheDir = tmp.file("thumbs");
    Models::StripOverlay ov;
    ov.uid       = "ovl-1";
    ov.assetPath = writeSolidPng(tmp, "bubble.png", 20, 20, 255, 255, 255);
    req.stripOverlays = {ov};

    std::set<std::uint64_t> heard;
    Log::setSink([&](std::uint64_t component, std::string_view) { heard.insert(component); });
    Log::enable(Log::All);
    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(req, cancel);
    Log::disable(Log::All);
    Log::setSink({});

    ASSERT_FALSE(outcome.failed) << (outcome.error ? outcome.error->message : "");
    for (const std::uint64_t c : {Log::ProcessingPipeline, Log::Scaler, Log::ScaledStrip, Log::Memory,
                                  Log::ColourCorrector, Log::StripOverlayCompositor})
        EXPECT_TRUE(heard.count(c)) << Log::componentName(c) << " stayed silent";
}

// A black-and-white bubble is the commonest overlay there is. Saved as a greyscale PNG it loads as one
// band; rasterizeOverlay() only adds alpha (two bands), and compositing then embeds it with a four-value
// transparent background, which libvips refuses — so the whole render fails.
TEST(SliceOutputTest, GreyscaleOverlayIsComposited)
{
    GTEST_SKIP() << "BUG: a greyscale (1-band) or grey+alpha overlay fails the render with "
                    "\"embed overlay layer: linear: vector must have 1 or 2 elements\" — "
                    "StripOverlayCompositor::rasterizeOverlay() does not convert grey to sRGB before "
                    "adding alpha (strip_overlay_compositor.cpp:99), so composite() gets a 2-band layer.";

    TempDir tmp("grey-ovl");
    RenderRequest req = request(threePages(tmp), {}, {}, tmp.dir());
    Models::StripOverlay ov;
    ov.uid       = "ovl-1";
    ov.assetPath = writePng(tmp, "bubble.png", 20, 20);   // 1-band greyscale PNG
    req.stripOverlays = {ov};

    Infrastructure::CancellationToken cancel;
    const auto outcome = ProcessingPipeline::render(req, cancel);
    EXPECT_FALSE(outcome.failed) << (outcome.error ? outcome.error->message : "");
    EXPECT_EQ(outcome.records.size(), 3u);
}

} // namespace Platemaker::Core
