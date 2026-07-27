/**
 * \file
 * \brief Unit tests for the output-profile preset catalogue and the "presets are never persisted" model.
 *
 * Presets are baked into the build and resolved from the catalogue at runtime; they are not written
 * into a workspace. These tests pin the catalogue and the membership test, the resolver that unions
 * user profiles with presets, the load-time migration (collapse a stored preset copy into a reference,
 * strip a diverged preset id), and the save-path guard that keeps a preset out of the JSON.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-26
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/id_generator/id_generator.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
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

/// A profile whose every field matches the Webtoon Standard preset, with a chosen id/name/format.
/// The default format is the preset's own (JPEG), so a caller that omits it produces a profile that
/// matches the preset outright; pass another format to model a diverged/unrelated profile.
std::string presetShapedJson(const std::string& idField,
                             const std::string& name,
                             const std::string& outputFormat = "JPEG")
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

Models::OutputProfile userProfile(std::string id, std::string name)
{
    Models::OutputProfile op;
    op.id   = std::move(id);
    op.name = std::move(name);
    return op;
}

} // namespace

// ===========================================================================
// The catalogue and the membership test
// ===========================================================================

TEST(OutputPresetTest, DefinitionIsPinnedIndependentlyOfStructDefaults)
{
    // Pin the definition independently of OutputProfile's field defaults: the preset must not silently
    // track a change to a struct default. (The format now diverges from the PNG default outright.)
    const auto preset = Models::webtoonStandardPreset();

    EXPECT_EQ(preset.id,   Models::k_webtoonStandardPresetId);
    EXPECT_EQ(preset.name, "Webtoon Standard");
    EXPECT_EQ(Models::outputProfileSignature(preset),
              "w800h1280p2f1i1;jpeg90,0,1,0;png6,0;webp80,0,4");
}

TEST(OutputPresetTest, ByIdIsTheMembershipTest)
{
    // outputProfilePresetById is the discriminator for a bare id: a preset id resolves to its
    // definition, anything else to nothing.
    const auto found = Models::outputProfilePresetById(Models::k_webtoonStandardPresetId);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(Models::outputProfileSignature(*found),
              Models::outputProfileSignature(Models::webtoonStandardPreset()));

    for (int i = 0; i < 100; ++i)
        EXPECT_FALSE(Models::outputProfilePresetById(Infrastructure::makeId("op")).has_value());

    EXPECT_FALSE(Models::outputProfilePresetById("op-Webtoon Standard").has_value());
    EXPECT_FALSE(Models::outputProfilePresetById("").has_value());

    // outputPresetDefById is the same test without constructing an OutputProfile.
    EXPECT_NE(Models::outputPresetDefById(Models::k_webtoonStandardPresetId), nullptr);
    EXPECT_EQ(Models::outputPresetDefById("op-not-a-preset"), nullptr);
}

TEST(OutputPresetTest, EveryCatalogueEntryResolvesById)
{
    const auto presets = Models::outputProfilePresets();
    ASSERT_FALSE(presets.empty());
    for (const auto& preset : presets) {
        const auto found = Models::outputProfilePresetById(preset.id);
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(Models::outputProfileSignature(*found), Models::outputProfileSignature(preset));
    }
}

// ===========================================================================
// resolveOutputProfile — user profiles ∪ presets, with provenance
// ===========================================================================

TEST(ResolveOutputProfileTest, FindsPresetsAndUserProfiles)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor(ws).replaceOutputProfiles({userProfile("op-user1", "Mine")});

    const auto preset = Models::resolveOutputProfile(ws, Models::k_webtoonStandardPresetId);
    ASSERT_TRUE(preset.has_value());
    EXPECT_EQ(preset->name, "Webtoon Standard");
    EXPECT_NE(Models::outputPresetDefById(preset->id), nullptr);   // provenance: a preset

    const auto user = Models::resolveOutputProfile(ws, "op-user1");
    ASSERT_TRUE(user.has_value());
    EXPECT_EQ(user->name, "Mine");
    EXPECT_EQ(Models::outputPresetDefById(user->id), nullptr);     // provenance: a user profile

    EXPECT_FALSE(Models::resolveOutputProfile(ws, "op-nothing").has_value());
}

// ===========================================================================
// load() — presets are not persisted
// ===========================================================================

TEST(OutputPresetLoadTest, NoPresetIsAddedToAnEmptyWorkspace)
{
    const TempWorkspace ws("empty", workspaceJson(""));

    Infrastructure::WorkspaceRepairReport report;
    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path(), report);

    EXPECT_TRUE(loaded.outputProfiles().empty());
    EXPECT_FALSE(report.any());
}

TEST(OutputPresetLoadTest, AUserProfileIsKeptAndNoPresetAppended)
{
    const TempWorkspace ws("userkept", workspaceJson(
        presetShapedJson(R"("id": "op-mine", )", "My Profile", "WebP")));

    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path());

    ASSERT_EQ(loaded.outputProfiles().size(), 1u);
    EXPECT_EQ(loaded.outputProfiles()[0].id, "op-mine");
    EXPECT_EQ(loaded.outputProfiles()[0].outputFormat, Models::OutputFormat::WebP);
}

TEST(OutputPresetLoadTest, StoredCanonicalPresetCopyIsDropped)
{
    // The real 0.2.x case: the preset was persisted under its canonical id. It is redundant now, so
    // it is dropped; a project referencing that id keeps resolving through the catalogue.
    const TempWorkspace ws("collapse", workspaceJson(
        presetShapedJson(R"("id": "op-preset-webtoon-standard", )", "Webtoon Standard"),
        projectJson("op-preset-webtoon-standard")));

    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path());

    EXPECT_TRUE(loaded.outputProfiles().empty());
    ASSERT_EQ(loaded.projectItems.size(), 1u);
    EXPECT_EQ(loaded.projectItems[0].outputProfileId, Models::k_webtoonStandardPresetId);

    const auto resolved =
        Models::resolveOutputProfile(loaded, loaded.projectItems[0].outputProfileId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_NE(Models::outputPresetDefById(loaded.projectItems[0].outputProfileId), nullptr);
}

TEST(OutputPresetLoadTest, AUserCopyOfAPresetSurvives)
{
    // A profile with an ordinary id whose settings happen to equal the preset is a copy the user
    // made (e.g. via add-output-profile --from-preset). The migration must leave it alone — only a
    // profile carrying a preset id, or the legacy op-<name> form, is collapsed.
    const TempWorkspace ws("usercopy", workspaceJson(
        presetShapedJson(R"("id": "op-mine", )", "My Webtoon"),
        projectJson("op-mine")));

    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path());

    ASSERT_EQ(loaded.outputProfiles().size(), 1u);
    EXPECT_EQ(loaded.outputProfiles()[0].id, "op-mine");
    EXPECT_EQ(loaded.projectItems[0].outputProfileId, "op-mine");
}

TEST(OutputPresetLoadTest, DivergedPresetIdProfileIsStrippedToAUserProfile)
{
    // Carries a preset id but no longer matches it (edited by an older build or by hand). It is the
    // user's, so it survives with its settings under a fresh, non-preset id, and the project follows.
    const TempWorkspace ws("diverged", workspaceJson(
        presetShapedJson(R"("id": "op-preset-webtoon-standard", )", "Webtoon Standard", "WebP"),
        projectJson("op-preset-webtoon-standard")));

    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path());

    ASSERT_EQ(loaded.outputProfiles().size(), 1u);
    EXPECT_FALSE(Models::outputProfilePresetById(loaded.outputProfiles()[0].id).has_value());
    EXPECT_EQ(loaded.outputProfiles()[0].outputFormat, Models::OutputFormat::WebP);
    EXPECT_EQ(loaded.projectItems[0].outputProfileId, loaded.outputProfiles()[0].id);
}

TEST(OutputPresetLoadTest, MigrationIsIdempotent)
{
    // A legacy name-derived id that matches the preset collapses to the canonical reference; loading,
    // saving and loading again must not keep churning ids.
    const TempWorkspace ws("idempotent", workspaceJson(
        presetShapedJson(R"("id": "op-Webtoon Standard", )", "Webtoon Standard"),
        projectJson("op-Webtoon Standard")));

    const Infrastructure::WorkspaceSerializer ser;
    const auto first = ser.load(ws.path());
    ser.save(first, ws.path());
    const auto second = ser.load(ws.path());

    EXPECT_EQ(first.outputProfiles().size(), second.outputProfiles().size());
    ASSERT_EQ(first.projectItems.size(), 1u);
    ASSERT_EQ(second.projectItems.size(), 1u);
    EXPECT_EQ(first.projectItems[0].outputProfileId, Models::k_webtoonStandardPresetId);
    EXPECT_EQ(second.projectItems[0].outputProfileId, Models::k_webtoonStandardPresetId);
}

// ===========================================================================
// The lib refuses to admit a preset into the palette — and therefore never writes one
// ===========================================================================

TEST(OutputPresetSaveTest, PresetIsRefusedByTheEditorAndNeverWritten)
{
    // The palette is private and can only be set through WorkspaceEditor.  Hand it a preset among
    // the profiles: the editor drops it on the way in (the front-line guarantee), so it is gone
    // before anything can persist it — and the serializer's own guard keeps it out regardless.
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor(ws).replaceOutputProfiles(
        {Models::webtoonStandardPreset(),          // preset id — must be dropped
         userProfile("op-user1", "Mine")});

    ASSERT_EQ(ws.outputProfiles().size(), 1u);
    EXPECT_EQ(ws.outputProfiles()[0].id, "op-user1");

    const std::string json = Infrastructure::WorkspaceSerializer{}.serialize(ws);
    EXPECT_EQ(json.find(std::string(Models::k_webtoonStandardPresetId)), std::string::npos);
    EXPECT_NE(json.find("op-user1"), std::string::npos);
}

} // namespace Platemaker
