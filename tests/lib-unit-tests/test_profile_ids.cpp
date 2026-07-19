/**
 * \file
 * \brief Unit tests for identifier generation and the load-time identifier repair.
 *
 * Covers the bug these were written for: profile ids used to be a millisecond timestamp,
 * so several profiles minted inside one loop came out identical.  A workspace saved that
 * way is ambiguous — every lookup resolves to the first profile holding the id, and the
 * other one silently drops out of the "assign a profile" list.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-19
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/infrastructure/id_generator/id_generator.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/workspace.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace Platemaker {

namespace {

/// Writes \p json to a uniquely named temp file and returns its path.
class TempWorkspace {
public:
    explicit TempWorkspace(const std::string& tag, const std::string& json)
        : m_path(std::filesystem::temp_directory_path() / ("pm_test_ids_" + tag + ".json"))
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

/**
 * \brief A project item in the real on-disk shape.
 *
 * Field names and value formats are taken from an actual workspace file rather than
 * invented: \c inputFiles / \c outputFiles (not "images"), and every one of these keys is
 * required by ProjectItem's from_json.
 */
std::string projectJson(const std::string& canvasIds, const std::string& outputProfileId)
{
    return R"({
        "uuid": "proj-1", "name": "Chapter 01",
        "inputDirectory": "", "outputDirectory": "",
        "inputFiles": [], "outputFiles": [],
        "outputSignature": "", "canvasProfileIdsAtRender": [],
        "canvasProfileIds": )" + canvasIds + R"(,
        "outputProfileId": ")" + outputProfileId + R"("
    })";
}

/// An output profile in the real on-disk shape — note the enums serialise as strings.
std::string outputProfileJson(const std::string& idField, const std::string& name)
{
    return R"({)" + idField + R"("name": ")" + name + R"(",
        "targetWidth": 800, "sliceHeight": 1280,
        "lastSlicePolicy": "KeepAsIs", "outputFormat": "PNG",
        "jpegOptions": {"quality": 90, "optimize": true,
                        "progressive": false, "subsampling": "YUV_444"},
        "pngOptions": {"compression": 6, "interlaced": false},
        "webpOptions": {"quality": 80, "lossless": false, "effort": 4},
        "startIndex": 1
    })";
}

std::string canvasProfileJson(const std::string& idField,
                              const std::string& name,
                              int                height)
{
    return R"({)" + idField + R"("name": ")" + name + R"(",
        "canvasSize": {"width": 1600, "height": )" + std::to_string(height) + R"(},
        "margins": {"top": 0, "right": 0, "bottom": 0, "left": 0}
    })";
}

std::string workspaceJson(const std::string& canvasProfiles,
                          const std::string& outputProfiles,
                          const std::string& projectItems)
{
    return R"({"version": 2, "outputDirectory": "", "stripDirty": false,
        "canvasProfiles": [)" + canvasProfiles + R"(],
        "outputProfiles": [)" + outputProfiles + R"(],
        "projectItems": [)"  + projectItems  + R"(]})";
}

/// The shape of the real bug: two canvas profiles of different sizes sharing one id.
std::string duplicateCanvasIdJson()
{
    return workspaceJson(
        canvasProfileJson(R"("id": "cp-collide", )", "Webtoon-4s", 10240) + "," +
            canvasProfileJson(R"("id": "cp-collide", )", "Webtoon-2s", 5120),
        "",
        projectJson(R"(["cp-collide"])", ""));
}

} // namespace

// ===========================================================================
// makeId / makeUniqueId
// ===========================================================================

TEST(MakeIdTest, TightLoopNeverRepeats)
{
    // The direct regression: the old generator produced one id per millisecond, so a loop
    // like this returned the *same* string many times over.
    constexpr int k_draws = 10000;

    std::set<std::string> seen;
    for (int i = 0; i < k_draws; ++i)
        seen.insert(Infrastructure::makeId("cp"));

    EXPECT_EQ(seen.size(), static_cast<std::size_t>(k_draws));
}

TEST(MakeIdTest, KeepsThePrefixAndHasFullWidth)
{
    const std::string id = Infrastructure::makeId("cp");
    EXPECT_EQ(id.rfind("cp-", 0), 0u);
    EXPECT_EQ(id.size(), 3u + 32u); // "cp-" + 128 bits as hex
}

