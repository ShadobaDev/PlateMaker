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
#include <platemaker/models/output_presets.hpp>

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

// ---------------------------------------------------------------------------
// Projects: duplicateProject — a naive seed (inputs + profile links), not a render clone
// ---------------------------------------------------------------------------

TEST(WorkspaceEditorTest, DuplicateProjectSeedsFromInputsAndProfilesOnly)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);

    const std::string cid = ed.addCanvasProfile(canvas("", "A", 800, 1000));
    const std::string oid = ed.addOutputProfile(output("", "Mine"));

    // Build a source project that looks fully rendered: linked profiles, an output directory, two
    // processed inputs, an output slice, and the render baselines.
    Models::ProjectItem& src = ed.addProject("Chapter 01");
    const std::string srcUid = src.uid;
    ASSERT_TRUE(ed.addCanvasProfileToProject(src, cid));
    ASSERT_TRUE(ed.setProjectOutputProfile(src, oid));
    src.getOutputDirectory()     = "C:/out/ch01";
    src.inputDirectory           = "C:/in/ch01";
    src.outputSignature          = "sig-xyz";
    src.canvasProfileIdsAtRender = {cid};

    Models::InputFile in0;
    in0.uid = "file-aaa"; in0.filePath = "C:/in/ch01/p0.png"; in0.order = 0;
    in0.sha256 = "deadbeef"; in0.status = Models::FileStatus::Processed;
    in0.lastProcessed = "2026-01-01T00:00:00Z"; in0.contributesTo = {"output_001.png"};
    in0.canvasProfileId = cid; in0.canvasFingerprint = "fp";
    src.getInputImages().push_back(std::move(in0));

    Models::InputFile in1;
    in1.uid = "file-bbb"; in1.filePath = "C:/in/ch01/p1.png"; in1.order = 1;
    in1.status = Models::FileStatus::Processed;
    src.getInputImages().push_back(std::move(in1));
    src.inputOrderAtRender = {"file-aaa", "file-bbb"};

    Models::OutputFile out0;
    out0.uid = "out-1"; out0.fileName = "output_001.png"; out0.sha256 = "cafebabe";
    src.getOutputImages().push_back(std::move(out0));
    src.rebuildLookupTables();

    // Duplicate. (The push_back may reallocate, so read the source afterwards via index, not `src`.)
    Models::ProjectItem& dup = ed.duplicateProject(ws.projectItems[0], "Chapter 01 (copy)");
    ASSERT_EQ(ws.projectItems.size(), 2u);

    // Identity: fresh workspace-unique project uid, the new name.
    EXPECT_EQ(dup.name, "Chapter 01 (copy)");
    EXPECT_EQ(dup.uid.rfind("proj-", 0), 0u);
    EXPECT_NE(dup.uid, srcUid);

    // Inputs: same files and order, but fresh project-local uids and NO render state (Pending).
    const auto& di = dup.getInputImages();
    ASSERT_EQ(di.size(), 2u);
    EXPECT_EQ(di[0].filePath, "C:/in/ch01/p0.png");
    EXPECT_EQ(di[0].order, 0);
    EXPECT_EQ(di[1].filePath, "C:/in/ch01/p1.png");
    EXPECT_EQ(di[1].order, 1);
    for (const auto& f : di) {
        EXPECT_FALSE(f.uid.empty());
        EXPECT_NE(f.uid, "file-aaa");
        EXPECT_NE(f.uid, "file-bbb");
        EXPECT_TRUE(f.sha256.empty());
        EXPECT_EQ(f.status, Models::FileStatus::Pending);
        EXPECT_TRUE(f.contributesTo.empty());
        EXPECT_TRUE(f.canvasProfileId.empty());
        EXPECT_TRUE(f.canvasFingerprint.empty());
        EXPECT_TRUE(f.lastProcessed.empty());
    }

    // Profile configuration is carried over; the input-side directory hint too.
    ASSERT_EQ(dup.canvasProfileIds().size(), 1u);
    EXPECT_EQ(dup.canvasProfileIds()[0], cid);
    EXPECT_EQ(dup.outputProfileId(), oid);
    EXPECT_EQ(dup.inputDirectory, "C:/in/ch01");

    // Everything the source *produced* is dropped.
    EXPECT_TRUE(dup.getOutputDirectory().empty());
    EXPECT_TRUE(dup.getOutputImages().empty());
    EXPECT_TRUE(dup.outputSignature.empty());
    EXPECT_TRUE(dup.canvasProfileIdsAtRender.empty());
    EXPECT_TRUE(dup.inputOrderAtRender.empty());

    // The source is left completely untouched.
    const auto& s = ws.projectItems[0];
    EXPECT_EQ(s.uid, srcUid);
    EXPECT_EQ(s.getOutputDirectory(), "C:/out/ch01");
    ASSERT_EQ(s.getOutputImages().size(), 1u);
    ASSERT_EQ(s.getInputImages().size(), 2u);
    EXPECT_EQ(s.getInputImages()[0].uid, "file-aaa");
    EXPECT_EQ(s.getInputImages()[0].sha256, "deadbeef");
    EXPECT_EQ(s.getInputImages()[0].status, Models::FileStatus::Processed);
}

// ---------------------------------------------------------------------------
// snapshotMeta / restoreMeta — workspace-level undo support
// ---------------------------------------------------------------------------

