/**
 * \file
 * \brief Unit tests for WorkspaceEditor — the single authority that mutates the profile palettes.
 *
 * These pin the intent-level operations and, crucially, that they enforce the *same* invariants a
 * loaded file is put through: unique ids, no persisted presets, templateInfo carried across a bulk
 * replace, and the project↔profile link guard.  installLoaded() is tested directly as the load path.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-27
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>
#include <platemaker/models/workspace.hpp>

#include <algorithm>
#include <string>

namespace Platemaker {

namespace {

Models::CanvasProfile canvas(std::string id, std::string name, int w, int h,
                             std::string templatePath = "")
{
    Models::CanvasProfile cp;
    cp.id                = std::move(id);
    cp.name              = std::move(name);
    cp.canvasSize        = {w, h};
    cp.margins           = {0, 0, 0, 0};
    cp.templateInfo.path = std::move(templatePath);
    return cp;
}

Models::OutputProfile output(std::string id, std::string name)
{
    Models::OutputProfile op;
    op.id   = std::move(id);
    op.name = std::move(name);
    return op;
}

bool hasCanvasId(const Models::Workspace& ws, const std::string& id)
{
    const auto& v = ws.canvasProfiles();
    return std::any_of(v.begin(), v.end(), [&](const auto& p) { return p.id == id; });
}

} // namespace

// ---------------------------------------------------------------------------
// Canvas palette: add / remove
// ---------------------------------------------------------------------------

TEST(WorkspaceEditorTest, AddCanvasProfileMintsAUniqueIdAndAppends)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);

    const std::string id1 = ed.addCanvasProfile(canvas("", "A", 800, 1000));
    const std::string id2 = ed.addCanvasProfile(canvas("", "B", 900, 1000));

    EXPECT_FALSE(id1.empty());
    EXPECT_NE(id1, id2);
    ASSERT_EQ(ws.canvasProfiles().size(), 2u);
    EXPECT_EQ(ws.canvasProfiles()[0].id, id1);
}

TEST(WorkspaceEditorTest, AddCanvasProfileOverwritesAnyIncomingId)
{
    Models::Workspace ws;
    // Even if a caller hands in an id, the editor mints its own so a caller can never plant a
    // colliding or reserved id.
    const std::string minted = Infrastructure::WorkspaceEditor(ws).addCanvasProfile(
        canvas("cp-planted", "A", 800, 1000));

    EXPECT_NE(minted, "cp-planted");
    EXPECT_FALSE(hasCanvasId(ws, "cp-planted"));
    EXPECT_TRUE(hasCanvasId(ws, minted));
}

TEST(WorkspaceEditorTest, RemoveCanvasProfileReportsWhetherItExisted)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);
    const std::string id = ed.addCanvasProfile(canvas("", "A", 800, 1000));

    EXPECT_FALSE(ed.removeCanvasProfile("cp-nope"));
    EXPECT_TRUE(ed.removeCanvasProfile(id));
    EXPECT_TRUE(ws.canvasProfiles().empty());
}

// ---------------------------------------------------------------------------
// replaceCanvasProfiles — carries templateInfo, mints, dedups
// ---------------------------------------------------------------------------

TEST(WorkspaceEditorTest, ReplaceCanvasProfilesCarriesTemplateInfoFromCurrentById)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);

    // Seed a profile that owns a template.
    ed.replaceCanvasProfiles({canvas("cp-1", "A", 800, 1000, "tpl.png")});
    ASSERT_EQ(ws.canvasProfiles().size(), 1u);
    ASSERT_EQ(ws.canvasProfiles()[0].templateInfo.path, "tpl.png");

    // A manage-dialog round trip returns the same id but drops templateInfo. The editor must
    // restore it from the workspace (the source of truth) while keeping the edited field (name).
    ed.replaceCanvasProfiles({canvas("cp-1", "A-renamed", 800, 1000, /*templatePath=*/"")});

    ASSERT_EQ(ws.canvasProfiles().size(), 1u);
    EXPECT_EQ(ws.canvasProfiles()[0].name, "A-renamed");
    EXPECT_EQ(ws.canvasProfiles()[0].templateInfo.path, "tpl.png");
}

