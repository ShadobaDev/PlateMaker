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

#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

#include <utility>

#include <filesystem>
#include <fstream>
#include <string>

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
    proj.colourCorrection.enabled           = true;
    proj.colourCorrection.iccToSRGB         = false;
    proj.colourCorrection.brightness        = 0.1;
    proj.colourCorrection.contrast          = 1.2;
    proj.colourCorrection.saturation        = 0.8;
    proj.colourCorrection.curves.master     = {{0.0, 0.0}, {0.5, 0.8}, {1.0, 1.0}};
    proj.colourCorrection.curves.r          = {{0.0, 0.0}, {1.0, 0.5}};
    proj.colourCorrection.excludedInputUids = {"file-001", "file-009"};
    proj.stripOverlays.push_back(
        Models::StripOverlay{"ovl-1", "/tmp/bubble.png", "deadbeef", 40, 1500, true});
    original.projectItems.push_back(std::move(proj));

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_processing.platemaker.json";
    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    const auto& p = loaded.projectItems.front();
    EXPECT_TRUE(p.colourCorrection.enabled);
    EXPECT_FALSE(p.colourCorrection.iccToSRGB);
    EXPECT_DOUBLE_EQ(p.colourCorrection.brightness, 0.1);
    EXPECT_DOUBLE_EQ(p.colourCorrection.contrast,   1.2);
    EXPECT_DOUBLE_EQ(p.colourCorrection.saturation, 0.8);
    ASSERT_EQ(p.colourCorrection.curves.master.size(), 3u);
    EXPECT_DOUBLE_EQ(p.colourCorrection.curves.master[1].x, 0.5);
    EXPECT_DOUBLE_EQ(p.colourCorrection.curves.master[1].y, 0.8);
    ASSERT_EQ(p.colourCorrection.curves.r.size(), 2u);
    EXPECT_DOUBLE_EQ(p.colourCorrection.curves.r[1].y, 0.5);
    EXPECT_TRUE(p.colourCorrection.curves.g.empty());
    EXPECT_EQ(p.colourCorrection.excludedInputUids,
              (std::vector<std::string>{"file-001", "file-009"}));
    ASSERT_EQ(p.stripOverlays.size(), 1u);
    EXPECT_EQ(p.stripOverlays[0].uid,        "ovl-1");
    EXPECT_EQ(p.stripOverlays[0].bitmapPath, "/tmp/bubble.png");
    EXPECT_EQ(p.stripOverlays[0].sha256,     "deadbeef");
    EXPECT_EQ(p.stripOverlays[0].x, 40);
    EXPECT_EQ(p.stripOverlays[0].y, 1500);
    EXPECT_TRUE(p.stripOverlays[0].enabled);

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
    EXPECT_FALSE(p.colourCorrection.enabled);
    EXPECT_TRUE(p.colourCorrection.iccToSRGB); // struct default preserved when the key is absent
    EXPECT_TRUE(p.colourCorrection.excludedInputUids.empty());
    EXPECT_TRUE(p.stripOverlays.empty());

    std::filesystem::remove(tmp);
}

} // namespace Platemaker::Infrastructure