TEST(MakeUniqueIdTest, SkipsIdentifiersAlreadyTaken)
{
    // Feed it a "taken" set built from ids it just produced: every result must still be new.
    std::vector<std::string> taken;
    for (int i = 0; i < 64; ++i)
        taken.push_back(Infrastructure::makeId("proj"));

    for (int i = 0; i < 64; ++i) {
        const std::string fresh = Infrastructure::makeUniqueId("proj", taken);
        EXPECT_EQ(std::find(taken.begin(), taken.end(), fresh), taken.end());
        taken.push_back(fresh);
    }
}

TEST(MakeUniqueIdTest, CanvasAndOutputHelpersAvoidExistingProfiles)
{
    std::vector<Models::CanvasProfile> canvases(3);
    canvases[0].id = Infrastructure::makeId("cp");
    canvases[1].id = Infrastructure::makeId("cp");
    canvases[2].id = Infrastructure::makeId("cp");

    const std::string freshCanvas = Infrastructure::makeUniqueCanvasProfileId(canvases);
    for (const auto& cp : canvases)
        EXPECT_NE(freshCanvas, cp.id);

    std::vector<Models::OutputProfile> outputs(2);
    outputs[0].id = Infrastructure::makeId("op");
    outputs[1].id = Infrastructure::makeId("op");

    const std::string freshOutput = Infrastructure::makeUniqueOutputProfileId(outputs);
    for (const auto& op : outputs)
        EXPECT_NE(freshOutput, op.id);
}

// ===========================================================================
// load() — duplicate identifier repair
// ===========================================================================

TEST(WorkspaceIdRepairTest, FirstProfileKeepsTheIdAndTheDuplicateGetsANewOne)
{
    const TempWorkspace ws("dup_canvas", duplicateCanvasIdJson());

    Infrastructure::WorkspaceRepairReport report;
    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path(), report);

    ASSERT_EQ(loaded.canvasProfiles.size(), 2u);

    // First one keeps it, so every reference that already existed still resolves.
    EXPECT_EQ(loaded.canvasProfiles[0].id, "cp-collide");
    EXPECT_NE(loaded.canvasProfiles[1].id, "cp-collide");
    EXPECT_FALSE(loaded.canvasProfiles[1].id.empty());

    ASSERT_EQ(report.canvasProfiles.size(), 1u);
    EXPECT_EQ(report.canvasProfiles[0].name,  "Webtoon-2s");
    EXPECT_EQ(report.canvasProfiles[0].oldId, "cp-collide");
    EXPECT_EQ(report.canvasProfiles[0].newId, loaded.canvasProfiles[1].id);
    EXPECT_TRUE(report.any());
}

TEST(WorkspaceIdRepairTest, ProjectReferenceStillResolvesToTheFirstProfile)
{
    // This is what makes "first keeps the id" the right rule: the project does not lose its
    // assignment, and it lands on the same profile it resolved to before the repair.
    const TempWorkspace ws("dup_ref", duplicateCanvasIdJson());

    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path());

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    ASSERT_EQ(loaded.projectItems[0].canvasProfileIds.size(), 1u);
    EXPECT_EQ(loaded.projectItems[0].canvasProfileIds[0], loaded.canvasProfiles[0].id);
    EXPECT_EQ(loaded.canvasProfiles[0].name, "Webtoon-4s");
}

TEST(WorkspaceIdRepairTest, DuplicateBecomesAssignableAgain)
{
    // The user-visible symptom: with a shared id, the second profile counted as "already
    // assigned" and never appeared in the assign list.  After the repair it is unassigned.
    const TempWorkspace ws("dup_assignable", duplicateCanvasIdJson());

    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path());

    const auto& ids = loaded.projectItems[0].canvasProfileIds;
    EXPECT_EQ(std::find(ids.begin(), ids.end(), loaded.canvasProfiles[1].id), ids.end());
}

TEST(WorkspaceIdRepairTest, CleanWorkspaceIsReportedAsCleanAndUntouched)
{
    const TempWorkspace ws("clean", workspaceJson(
        canvasProfileJson(R"("id": "cp-aaa", )", "A", 10240) + "," +
            canvasProfileJson(R"("id": "cp-bbb", )", "B", 5120),
        "", ""));

    Infrastructure::WorkspaceRepairReport report;
    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path(), report);

    EXPECT_FALSE(report.any());
    EXPECT_EQ(loaded.canvasProfiles[0].id, "cp-aaa");
    EXPECT_EQ(loaded.canvasProfiles[1].id, "cp-bbb");
}

