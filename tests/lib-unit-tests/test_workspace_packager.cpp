/**
 * \file tests/lib-unit-tests/test_workspace_packager.cpp
 * \brief WorkspacePackager::plan() — every referenced file placed once, the copy rewritten to name the
 *        places, nothing a render reproduces carried, and the copy loading back from the package root.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <archive.h>

#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
#include <platemaker/infrastructure/workspace_packager/workspace_packager.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace Platemaker::Infrastructure {

namespace {

namespace fs = std::filesystem;

struct Scratch {
    fs::path root;
    explicit Scratch(const char* name)
        : root(fs::temp_directory_path() / utf8ToPath(std::string("pm_package_ż_") + name))
    {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(root, ec); }
};

/// The form paths are compared in: UTF-8, normalised, '/' separators — what consumers write.
std::string g(const fs::path& p)
{
    const std::u8string s = p.lexically_normal().generic_u8string();
    return std::string(s.begin(), s.end());
}

void touchFile(const fs::path& p, const std::string& bytes = "x")
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << bytes;
}

Models::InputFile page(const std::string& uid, const fs::path& file, int order)
{
    Models::InputFile f;
    f.uid           = uid;
    f.filePath      = g(file);
    f.sha256        = "sha-" + uid;
    f.order         = order;
    f.status        = Models::FileStatus::Processed;
    f.lastProcessed = "2026-09-30T12:00:00";
    f.contributesTo = {"001.webp"};
    f.thumbnailPath = "C:/cache/thumb.png";
    return f;
}

Models::StripOverlay overlay(const std::string& uid, const fs::path& asset, const std::string& anchor)
{
    Models::StripOverlay o;
    o.uid            = uid;
    o.assetPath      = g(asset);
    o.sha256         = "sha-" + uid;
    o.anchorInputUid = anchor;
    return o;
}

} // anonymous namespace

TEST(WorkspacePackager, APlanPlacesEveryFileOnceAndCarriesNoRender)
{
    const Scratch  s("plan");
    const fs::path ws = s.root / "Chapter_002";
    const fs::path a  = s.root / "scans" / "a";
    const fs::path b  = s.root / "scans" / "b";

    touchFile(a / "001.png");
    touchFile(a / "002.png");
    touchFile(b / "001.png");                          // same name, another folder: a clash
    touchFile(ws / "overlays" / "ovl-1.svg");
    touchFile(ws / "templates" / "3p-m.png");
    const fs::path gone = s.root / "scans" / "gone" / "003.png";   // referenced, never on disk

    Models::Workspace workspace;
    workspace.outputDirectory = g(s.root / "out");
    {
        Models::CanvasProfile canvas;
        canvas.name       = "Standard";
        canvas.canvasSize = {1600, 10240};
        WorkspaceEditor ed(workspace);
        const std::string id = ed.addCanvasProfile(canvas);
        Models::CanvasTemplateInfo info;
        info.path = "templates/3p-m.png";             // relative to the workspace folder, as the GUI writes it
        ed.setCanvasProfileTemplateInfo(id, info);
    }

    Models::ProjectItem one;
    one.name                = "One";
    one.uid                 = "proj-1";
    one.inputDirectory      = g(a);
    one.outputSignature     = "sig";
    one.inputOrderAtRender  = {"in-1", "in-2"};
    one.getOutputDirectory() = g(s.root / "out" / "one");
    one.getInputImages()    = {page("in-1", a / "001.png", 0), page("in-2", a / "002.png", 1),
                               page("in-3", gone, 2)};
    one.getStripOverlays()  = {overlay("ovl-1", ws / "overlays" / "ovl-1.svg", "in-1"),
                               overlay("ovl-2", ws / "overlays" / "ovl-1.svg", "in-2")};   // shares a file
    Models::OutputFile out;
    out.uid      = "out-1";
    out.fileName = "001.webp";
    one.getOutputImages() = {out};

    Models::ProjectItem two;
    two.name             = "Two";
    two.uid              = "proj-2";
    two.getInputImages() = {page("in-4", b / "001.png", 0), page("in-5", a / "001.png", 1)};   // shares one

    workspace.projectItems.push_back(std::move(one));
    workspace.projectItems.push_back(std::move(two));

    const PackagePlan plan = WorkspacePackager::plan(workspace, pathToUtf8(ws / "Chapter_002.platemaker.json"));

    EXPECT_EQ(plan.workspaceFileName, "Chapter_002.platemaker.json");

    // Each file once, under its own name; the clash gets " (2)".
    std::map<std::string, std::string> packed;
    for (const auto& f : plan.files)
        packed.emplace(f.target, g(utf8ToPath(f.source)));
    EXPECT_EQ(packed.size(), plan.files.size()) << "a target was given out twice";
    EXPECT_EQ(packed, (std::map<std::string, std::string>{
                          {"inputs/001.png", g(a / "001.png")},
                          {"inputs/002.png", g(a / "002.png")},
                          {"inputs/001 (2).png", g(b / "001.png")},
                          {"overlays/ovl-1.svg", g(ws / "overlays" / "ovl-1.svg")},
                          {"templates/3p-m.png", g(ws / "templates" / "3p-m.png")},
                      }));

    // The missing page is declared, not packed, and has a place to be dropped into.
    ASSERT_EQ(plan.missing.size(), 1u);
    EXPECT_EQ(plan.missing[0].project, "One");
    EXPECT_EQ(plan.missing[0].source, g(gone));
    EXPECT_EQ(plan.missing[0].target, "inputs/003.png");

    // The copy opens from the package root: write it and the files there, and load it back.
    const fs::path root = s.root / "unpacked";
    for (const auto& f : plan.files)
        touchFile(root / utf8ToPath(f.target));
    std::ofstream(root / plan.workspaceFileName, std::ios::binary) << plan.workspaceJson;

    const Models::Workspace copy = WorkspaceSerializer{}.load(pathToUtf8(root / plan.workspaceFileName));
    EXPECT_TRUE(copy.outputDirectory.empty());
    ASSERT_EQ(copy.projectItems.size(), 2u);

    const auto& p1 = copy.projectItems[0];
    EXPECT_EQ(p1.uid, "proj-1");
    EXPECT_TRUE(p1.getOutputImages().empty());
    EXPECT_TRUE(p1.getOutputDirectory().empty());
    EXPECT_TRUE(p1.inputDirectory.empty());
    EXPECT_TRUE(p1.outputSignature.empty());
    EXPECT_TRUE(p1.inputOrderAtRender.empty());
    ASSERT_EQ(p1.getInputImages().size(), 3u);
    const auto& in1 = p1.getInputImages()[0];
    EXPECT_EQ(in1.uid, "in-1");                               // identity kept
    EXPECT_EQ(in1.sha256, "sha-in-1");                        // the bytes are the same bytes
    EXPECT_EQ(in1.filePath, g(root / "inputs" / "001.png"));
    EXPECT_EQ(in1.status, Models::FileStatus::Pending);       // not rendered, in this copy
    EXPECT_TRUE(in1.contributesTo.empty());
    EXPECT_TRUE(in1.thumbnailPath.empty());
    EXPECT_EQ(p1.getInputImages()[2].filePath, g(root / "inputs" / "003.png"));
    EXPECT_EQ(p1.getStripOverlays()[0].assetPath, g(root / "overlays" / "ovl-1.svg"));
    EXPECT_EQ(p1.getStripOverlays()[1].assetPath, g(root / "overlays" / "ovl-1.svg"));
    EXPECT_EQ(p1.getStripOverlays()[1].anchorInputUid, "in-2");

    const auto& p2 = copy.projectItems[1];
    EXPECT_EQ(p2.getInputImages()[0].filePath, g(root / "inputs" / "001 (2).png"));
    EXPECT_EQ(p2.getInputImages()[1].filePath, g(root / "inputs" / "001.png"));   // the shared page, once

    EXPECT_EQ(copy.canvasProfiles().at(0).templateInfo.path, "templates/3p-m.png");
}

TEST(WorkspacePackager, TheArchiveHeadersMatchTheLibraryThatShips)
{
    // The headers are fetched separately from the DLL (see the root CMakeLists.txt); a mismatch would
    // describe structures and calls the library does not have.
    EXPECT_EQ(archive_version_number(), ARCHIVE_VERSION_NUMBER);
}

} // namespace Platemaker::Infrastructure
