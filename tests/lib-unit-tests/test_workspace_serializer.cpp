/**
 * \file
 * \brief Unit tests for WorkspaceSerializer — JSON round-trip, defaults, migration.
 *
 * These tests exercise the serialiser in isolation using in-memory JSON
 * strings and temporary files (via std::filesystem temp_directory_path).
 * No real image files are needed — the serialiser deals only with metadata.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-02
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

#include <utility>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Platemaker::Infrastructure {

namespace {

/// Build a minimal valid Workspace suitable for round-trip tests.
Models::Workspace makeMinimalWorkspace()
{
    Models::CanvasProfile canvas;
    canvas.id           = "cp-test-001";
    canvas.name         = "Webtoon Standard";
    canvas.canvasSize   = {1600, 10240};
    canvas.margins      = {100, 100, 100, 100};
    canvas.visualColour = {255, 105, 180, 128}; // hot pink @ 50 % alpha

    // A genuine user profile — deliberately *not* preset-shaped (WebP, not the preset's PNG), so it
    // is not collapsed into a catalogue reference on load.
    Models::OutputProfile output;
    output.id              = "op-test-001";
    output.name            = "Webtoon Export";
    output.targetWidth     = 800;
    output.sliceHeight     = 1280;
    output.lastSlicePolicy = Models::LastSlicePolicy::KeepAsIs;
    output.outputFormat    = Models::OutputFormat::WebP;
    output.startIndex      = 1;

    Models::Workspace ws;
    ws.version         = 2;
    ws.outputDirectory = "/tmp/out";
    ws.stripDirty      = true;
    WorkspaceEditor ed(ws);
    ed.replaceCanvasProfiles({canvas}); // keeps the supplied ids (only mints when absent)
    ed.replaceOutputProfiles({output});
    return ws;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Round-trip: save → load → compare
// ---------------------------------------------------------------------------

TEST(WorkspaceSerializerTest, RoundTripPreservesVersion)
{
    const WorkspaceSerializer ser;
    const Models::Workspace   original = makeMinimalWorkspace();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_roundtrip.platemaker.json";

    ASSERT_NO_THROW(ser.save(original, tmp.string()));

    Models::Workspace loaded;
    ASSERT_NO_THROW(loaded = ser.load(tmp.string()));

    EXPECT_EQ(loaded.version, original.version);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, RoundTripPreservesCanvasProfile)
{
    const WorkspaceSerializer ser;
    const Models::Workspace   original = makeMinimalWorkspace();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_canvas.platemaker.json";

    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    ASSERT_EQ(loaded.canvasProfiles().size(), 1u);
    EXPECT_EQ(loaded.canvasProfiles()[0].name,              original.canvasProfiles()[0].name);
    EXPECT_EQ(loaded.canvasProfiles()[0].canvasSize.width,  original.canvasProfiles()[0].canvasSize.width);
    EXPECT_EQ(loaded.canvasProfiles()[0].canvasSize.height, original.canvasProfiles()[0].canvasSize.height);
    EXPECT_EQ(loaded.canvasProfiles()[0].margins.top,       original.canvasProfiles()[0].margins.top);
    EXPECT_EQ(loaded.canvasProfiles()[0].visualColour.r,    original.canvasProfiles()[0].visualColour.r);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, RoundTripPreservesOutputProfile)
{
    const WorkspaceSerializer ser;
    const Models::Workspace   original = makeMinimalWorkspace();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_output.platemaker.json";

    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    // A genuine user profile round-trips unchanged, and load() no longer appends a preset —
    // presets live in the catalogue, not in the workspace.
    ASSERT_EQ(loaded.outputProfiles().size(), 1u);
    EXPECT_EQ(loaded.outputProfiles()[0].id,           original.outputProfiles()[0].id);
    EXPECT_EQ(loaded.outputProfiles()[0].targetWidth,  original.outputProfiles()[0].targetWidth);
    EXPECT_EQ(loaded.outputProfiles()[0].sliceHeight,  original.outputProfiles()[0].sliceHeight);
    EXPECT_EQ(loaded.outputProfiles()[0].outputFormat, original.outputProfiles()[0].outputFormat);
    EXPECT_EQ(loaded.outputProfiles()[0].startIndex,   original.outputProfiles()[0].startIndex);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, RoundTripPreservesInputDimensions)
{
    // The per-input display W×H recorded at render time must survive save/load — it is what lets
    // detectCanvasConfigChange() re-match each page offline instead of blanket-invalidating a project.
    const WorkspaceSerializer ser;
    Models::Workspace         original = makeMinimalWorkspace();

    Models::ProjectItem proj;
    proj.name = "Chapter";
    proj.uid  = "proj-dim-001";
    Models::InputFile inf;
    inf.uid      = "file-001";
    inf.filePath = "page_000.png";
    inf.width    = 1080;
    inf.height   = 1920;
    proj.getInputImages().push_back(std::move(inf));
    original.projectItems.push_back(std::move(proj));

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_dims.platemaker.json";
    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    ASSERT_EQ(loaded.projectItems[0].getInputImages().size(), 1u);
    EXPECT_EQ(loaded.projectItems[0].getInputImages()[0].width,  1080);
    EXPECT_EQ(loaded.projectItems[0].getInputImages()[0].height, 1920);

    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

TEST(WorkspaceSerializerTest, LoadMissingFileThrowsRuntimeError)
{
    const WorkspaceSerializer ser;
    EXPECT_THROW(
        (void)ser.load("/nonexistent/path/workspace.platemaker.json"),
        std::runtime_error
    );
}

TEST(WorkspaceSerializerTest, LoadMalformedJsonThrowsRuntimeError)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_malformed.json";

    { std::ofstream f(tmp); f << "{ this is not valid json @@@ }"; }

    const WorkspaceSerializer ser;
    EXPECT_THROW((void)ser.load(tmp.string()), std::runtime_error);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, LoadMissingVersionFieldThrowsRuntimeError)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_noversion.json";

    { std::ofstream f(tmp); f << R"({"canvasProfiles":[],"outputProfiles":[]})"; }

    const WorkspaceSerializer ser;
    EXPECT_THROW((void)ser.load(tmp.string()), std::runtime_error);

    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// CanvasProfile::id round-trip and back-compat
// ---------------------------------------------------------------------------

TEST(WorkspaceSerializerTest, RoundTripPreservesCanvasProfileId)
{
    const WorkspaceSerializer ser;
    const Models::Workspace   original = makeMinimalWorkspace();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_cp_id.platemaker.json";

    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    ASSERT_EQ(loaded.canvasProfiles().size(), 1u);
    EXPECT_EQ(loaded.canvasProfiles()[0].id, "cp-test-001");

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, BackCompatLoadWithoutIdGetsAMintedId)
{
    // Workspace JSON that predates the 'id' field on CanvasProfile.
    //
    // Such a profile used to have its id *derived from its name* ("cp-" + name).  That was
    // a second identity scheme and it was not unique either — two profiles sharing a name
    // shared an id.  Since 0.2.1 the serializer mints a random unique id instead and
    // relinks the legacy references (covered by test_profile_ids.cpp); all this test cares
    // about is that the profile comes back usable, with *some* id that is not the old
    // name-derived one.
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_backcompat_id.json";

    {
        std::ofstream f(tmp);
        f << R"({
            "version": 1,
            "canvasProfiles": [{
                "name": "Webtoon Standard",
                "canvasSize": {"width": 1600, "height": 10240},
                "margins": {"top": 0, "right": 0, "bottom": 0, "left": 0}
            }],
            "outputProfiles": [],
            "activeCanvasProfileName": "Webtoon Standard",
            "activeOutputProfileName": "",
            "outputDirectory": ""
        })";
    }

    const WorkspaceSerializer ser;
    Models::Workspace loaded;
    ASSERT_NO_THROW(loaded = ser.load(tmp.string()));

    ASSERT_EQ(loaded.canvasProfiles().size(), 1u);
    EXPECT_FALSE(loaded.canvasProfiles()[0].id.empty());
    EXPECT_NE(loaded.canvasProfiles()[0].id, "cp-Webtoon Standard");
    EXPECT_EQ(loaded.canvasProfiles()[0].id.rfind("cp-", 0), 0u); // keeps the readable prefix

    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Optional processing steps (colour correction + strip overlays)
// ---------------------------------------------------------------------------

TEST(WorkspaceSerializerTest, RoundTripPreservesProcessingSteps)
{
    // Colour-correction params and strip overlays are per-project config; they must survive save/load
    // so a graded / annotated chapter reopens exactly as configured.
    const WorkspaceSerializer ser;
    Models::Workspace         original = makeMinimalWorkspace();

    Models::ProjectItem proj;
    proj.name = "Chapter";
    proj.uid  = "proj-proc-001";
    proj.colourCorrection.brightness        = 0.1;
    proj.colourCorrection.contrast          = 1.2;
    proj.colourCorrection.saturation        = 0.8;
    proj.colourCorrection.curves.master     = {{0.0, 0.0}, {0.5, 0.8}, {1.0, 1.0}};
    proj.colourCorrection.curves.red          = {{0.0, 0.0}, {1.0, 0.5}};
    proj.colourCorrection.excludedInputUids = {"file-001", "file-009"};
    Models::StripOverlay ovl;
    ovl.uid            = "ovl-1";
    ovl.assetPath      = "/tmp/bubble.png";
    ovl.sha256         = "deadbeef";
    ovl.anchorInputUid = "file-004";   // page-anchored: y is relative to that page's top
    ovl.xFrac          = 0.05;
    ovl.yFrac          = 1.875;
    ovl.enabled        = true;
    ovl.blend          = Models::BlendMode::Multiply;
    proj.getStripOverlays().push_back(std::move(ovl));
    original.projectItems.push_back(std::move(proj));

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_processing.platemaker.json";
    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    const auto& p = loaded.projectItems.front();
    EXPECT_FALSE(Models::isNeutral(p.colourCorrection));
    EXPECT_DOUBLE_EQ(p.colourCorrection.brightness, 0.1);
    EXPECT_DOUBLE_EQ(p.colourCorrection.contrast,   1.2);
    EXPECT_DOUBLE_EQ(p.colourCorrection.saturation, 0.8);
    ASSERT_EQ(p.colourCorrection.curves.master.size(), 3u);
    EXPECT_DOUBLE_EQ(p.colourCorrection.curves.master[1].x, 0.5);
    EXPECT_DOUBLE_EQ(p.colourCorrection.curves.master[1].y, 0.8);
    ASSERT_EQ(p.colourCorrection.curves.red.size(), 2u);
    EXPECT_DOUBLE_EQ(p.colourCorrection.curves.red[1].y, 0.5);
    EXPECT_TRUE(p.colourCorrection.curves.green.empty());
    EXPECT_EQ(p.colourCorrection.excludedInputUids,
              (std::vector<std::string>{"file-001", "file-009"}));
    ASSERT_EQ(p.getStripOverlays().size(), 1u);
    EXPECT_EQ(p.getStripOverlays()[0].uid,        "ovl-1");
    EXPECT_EQ(p.getStripOverlays()[0].assetPath, "/tmp/bubble.png");
    EXPECT_EQ(p.getStripOverlays()[0].sha256,     "deadbeef");
    EXPECT_EQ(p.getStripOverlays()[0].anchorInputUid, "file-004")
        << "the anchor must survive the round-trip — without it the overlay reloads as an absolute "
           "strip-Y and lands on whatever artwork now happens to sit there";
    EXPECT_DOUBLE_EQ(p.getStripOverlays()[0].xFrac, 0.05);
    EXPECT_DOUBLE_EQ(p.getStripOverlays()[0].yFrac, 1.875);
    EXPECT_TRUE(p.getStripOverlays()[0].enabled);
    EXPECT_EQ(p.getStripOverlays()[0].blend, Models::BlendMode::Multiply);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, LegacyProjectLoadsProcessingDefaults)
{
    // A project written before optional processing steps existed — no colourCorrection / stripOverlays
    // keys.  It must load with the step disabled and no overlays, so the render is unchanged.
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_processing_legacy.platemaker.json";

    {
        std::ofstream f(tmp);
        f << R"({
            "version": 2,
            "canvasProfiles": [],
            "outputProfiles": [],
            "outputDirectory": "",
            "projectItems": [{
                "name": "Legacy",
                "uid": "proj-legacy",
                "inputFiles": [],
                "outputFiles": [],
                "outputDirectory": ""
            }]
        })";
    }

    const WorkspaceSerializer ser;
    Models::Workspace loaded;
    ASSERT_NO_THROW(loaded = ser.load(tmp.string()));

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    const auto& p = loaded.projectItems.front();
    EXPECT_TRUE(Models::isNeutral(p.colourCorrection));
    EXPECT_TRUE(p.colourCorrection.excludedInputUids.empty());
    EXPECT_TRUE(p.getStripOverlays().empty());

    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Paths relative to the workspace file — a folder that is moved, renamed or zipped still opens
// ---------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

/// A scratch root of its own per test, with a non-ASCII name: the paths travel as UTF-8 end to end.
struct RelScratch {
    fs::path root;
    explicit RelScratch(const char* name)
        : root(fs::temp_directory_path() / utf8ToPath(std::string("pm_relpaths_ż_") + name))
    {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~RelScratch() { std::error_code ec; fs::remove_all(root, ec); }
};

/// The form paths are compared in: UTF-8, normalised, '/' separators — what consumers write.
std::string g(const fs::path& p)
{
    const std::u8string s = p.lexically_normal().generic_u8string();
    return std::string(s.begin(), s.end());
}

void touchFile(const fs::path& p)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

std::string readText(const fs::path& p)
{
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::size_t occurrences(const std::string& text, const std::string& what)
{
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size()))
        ++n;
    return n;
}

/// A workspace with one project holding the given overlays (by asset path).
Models::Workspace withOverlays(const std::vector<std::string>& assets)
{
    Models::Workspace   ws = makeMinimalWorkspace();
    Models::ProjectItem proj;
    proj.name = "Chapter";
    proj.uid  = "proj-rel-001";
    int n = 0;
    for (const auto& a : assets) {
        Models::StripOverlay o;
        o.uid       = "ovl-" + std::to_string(++n);
        o.assetPath = a;
        proj.getStripOverlays().push_back(std::move(o));
    }
    ws.projectItems.push_back(std::move(proj));
    return ws;
}

} // anonymous namespace

TEST(WorkspaceSerializerRelativePaths, OnlyAnAssetInsideTheFolderGetsARelativeCopy)
{
    const RelScratch s("inside");
    const fs::path   inside  = s.root / "ws" / "overlays" / "a.svg";
    const fs::path   outside = s.root / "elsewhere" / "b.svg";
    touchFile(inside);
    touchFile(outside);
    const fs::path file = s.root / "ws" / "c.platemaker.json";

    const WorkspaceSerializer ser;
    const Models::Workspace   ws = withOverlays({g(inside), g(outside)});
    ser.save(ws, pathToUtf8(file));

    const std::string text = readText(file);
    EXPECT_EQ(occurrences(text, "\"assetPathRelative\""), 1u);
    EXPECT_NE(text.find("\"assetPathRelative\": \"overlays/a.svg\""), std::string::npos);
    // serialize() has no location, so it carries no relative copy — and change detection, which compares
    // two serialize() results, is the same wherever the workspace is saved.
    EXPECT_EQ(ser.serialize(ws).find("assetPathRelative"), std::string::npos);

    // In place, nothing changes: both paths come back exactly as written.
    const auto loaded = ser.load(pathToUtf8(file));
    ASSERT_EQ(loaded.projectItems.at(0).getStripOverlays().size(), 2u);
    EXPECT_EQ(loaded.projectItems[0].getStripOverlays()[0].assetPath, g(inside));
    EXPECT_EQ(loaded.projectItems[0].getStripOverlays()[1].assetPath, g(outside));
}

TEST(WorkspaceSerializerRelativePaths, AMovedFolderStillFindsItsOverlays)
{
    const RelScratch s("moved");
    const fs::path   asset = s.root / "before" / "overlays" / "a.svg";
    touchFile(asset);

    const WorkspaceSerializer ser;
    ser.save(withOverlays({g(asset)}), pathToUtf8(s.root / "before" / "c.platemaker.json"));

    fs::rename(s.root / "before", s.root / "after");   // moved, renamed, or zipped and unpacked elsewhere

    const auto loaded = ser.load(pathToUtf8(s.root / "after" / "c.platemaker.json"));
    EXPECT_EQ(loaded.projectItems.at(0).getStripOverlays().at(0).assetPath,
              g(s.root / "after" / "overlays" / "a.svg"));
}

TEST(WorkspaceSerializerRelativePaths, ARelativeCopyThatFindsNothingLeavesTheAbsolutePath)
{
    // The workspace file copied on its own, without its folder: the relative copy names nothing, so the
    // absolute path — still valid on this machine — is the one that stands. It is also what a reader from
    // before the key would use.
    const RelScratch s("copied");
    const fs::path   asset = s.root / "ws" / "overlays" / "a.svg";
    touchFile(asset);

    const WorkspaceSerializer ser;
    ser.save(withOverlays({g(asset)}), pathToUtf8(s.root / "ws" / "c.platemaker.json"));
    fs::create_directories(s.root / "copy");
    fs::copy_file(s.root / "ws" / "c.platemaker.json", s.root / "copy" / "c.platemaker.json");

    const auto loaded = ser.load(pathToUtf8(s.root / "copy" / "c.platemaker.json"));
    EXPECT_EQ(loaded.projectItems.at(0).getStripOverlays().at(0).assetPath, g(asset));
}

TEST(WorkspaceSerializerRelativePaths, PathsWithNoRootAreReadAgainstTheFolder)
{
    // What a hand-written or packaged file carries. The output directory is the exception: a CLI user's
    // relative --output is resolved against the working directory when rendering, and stays that way.
    const RelScratch s("rootless");

    Models::Workspace ws = withOverlays({"overlays/x.svg"});
    Models::InputFile page;
    page.uid      = "file-001";
    page.filePath = "pages/001.png";
    ws.projectItems[0].getInputImages().push_back(page);
    ws.projectItems[0].getOutputDirectory() = "out";

    const WorkspaceSerializer ser;
    const fs::path            file = s.root / "c.platemaker.json";
    ser.save(ws, pathToUtf8(file));
    const auto loaded = ser.load(pathToUtf8(file));

    const auto& p = loaded.projectItems.at(0);
    EXPECT_EQ(p.getStripOverlays().at(0).assetPath, g(s.root / "overlays" / "x.svg"));
    EXPECT_EQ(p.getInputImages().at(0).filePath, g(s.root / "pages" / "001.png"));
    EXPECT_EQ(p.getOutputDirectory(), "out");
}

} // namespace Platemaker::Infrastructure