TEST(WorkspaceIdRepairTest, OutputProfileDuplicatesAreRepairedToo)
{
    const TempWorkspace ws("dup_output", workspaceJson(
        "",
        outputProfileJson(R"("id": "op-collide", )", "PNG") + "," +
            outputProfileJson(R"("id": "op-collide", )", "WEBP"),
        projectJson("[]", "op-collide")));

    Infrastructure::WorkspaceRepairReport report;
    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path(), report);

    // Two from the file, plus the preset the presence pass guarantees.
    ASSERT_EQ(loaded.outputProfiles.size(), 3u);
    EXPECT_EQ(loaded.outputProfiles[0].id, "op-collide");
    EXPECT_NE(loaded.outputProfiles[1].id, "op-collide");
    EXPECT_EQ(loaded.outputProfiles[2].id, Models::k_webtoonStandardPresetId);

    ASSERT_EQ(report.outputProfiles.size(), 1u);
    EXPECT_EQ(report.outputProfiles[0].name, "WEBP");

    // Reference keeps pointing at the profile it always resolved to.
    EXPECT_EQ(loaded.projectItems[0].outputProfileId, loaded.outputProfiles[0].id);
}

// ===========================================================================
// load() — migration away from the name-derived identifier
// ===========================================================================

TEST(WorkspaceIdMigrationTest, MissingIdIsMintedAndLegacyReferencesAreRelinked)
{
    // Pre-0.2.1, a profile saved without an id had one derived from its name, and projects
    // stored that derived form. The id is no longer derived, so the reference has to be
    // rewritten or the project would lose its output profile.
    //
    // This one is also the preset by its settings, so it is adopted rather than given a
    // random id — otherwise the presence pass would append a second, identical
    // "Webtoon Standard" next to it.
    const TempWorkspace ws("relink", workspaceJson(
        "",
        outputProfileJson("", "Webtoon Standard"),
        projectJson("[]", "op-Webtoon Standard")));

    Infrastructure::WorkspaceRepairReport report;
    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path(), report);

    ASSERT_EQ(loaded.outputProfiles.size(), 1u);
    EXPECT_EQ(loaded.outputProfiles[0].id, Models::k_webtoonStandardPresetId);

    // The project follows the profile to its new id.
    EXPECT_EQ(loaded.projectItems[0].outputProfileId, loaded.outputProfiles[0].id);

    // Minting is unambiguous bookkeeping, not a collision — nothing to tell the user about.
    EXPECT_FALSE(report.any());
}

TEST(WorkspaceIdMigrationTest, LegacyProfileTheUserEditedIsNotPromotedToAPreset)
{
    // Same legacy id, but the settings moved on (WebP instead of PNG). Adopting it would make
    // the shared preset id mean something different in this workspace than in every other, so
    // it stays a plain user profile — and the real preset is added alongside.
    std::string edited = outputProfileJson(R"("id": "op-Webtoon Standard", )", "Webtoon Standard");
    const auto pos = edited.find(R"("outputFormat": "PNG")");
    ASSERT_NE(pos, std::string::npos);
    edited.replace(pos, std::string(R"("outputFormat": "PNG")").size(),
                   R"("outputFormat": "WebP")");

    const TempWorkspace ws("edited_legacy", workspaceJson("", edited, ""));

    Infrastructure::WorkspaceRepairReport report;
    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path(), report);

    ASSERT_EQ(loaded.outputProfiles.size(), 2u);
    EXPECT_EQ(loaded.outputProfiles[0].id, "op-Webtoon Standard");   // left exactly as it was
    EXPECT_EQ(loaded.outputProfiles[0].outputFormat, Models::OutputFormat::WebP);
    EXPECT_EQ(loaded.outputProfiles[1].id, Models::k_webtoonStandardPresetId);
    EXPECT_FALSE(report.any());
}

TEST(WorkspaceIdMigrationTest, TwoIdlessProfilesSharingANameGetDistinctIds)
{
    // The old name-derived scheme handed both of these the identical id "cp-Cover".
    const TempWorkspace ws("same_name", workspaceJson(
        canvasProfileJson("", "Cover", 10240) + "," +
            canvasProfileJson("", "Cover", 5120),
        "", ""));

    const auto loaded = Infrastructure::WorkspaceSerializer{}.load(ws.path());

    ASSERT_EQ(loaded.canvasProfiles.size(), 2u);
    EXPECT_NE(loaded.canvasProfiles[0].id, loaded.canvasProfiles[1].id);
}

} // namespace Platemaker