TEST(WorkspaceEditorTest, ReplaceCanvasProfilesMintsMissingIdsAndDeduplicates)
{
    Models::Workspace ws;
    const auto report = Infrastructure::WorkspaceEditor(ws).replaceCanvasProfiles({
        canvas("",       "NoId",   800, 1000),  // must be minted
        canvas("cp-dup", "First",  900, 1000),
        canvas("cp-dup", "Second", 700, 1000),  // duplicate id → reassigned
    });

    ASSERT_EQ(ws.canvasProfiles().size(), 3u);
    for (const auto& cp : ws.canvasProfiles())
        EXPECT_FALSE(cp.id.empty());

    // Every id is unique after the pass.
    EXPECT_NE(ws.canvasProfiles()[0].id, ws.canvasProfiles()[1].id);
    EXPECT_NE(ws.canvasProfiles()[1].id, ws.canvasProfiles()[2].id);
    EXPECT_NE(ws.canvasProfiles()[0].id, ws.canvasProfiles()[2].id);

    // The first holder of cp-dup keeps it; the later duplicate is reported.
    EXPECT_EQ(ws.canvasProfiles()[1].id, "cp-dup");
    ASSERT_EQ(report.canvasProfiles.size(), 1u);
    EXPECT_EQ(report.canvasProfiles[0].name,  "Second");
    EXPECT_EQ(report.canvasProfiles[0].oldId, "cp-dup");
}

TEST(WorkspaceEditorTest, SetCanvasProfileTemplateInfoSetsAndClearsExactly)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);
    const std::string id = ed.addCanvasProfile(canvas("", "A", 800, 1000));

    Models::CanvasTemplateInfo info;
    info.path = "templates/a.png";
    EXPECT_TRUE(ed.setCanvasProfileTemplateInfo(id, info));
    EXPECT_EQ(ws.canvasProfiles()[0].templateInfo.path, "templates/a.png");

    // Clearing sets an exactly-empty value — a carry heuristic could not express this.
    EXPECT_TRUE(ed.setCanvasProfileTemplateInfo(id, Models::CanvasTemplateInfo{}));
    EXPECT_TRUE(ws.canvasProfiles()[0].templateInfo.path.empty());

    // Unknown id is a no-op failure.
    EXPECT_FALSE(ed.setCanvasProfileTemplateInfo("cp-nope", info));
}

// ---------------------------------------------------------------------------
// Output palette
// ---------------------------------------------------------------------------

TEST(WorkspaceEditorTest, ReplaceOutputProfilesStripsPersistedPresets)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor(ws).replaceOutputProfiles({
        Models::webtoonStandardPreset(),   // preset id — must be dropped
        output("op-mine", "Mine"),
    });

    ASSERT_EQ(ws.outputProfiles().size(), 1u);
    EXPECT_EQ(ws.outputProfiles()[0].id, "op-mine");
    EXPECT_EQ(Models::outputPresetDefById(ws.outputProfiles()[0].id), nullptr);
}

TEST(WorkspaceEditorTest, AddOutputProfileNeverMintsAPresetId)
{
    Models::Workspace ws;
    const std::string id = Infrastructure::WorkspaceEditor(ws).addOutputProfile(output("", "Mine"));
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(Models::outputPresetDefById(id), nullptr);
}

// ---------------------------------------------------------------------------
// Project ↔ profile links
// ---------------------------------------------------------------------------

TEST(WorkspaceEditorTest, LinkAndUnlinkCanvasProfileAreSymmetric)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);
    const std::string id = ed.addCanvasProfile(canvas("", "A", 800, 1000));

    Models::ProjectItem proj;
    EXPECT_TRUE(ed.addCanvasProfileToProject(proj, id));
    ASSERT_EQ(proj.canvasProfileIds().size(), 1u);
    EXPECT_EQ(proj.canvasProfileIds()[0], id);

    // Idempotent add; unknown-id add fails.
    EXPECT_TRUE(ed.addCanvasProfileToProject(proj, id));
    EXPECT_FALSE(ed.addCanvasProfileToProject(proj, "cp-nope"));

    EXPECT_TRUE(ed.removeCanvasProfileFromProject(proj, id));
    EXPECT_TRUE(proj.canvasProfileIds().empty());
    EXPECT_FALSE(ed.removeCanvasProfileFromProject(proj, id)); // already gone
}

