/**
 * \file
 * \brief Unit tests for input ordering: ProjectEditor, inputsInOrder(), and the input-composition
 *        staleness axis (detectInputCompositionChange / inputOrderAtRender + load-time backfill).
 *
 * The strip is built in InputFile::order sequence, not in the stored-vector order; a reorder rewrites
 * only the order field. These pin that the render sees the reordered sequence, that the stored vector
 * is never physically moved, and that a reorder (or add/remove) is detected as stale outputs and
 * survives a save→load round trip — including a project rendered before the baseline existed.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-29
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/project_editor/project_editor.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>
#include <platemaker/models/workspace.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Platemaker {
namespace {

namespace fs = std::filesystem;
using Models::FileStatus;
using Models::InputFile;
using Models::OutputFile;
using Models::ProjectItem;

/// A project with \p n inputs (uid "u0".."u<n-1>", path "pN.png", order == index) and no outputs.
ProjectItem makeProject(int n)
{
    ProjectItem p;
    p.name = "Chapter";
    for (int i = 0; i < n; ++i) {
        InputFile inf;
        inf.uid      = "u" + std::to_string(i);
        inf.filePath = "p" + std::to_string(i) + ".png";
        inf.order    = i;
        inf.status   = FileStatus::Processed;
        p.getInputImages().push_back(std::move(inf));
    }
    return p;
}

std::vector<std::string> orderSeqUids(const ProjectItem& p)
{
    std::vector<std::string> uids;
    for (const auto& f : p.inputsInOrder())
        uids.push_back(f.uid);
    return uids;
}

std::vector<std::string> vectorSeqPaths(const ProjectItem& p)
{
    std::vector<std::string> paths;
    for (const auto& f : p.getInputImages())
        paths.push_back(f.filePath);
    return paths;
}

} // namespace

// ---------------------------------------------------------------------------
// ProjectEditor::setInputOrder / moveInput / inputsInOrder
// ---------------------------------------------------------------------------

TEST(ProjectEditorTest, SetInputOrderRewritesOrderFieldsAndLeavesVectorUnchanged)
{
    ProjectItem p = makeProject(3);            // vector + order: u0, u1, u2

    Infrastructure::ProjectEditor(p).setInputOrder({"u2", "u0", "u1"});

    // The render sequence follows `order`, which is now u2, u0, u1 …
    EXPECT_EQ(orderSeqUids(p), (std::vector<std::string>{"u2", "u0", "u1"}));
    // … while the physical vector is untouched (still p0, p1, p2 by insertion).
    EXPECT_EQ(vectorSeqPaths(p), (std::vector<std::string>{"p0.png", "p1.png", "p2.png"}));
}

TEST(ProjectEditorTest, SetInputOrderRejectsNonPermutation)
{
    ProjectItem p = makeProject(3);
    Infrastructure::ProjectEditor ed(p);

    EXPECT_FALSE(ed.setInputOrder({"u0", "u1"}));            // wrong size
    EXPECT_FALSE(ed.setInputOrder({"u0", "u0", "u1"}));      // duplicate
    EXPECT_FALSE(ed.setInputOrder({"u0", "u1", "x"}));       // unknown uid

    // A rejected order changes nothing.
    EXPECT_EQ(orderSeqUids(p), (std::vector<std::string>{"u0", "u1", "u2"}));
}

TEST(ProjectEditorTest, MoveInputUpAndDown)
{
    ProjectItem p = makeProject(3);
    Infrastructure::ProjectEditor ed(p);

    EXPECT_TRUE(ed.moveInput("u2", -1));   // u2 up one → u0, u2, u1
    EXPECT_EQ(orderSeqUids(p), (std::vector<std::string>{"u0", "u2", "u1"}));

    EXPECT_TRUE(ed.moveInput("u0", +1));   // u0 down one → u2, u0, u1
    EXPECT_EQ(orderSeqUids(p), (std::vector<std::string>{"u2", "u0", "u1"}));

    EXPECT_FALSE(ed.moveInput("u2", -1));  // already first
    EXPECT_FALSE(ed.moveInput("u1", +1));  // already last
    EXPECT_FALSE(ed.moveInput("nope", -1)); // unknown
}

// ---------------------------------------------------------------------------
// ProjectEditor::snapshot / restore — undo/redo support
// ---------------------------------------------------------------------------

TEST(ProjectEditorSnapshotTest, RoundTripsContentAndPreservesName)
{
    ProjectItem p = makeProject(3);
    p.getOutputDirectory() = "out/dir";
    p.outputSignature      = "sig123";

    Infrastructure::ProjectEditor ed(p);
    const std::string snap = ed.snapshot();

    // Mutate: reorder, change output dir, rename.
    ed.setInputOrder({"u2", "u1", "u0"});
    p.getOutputDirectory() = "different";
    p.name                 = "Renamed";

    ed.restore(snap);

    EXPECT_EQ(orderSeqUids(p), (std::vector<std::string>{"u0", "u1", "u2"})); // order restored
    EXPECT_EQ(p.getOutputDirectory(), "out/dir");                             // dir restored
    EXPECT_EQ(p.outputSignature, "sig123");
    EXPECT_EQ(p.name, "Renamed"); // name is workspace-owned → preserved, NOT reverted to the snapshot
}

TEST(ProjectEditorSnapshotTest, RoundTripsProfileLinks)
{
    Models::Workspace ws;
    Infrastructure::WorkspaceEditor wed(ws);

    Models::CanvasProfile cp;
    cp.name       = "A4";
    cp.canvasSize = {100, 200};
    const std::string cid = wed.addCanvasProfile(cp);

    Models::OutputProfile op;
    op.name = "Web";
    const std::string oid = wed.addOutputProfile(op);

    Models::ProjectItem& p = wed.addProject("Chapter");
    ASSERT_TRUE(wed.addCanvasProfileToProject(p, cid));
    ASSERT_TRUE(wed.setProjectOutputProfile(p, oid));
    ASSERT_EQ(p.canvasProfileIds(), (std::vector<std::string>{cid}));
    ASSERT_EQ(p.outputProfileId(), oid);

    const std::string snap = Infrastructure::ProjectEditor(p).snapshot();

    // Unlink everything, then restore.
    ASSERT_TRUE(wed.removeCanvasProfileFromProject(p, cid));
    ASSERT_TRUE(wed.setProjectOutputProfile(p, ""));
    ASSERT_TRUE(p.canvasProfileIds().empty());
    ASSERT_TRUE(p.outputProfileId().empty());

    Infrastructure::ProjectEditor(p).restore(snap);
    EXPECT_EQ(p.canvasProfileIds(), (std::vector<std::string>{cid}));
    EXPECT_EQ(p.outputProfileId(), oid);
}

// ---------------------------------------------------------------------------
// detectInputCompositionChange — the persistent baseline compare
// ---------------------------------------------------------------------------

TEST(InputCompositionTest, DetectsReorderAgainstBaseline)
{
    ProjectItem p = makeProject(3);
    // Pretend a render happened: one output exists and the baseline is the current order.
    OutputFile out;
    out.fileName = "output_001.png";
    out.status   = FileStatus::Done;
    p.getOutputImages().push_back(std::move(out));
    p.inputOrderAtRender = {"u0", "u1", "u2"};

    EXPECT_FALSE(p.detectInputCompositionChange());          // unchanged

    Infrastructure::ProjectEditor(p).setInputOrder({"u1", "u0", "u2"});
    EXPECT_TRUE(p.detectInputCompositionChange());           // reordered
}

TEST(InputCompositionTest, NoOutputsOrNoBaselineReportsNoChange)
{
    ProjectItem p = makeProject(3);
    p.inputOrderAtRender = {"u2", "u1", "u0"};   // differs from current, but…
    EXPECT_FALSE(p.detectInputCompositionChange()); // …no outputs → nothing to invalidate

    OutputFile out; out.fileName = "output_001.png"; out.status = FileStatus::Done;
    p.getOutputImages().push_back(std::move(out));
    p.inputOrderAtRender.clear();                 // no baseline (pre-existing project)
    EXPECT_FALSE(p.detectInputCompositionChange());
}

TEST(InputCompositionTest, ApplyProcessingResultsCapturesBaseline)
{
    ProjectItem p = makeProject(3);
    Infrastructure::ProjectEditor(p).setInputOrder({"u2", "u0", "u1"});

    // Files need not exist — the baseline is captured unconditionally at the end of the apply.
    // (The unreadable inputs now come back as post-render Error entries; this test only cares about
    // the baseline, so the returned errors are intentionally discarded.)
    (void)Infrastructure::ProjectEditor{p}.applyProcessingResults(/*records*/ {}, /*applied*/ {}, /*skipped*/ {},
                                   /*workspaceProfiles*/ {}, /*outDir*/ "out", /*ts*/ "2026-07-29T00:00:00Z");

    EXPECT_EQ(p.inputOrderAtRender, (std::vector<std::string>{"u2", "u0", "u1"}));
}