TEST(WorkspaceEditorSnapshotMetaTest, RestoresProfilesAndProjectNames)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);
    const std::string cid = ed.addCanvasProfile(canvas("", "A", 800, 1000));
    const std::string oid = ed.addOutputProfile(output("", "Mine"));
    ed.addProject("Chapter 01");
    ed.addProject("Chapter 02");

    const std::string snap = ed.snapshotMeta();

    // Mutate workspace-level state: delete the canvas profile and rename a project.
    ed.removeCanvasProfile(cid);
    ws.projectItems[0].name = "Renamed";
    ASSERT_TRUE(ws.canvasProfiles().empty());

    ed.restoreMeta(snap);

    ASSERT_EQ(ws.canvasProfiles().size(), 1u);
    EXPECT_EQ(ws.canvasProfiles()[0].id, cid);   // id preserved through the validated setter
    ASSERT_EQ(ws.outputProfiles().size(), 1u);
    EXPECT_EQ(ws.outputProfiles()[0].id, oid);
    EXPECT_EQ(ws.projectItems[0].name, "Chapter 01"); // name restored by uid
}

TEST(WorkspaceEditorSnapshotMetaTest, RestoresCanvasTemplateInfoExactly)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);
    const std::string cid = ed.addCanvasProfile(canvas("", "A", 800, 1000));
    ASSERT_TRUE(ed.setCanvasProfileTemplateInfo(cid, [] {
        Models::CanvasTemplateInfo t; t.path = "tpl.png"; return t; }()));

    const std::string snap = ed.snapshotMeta();

    // Clear the template (as a "delete template" would), then undo via restoreMeta.
    ASSERT_TRUE(ed.setCanvasProfileTemplateInfo(cid, Models::CanvasTemplateInfo{}));
    ASSERT_TRUE(ws.canvasProfiles()[0].templateInfo.path.empty());

    ed.restoreMeta(snap);
    EXPECT_EQ(ws.canvasProfiles()[0].templateInfo.path, "tpl.png");
}

TEST(WorkspaceEditorSnapshotMetaTest, LeavesProjectContentsUntouched)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);
    Models::ProjectItem& p = ed.addProject("Chapter");
    Models::InputFile inf;
    inf.uid = "u0"; inf.filePath = "p0.png"; inf.order = 0;
    p.getInputImages().push_back(std::move(inf));

    const std::string snap = ed.snapshotMeta();

    // Add project content after the snapshot; restoreMeta must not revert it (contents are the
    // project scope's job, captured by ProjectEditor::snapshot — not this metadata snapshot).
    Models::InputFile inf2;
    inf2.uid = "u1"; inf2.filePath = "p1.png"; inf2.order = 1;
    ws.projectItems[0].getInputImages().push_back(std::move(inf2));

    ed.restoreMeta(snap);
    EXPECT_EQ(ws.projectItems[0].getInputImages().size(), 2u);
}

// ---------------------------------------------------------------------------
// importProfiles — cross-workspace transfer invariant (fresh ids, no template, no presets)
// ---------------------------------------------------------------------------

TEST(WorkspaceEditorImportTest, MintsFreshIdsAndClearsTemplateInfo)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);

    // A profile arriving from another workspace carries that workspace's id and a template path
    // relative to *its* directory. Import must give a fresh id and drop the template.
    const auto report = ed.importProfiles(
        {canvas("cp-source", "Manga B4", 1600, 10240, "templates/manga.png")},
        {});

    ASSERT_EQ(report.canvasIds.size(), 1u);
    ASSERT_EQ(ws.canvasProfiles().size(), 1u);
    const auto& imported = ws.canvasProfiles()[0];
    EXPECT_NE(imported.id, "cp-source");            // fresh id, not the source's
    EXPECT_EQ(imported.id, report.canvasIds[0]);
    EXPECT_EQ(imported.name, "Manga B4");           // content preserved
    EXPECT_EQ(imported.canvasSize.width, 1600);
    EXPECT_TRUE(imported.templateInfo.path.empty()); // template stripped
}

TEST(WorkspaceEditorImportTest, SkipsPresetOutputProfilesButKeepsUserOnes)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);

    // A preset id must never be imported as a user copy — it resolves from the catalogue instead.
    const auto report = ed.importProfiles(
        {},
        {output(std::string(Models::k_webtoonStandardPresetId), "Webtoon Standard"),
         output("op-source", "My Output")});

    ASSERT_EQ(report.outputIds.size(), 1u);          // only the user profile came in
    ASSERT_EQ(ws.outputProfiles().size(), 1u);
    EXPECT_EQ(ws.outputProfiles()[0].name, "My Output");
    EXPECT_NE(ws.outputProfiles()[0].id, "op-source"); // fresh user id
    EXPECT_EQ(ws.outputProfiles()[0].id, report.outputIds[0]);
}

TEST(WorkspaceEditorImportTest, IsAdditiveAndLeavesExistingProfilesUntouched)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor ed(ws);

    const std::string existing = ed.addCanvasProfile(canvas("", "Existing", 800, 1280));
    ed.importProfiles({canvas("", "Imported", 900, 1280)}, {});

    ASSERT_EQ(ws.canvasProfiles().size(), 2u);
    EXPECT_TRUE(hasCanvasId(ws, existing));          // the original survives
    EXPECT_EQ(ws.canvasProfiles()[0].name, "Existing");
    EXPECT_EQ(ws.canvasProfiles()[1].name, "Imported");
}

} // namespace Platemaker
