/**
 * \file
 * \brief Unit tests for the output-profile preset catalogue and the invariants load() keeps.
 *
 * A preset identifier is shared by every workspace, which is what lets a preset stay
 * recognisable across files and app updates. That only holds while the id cannot come to
 * mean something else, so load() adopts, forks and guarantees presence. These tests pin
 * all three, plus the preset definition itself.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-19
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/id_generator/id_generator.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/workspace.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace Platemaker {

namespace {

class TempWorkspace {
public:
    explicit TempWorkspace(const std::string& tag, const std::string& json)
        : m_path(std::filesystem::temp_directory_path() / ("pm_test_preset_" + tag + ".json"))
    {
        std::ofstream f(m_path);
        f << json;
    }

    ~TempWorkspace() { std::error_code ec; std::filesystem::remove(m_path, ec); }

    TempWorkspace(const TempWorkspace&)            = delete;
    TempWorkspace& operator=(const TempWorkspace&) = delete;

    [[nodiscard]] std::string path() const { return m_path.string(); }

private:
    std::filesystem::path m_path;
};

std::string workspaceJson(const std::string& outputProfiles,
                          const std::string& projectItems = "")
{
    return R"({"version": 2, "outputDirectory": "", "stripDirty": false,
        "canvasProfiles": [],
        "outputProfiles": [)" + outputProfiles + R"(],
        "projectItems": [)"  + projectItems  + R"(]})";
}

std::string projectJson(const std::string& outputProfileId)
{
    return R"({
        "uuid": "proj-1", "name": "Chapter 01",
        "inputDirectory": "", "outputDirectory": "",
        "inputFiles": [], "outputFiles": [],
        "outputSignature": "", "canvasProfileIdsAtRender": [],
        "canvasProfileIds": [],
        "outputProfileId": ")" + outputProfileId + R"("
    })";
}

/// A profile whose every field matches the Webtoon Standard preset, with a chosen id/name.
std::string presetShapedJson(const std::string& idField,
                             const std::string& name,
                             const std::string& outputFormat = "PNG")
{
    return R"({)" + idField + R"("name": ")" + name + R"(",
        "targetWidth": 800, "sliceHeight": 1280,
        "lastSlicePolicy": "KeepAsIs", "outputFormat": ")" + outputFormat + R"(",
        "jpegOptions": {"quality": 90, "optimize": true,
                        "progressive": false, "subsampling": "YUV_444"},
        "pngOptions": {"compression": 6, "interlaced": false},
        "webpOptions": {"quality": 80, "lossless": false, "effort": 4},
        "startIndex": 1
    })";
}

//! Index of the profile carrying the canonical preset id, or -1.
int presetIndex(const Models::Workspace& ws)
{
    for (std::size_t i = 0; i < ws.outputProfiles.size(); ++i)
        if (ws.outputProfiles[i].id == Models::k_webtoonStandardPresetId)
            return static_cast<int>(i);
    return -1;
}

} // namespace

// ===========================================================================
// The catalogue
// ===========================================================================

TEST(OutputPresetTest, DefinitionIsPinnedIndependentlyOfStructDefaults)
{
    // The preset currently happens to coincide with OutputProfile's field defaults. That
    // coincidence is the hazard: changing a default would silently redefine the preset and
    // desynchronise it from every workspace already on disk. This pins the definition, so
    // such a change fails here instead of in a user's file.
    const auto preset = Models::webtoonStandardPreset();

    EXPECT_EQ(preset.id,   Models::k_webtoonStandardPresetId);
    EXPECT_EQ(preset.name, "Webtoon Standard");
    EXPECT_EQ(Models::outputProfileSignature(preset),
              "w800h1280p2f0i1;jpeg90,0,1,0;png6,0;webp80,0,4");
}

TEST(OutputPresetTest, PresetIdsAreRecognisedAndGeneratedOnesAreNot)
{
    EXPECT_TRUE(Models::isOutputProfilePresetId(Models::k_webtoonStandardPresetId));

    // Generated ids are hex after the prefix, so they can never land in the reserved
    // namespace — the two id spaces are disjoint by construction.
    for (int i = 0; i < 100; ++i)
        EXPECT_FALSE(Models::isOutputProfilePresetId(Infrastructure::makeId("op")));

    EXPECT_FALSE(Models::isOutputProfilePresetId("op-Webtoon Standard"));
    EXPECT_FALSE(Models::isOutputProfilePresetId(""));
}

TEST(OutputPresetTest, LookupTableAndByIdAgree)
{
    const auto presets = Models::outputProfilePresets();
    ASSERT_FALSE(presets.empty());

    for (const auto& preset : presets) {
        const auto found = Models::outputProfilePresetById(preset.id);
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(Models::outputProfileSignature(*found),
                  Models::outputProfileSignature(preset));
    }

    EXPECT_FALSE(Models::outputProfilePresetById("op-not-a-preset").has_value());
}

// ===========================================================================
// load() — presence
// ===========================================================================

TEST(OutputPresetLoadTest, MissingPresetIsAddedSilently)
{
    const TempWorkspace ws("absent", workspaceJson(""));

    Infrastructure::WorkspaceRepairReport report;
    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path(), report);

    ASSERT_EQ(loaded.outputProfiles.size(), 1u);
    EXPECT_EQ(loaded.outputProfiles[0].id, Models::k_webtoonStandardPresetId);

    // Bookkeeping, not a collision — the user has no decision to make about it.
    EXPECT_FALSE(report.any());
}

TEST(OutputPresetLoadTest, PresetIsAppendedSoTheDefaultProfileDoesNotChange)
{
    // resolveOutputProfile() falls back to outputProfiles.front() for an unassigned project,
    // so inserting the preset at the front would quietly change what existing workspaces
    // render with.
    const TempWorkspace ws("append", workspaceJson(
        presetShapedJson(R"("id": "op-mine", )", "My Profile", "WebP")));

    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path());

    ASSERT_EQ(loaded.outputProfiles.size(), 2u);
    EXPECT_EQ(loaded.outputProfiles[0].id, "op-mine");
    EXPECT_EQ(loaded.outputProfiles[1].id, Models::k_webtoonStandardPresetId);
}

TEST(OutputPresetLoadTest, PresentPresetIsNotDuplicated)
{
    const TempWorkspace ws("present", workspaceJson(
        presetShapedJson(R"("id": "op-preset-webtoon-standard", )", "Webtoon Standard")));

    Infrastructure::WorkspaceRepairReport report;
    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path(), report);

    ASSERT_EQ(loaded.outputProfiles.size(), 1u);
    EXPECT_EQ(loaded.outputProfiles[0].id, Models::k_webtoonStandardPresetId);
    EXPECT_FALSE(report.any());
}

// ===========================================================================
// load() — fork of a diverged preset
// ===========================================================================

TEST(OutputPresetLoadTest, DivergedPresetIsForkedAndTheRealPresetReturns)
{
    // Edited by an older build, or by hand in the JSON: it carries the preset id but no
    // longer produces what the preset produces.
    const TempWorkspace ws("diverged", workspaceJson(
        presetShapedJson(R"("id": "op-preset-webtoon-standard", )", "Webtoon Standard", "WebP"),
        projectJson("op-preset-webtoon-standard")));

    Infrastructure::WorkspaceRepairReport report;
    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path(), report);

    // The user's version survives with its settings intact, under an id of its own...
    ASSERT_EQ(loaded.outputProfiles.size(), 2u);
    EXPECT_FALSE(Models::isOutputProfilePresetId(loaded.outputProfiles[0].id));
    EXPECT_EQ(loaded.outputProfiles[0].outputFormat, Models::OutputFormat::WebP);

    // ...the project follows it, because that is the profile it was actually using...
    EXPECT_EQ(loaded.projectItems[0].outputProfileId, loaded.outputProfiles[0].id);

    // ...and the fork freed the canonical id, so the genuine preset comes back beside it.
    const int idx = presetIndex(loaded);
    ASSERT_NE(idx, -1);
    EXPECT_EQ(loaded.outputProfiles[static_cast<std::size_t>(idx)].outputFormat,
              Models::OutputFormat::PNG);

    EXPECT_FALSE(report.any());
}

TEST(OutputPresetLoadTest, RepairIsIdempotent)
{
    // Loading, saving and loading again must not keep churning ids — otherwise every open
    // would look like a change and the GUI would keep offering to save.
    const TempWorkspace ws("idempotent", workspaceJson(
        presetShapedJson(R"("id": "op-Webtoon Standard", )", "Webtoon Standard"),
        projectJson("op-Webtoon Standard")));

    const Infrastructure::WorkspaceSerializer ser;
    const auto first = ser.load(ws.path());
    ser.save(first, ws.path());
    const auto second = ser.load(ws.path());

    ASSERT_EQ(first.outputProfiles.size(), second.outputProfiles.size());
    for (std::size_t i = 0; i < first.outputProfiles.size(); ++i)
        EXPECT_EQ(first.outputProfiles[i].id, second.outputProfiles[i].id);

    EXPECT_EQ(first.projectItems[0].outputProfileId,
              second.projectItems[0].outputProfileId);
}

} // namespace Platemaker