// ---------------------------------------------------------------------------
// Serialization: the baseline round-trips; and it is backfilled for old projects.
// ---------------------------------------------------------------------------

class SerializedProject : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_dir = fs::temp_directory_path() / "pm-project-editor-test";
        fs::remove_all(m_dir);
        fs::create_directories(m_dir);
    }
    void TearDown() override { std::error_code ec; fs::remove_all(m_dir, ec); }

    Models::Workspace roundTrip(Models::Workspace in)
    {
        const std::string path = (m_dir / "ws.platemaker.json").string();
        Infrastructure::WorkspaceSerializer s;
        s.save(in, path);
        return s.load(path);
    }

    fs::path m_dir;
};

TEST_F(SerializedProject, BaselineSurvivesRoundTrip)
{
    Models::Workspace ws;
    ProjectItem p = makeProject(3);
    p.uid = "proj-test";
    p.inputOrderAtRender = {"u1", "u2", "u0"};
    ws.projectItems.push_back(std::move(p));

    Models::Workspace loaded = roundTrip(std::move(ws));
    ASSERT_EQ(loaded.projectItems.size(), 1u);
    EXPECT_EQ(loaded.projectItems[0].inputOrderAtRender,
              (std::vector<std::string>{"u1", "u2", "u0"}));
}