TEST(WorkspaceEditorTest, LinkCanvasProfileRejectsADimensionConflict)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);
    const std::string a = ed.addCanvasProfile(canvas("", "A", 800, 1000));
    const std::string b = ed.addCanvasProfile(canvas("", "B", 800, 1000)); // same W×H

    Models::ProjectItem proj;
    EXPECT_TRUE(ed.addCanvasProfileToProject(proj, a));
    EXPECT_FALSE(ed.addCanvasProfileToProject(proj, b)); // conflict: same canvas dimensions
    EXPECT_EQ(proj.canvasProfileIds().size(), 1u);
}

TEST(WorkspaceEditorTest, SetProjectOutputProfileValidatesTheId)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);
    const std::string mine = ed.addOutputProfile(output("", "Mine"));

    Models::ProjectItem proj;

    // A user profile id resolves.
    EXPECT_TRUE(ed.setProjectOutputProfile(proj, mine));
    EXPECT_EQ(proj.outputProfileId(), mine);

    // A baked-in preset id resolves too (a project can point at a preset).
    EXPECT_TRUE(ed.setProjectOutputProfile(proj, std::string(Models::k_webtoonStandardPresetId)));
    EXPECT_EQ(proj.outputProfileId(), Models::k_webtoonStandardPresetId);

    // Empty = "use the workspace default" — accepted, clears the link.
    EXPECT_TRUE(ed.setProjectOutputProfile(proj, ""));
    EXPECT_TRUE(proj.outputProfileId().empty());

    // An unknown id is rejected and leaves the field untouched.
    EXPECT_FALSE(ed.setProjectOutputProfile(proj, "op-nope"));
    EXPECT_TRUE(proj.outputProfileId().empty());
}

// ---------------------------------------------------------------------------
// installLoaded — the load path runs the full repair pass
// ---------------------------------------------------------------------------

TEST(WorkspaceEditorTest, InstallLoadedMintsDeduplicatesAndDropsPersistedPresets)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceRepairReport report;

    Infrastructure::WorkspaceEditor(ws).installLoaded(
        /*canvas=*/{
            canvas("cp-x", "First",  800, 1000),
            canvas("cp-x", "Second", 900, 1000),  // duplicate id → reassigned + reported
            canvas("",     "NoId",   700, 1000),  // minted
        },
        /*output=*/{
            Models::webtoonStandardPreset(),       // persisted preset → dropped
            output("op-user", "Mine"),
        },
        report);

    // Canvas: three profiles, all with unique non-empty ids; the collision is reported.
    ASSERT_EQ(ws.canvasProfiles().size(), 3u);
    for (const auto& cp : ws.canvasProfiles())
        EXPECT_FALSE(cp.id.empty());
    EXPECT_EQ(ws.canvasProfiles()[0].id, "cp-x");
    EXPECT_NE(ws.canvasProfiles()[1].id, "cp-x");
    ASSERT_EQ(report.canvasProfiles.size(), 1u);
    EXPECT_EQ(report.canvasProfiles[0].oldId, "cp-x");

    // Output: the preset is gone, only the user profile remains.
    ASSERT_EQ(ws.outputProfiles().size(), 1u);
    EXPECT_EQ(ws.outputProfiles()[0].id, "op-user");
}

// ---------------------------------------------------------------------------
// Projects: addProject mints the uid (a workspace-unique concern the lib owns)
// ---------------------------------------------------------------------------

TEST(WorkspaceEditorTest, AddProjectMintsAUniqueProjUidAndAppends)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);

    // The returned reference is only valid until the next mutation of the project list, so capture
    // the minted uid before adding the second project.
    const std::string uidA = ed.addProject("Chapter 01").uid;
    const std::string uidB = ed.addProject("Chapter 02").uid;

    EXPECT_EQ(uidA.rfind("proj-", 0), 0u) << "expected a 'proj-' prefix, got " << uidA;
    EXPECT_FALSE(uidA.empty());
    EXPECT_NE(uidA, uidB);
    ASSERT_EQ(ws.projectItems.size(), 2u);
    EXPECT_EQ(ws.projectItems[0].name, "Chapter 01");
    EXPECT_EQ(ws.projectItems[0].uid, uidA);
    EXPECT_EQ(ws.projectItems[1].name, "Chapter 02");
    EXPECT_EQ(ws.projectItems[1].uid, uidB);
}

} // namespace Platemaker
