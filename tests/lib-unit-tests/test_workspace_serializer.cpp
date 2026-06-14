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

#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>

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

    Models::OutputProfile output;
    output.name            = "Webtoon Export";
    output.targetWidth     = 800;
    output.sliceHeight     = 1280;
    output.lastSlicePolicy = Models::LastSlicePolicy::KeepAsIs;
    output.outputFormat    = Models::OutputFormat::PNG;
    output.startIndex      = 1;

    Models::Workspace ws;
    ws.version                 = 1;
    ws.canvasProfiles          = {canvas};
    ws.outputProfiles          = {output};
    ws.activeCanvasProfileName = canvas.name;
    ws.activeOutputProfileName = output.name;
    ws.outputDirectory         = "/tmp/out";
    ws.stripDirty              = true;
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

    ASSERT_EQ(loaded.canvasProfiles.size(), 1u);
    EXPECT_EQ(loaded.canvasProfiles[0].name,              original.canvasProfiles[0].name);
    EXPECT_EQ(loaded.canvasProfiles[0].canvasSize.width,  original.canvasProfiles[0].canvasSize.width);
    EXPECT_EQ(loaded.canvasProfiles[0].canvasSize.height, original.canvasProfiles[0].canvasSize.height);
    EXPECT_EQ(loaded.canvasProfiles[0].margins.top,       original.canvasProfiles[0].margins.top);
    EXPECT_EQ(loaded.canvasProfiles[0].visualColour.r,    original.canvasProfiles[0].visualColour.r);

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

    ASSERT_EQ(loaded.outputProfiles.size(), 1u);
    EXPECT_EQ(loaded.outputProfiles[0].targetWidth,  original.outputProfiles[0].targetWidth);
    EXPECT_EQ(loaded.outputProfiles[0].sliceHeight,  original.outputProfiles[0].sliceHeight);
    EXPECT_EQ(loaded.outputProfiles[0].outputFormat, original.outputProfiles[0].outputFormat);
    EXPECT_EQ(loaded.outputProfiles[0].startIndex,   original.outputProfiles[0].startIndex);

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

    ASSERT_EQ(loaded.canvasProfiles.size(), 1u);
    EXPECT_EQ(loaded.canvasProfiles[0].id, "cp-test-001");

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, BackCompatLoadWithoutIdDerivedFromName)
{
    // Workspace JSON that predates the 'id' field on CanvasProfile.
    // The serializer should derive 'id' from 'name' so the object is usable.
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

    ASSERT_EQ(loaded.canvasProfiles.size(), 1u);
    // Back-compat: id derived as "cp-" + name
    EXPECT_EQ(loaded.canvasProfiles[0].id, "cp-Webtoon Standard");

    std::filesystem::remove(tmp);
}

} // namespace Platemaker::Infrastructure