TEST_F(SerializedProject, BackfillsBaselineFromOutputProvenance)
{
    // A project rendered before the baseline field existed: outputs carry sourceMap, but
    // inputOrderAtRender is empty. Provenance order is u0, u1, u2 (out1 feeds p0 then p1; out2 p2).
    Models::Workspace ws;
    ProjectItem p = makeProject(3);
    p.uid = "proj-old";

    OutputFile out1;
    out1.fileName  = "output_001.png";
    out1.status    = FileStatus::Done;
    out1.sourceMap = {{"p0.png", 0, 10}, {"p1.png", 0, 5}};
    p.getOutputImages().push_back(std::move(out1));

    OutputFile out2;
    out2.fileName  = "output_002.png";
    out2.status    = FileStatus::Done;
    out2.sourceMap = {{"p2.png", 0, 10}};
    p.getOutputImages().push_back(std::move(out2));

    ASSERT_TRUE(p.inputOrderAtRender.empty());
    ws.projectItems.push_back(std::move(p));

    Models::Workspace loaded = roundTrip(std::move(ws));
    ASSERT_EQ(loaded.projectItems.size(), 1u);
    // Reconstructed from provenance, keyed by the current input uids.
    EXPECT_EQ(loaded.projectItems[0].inputOrderAtRender,
              (std::vector<std::string>{"u0", "u1", "u2"}));

    // With the baseline restored, a reorder is now detectable.
    Infrastructure::ProjectEditor(loaded.projectItems[0]).setInputOrder({"u2", "u1", "u0"});
    EXPECT_TRUE(loaded.projectItems[0].detectInputCompositionChange());
}

// ---------------------------------------------------------------------------
// sanitize() marks outputs out-of-sync on a reorder (disk-backed — sanitize hashes files)
// ---------------------------------------------------------------------------

TEST(InputCompositionSanitizeTest, ReorderMarksOutputsDesynchronized)
{
    const fs::path dir = fs::temp_directory_path() / "pm-reorder-sanitize";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const auto write = [&](const std::string& name, const std::string& content) {
        const auto path = dir / name;
        std::ofstream(path, std::ios::binary) << content;
        return path.string();
    };

    ProjectItem p;
    p.name = "Chapter";
    p.getOutputDirectory() = dir.string();
    for (int i = 0; i < 3; ++i) {
        InputFile inf;
        inf.uid      = "u" + std::to_string(i);
        inf.filePath = write("page_" + std::to_string(i) + ".png", "in-" + std::to_string(i));
        inf.sha256   = Infrastructure::FileMetaData::computeFileSha256(inf.filePath);
        inf.order    = i;
        p.getInputImages().push_back(std::move(inf));

        OutputFile outf;
        outf.fileName = "output_" + std::to_string(i) + ".png";
        outf.sha256   = Infrastructure::FileMetaData::computeFileSha256(
            write(outf.fileName, "out-" + std::to_string(i)));
        p.getOutputImages().push_back(std::move(outf));
    }
    p.inputOrderAtRender = {"u0", "u1", "u2"};   // baseline from the "render"
    p.rebuildLookupTables();

    EXPECT_TRUE(Infrastructure::ProjectEditor{p}.sanitize(/*workspaceProfiles*/ {}));   // unchanged → everything settled

    // Reorder, then re-sanitize: outputs go out of sync, inputs stay Processed.
    Infrastructure::ProjectEditor(p).setInputOrder({"u2", "u1", "u0"});
    EXPECT_FALSE(Infrastructure::ProjectEditor{p}.sanitize({}));
    for (const auto& out : p.getOutputImages())
        EXPECT_EQ(out.status, FileStatus::Desynchronized);
    for (const auto& inf : p.getInputImages())
        EXPECT_EQ(inf.status, FileStatus::Processed);

    std::error_code ec; fs::remove_all(dir, ec);
}

} // namespace Platemaker
