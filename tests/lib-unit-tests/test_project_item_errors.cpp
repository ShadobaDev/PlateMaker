/**
 * \file
 * \brief Unit tests for the post-render error path: FileStatus::Error, the typed failures returned by
 *        ProjectItem::applyProcessingResults(), sanitize() stickiness/recovery, and serialization.
 *
 * Regression guard for the silent infinite loop: an input that was rendered but could not be hashed
 * afterwards (computeFileSha256 == "") must NOT be left Pending — it is marked FileStatus::Error and
 * returned as a typed InputHashFailed failure, so it stops forcing a full re-render on every run.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/processing_error.hpp>
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
using Models::ProcessingErrorCategory;
using Models::ProcessingErrorCode;
using Models::ProjectItem;

/// A project with one input pointing at \p path (status Pending, no baseline).
ProjectItem projectWithInput(const std::string& path)
{
    ProjectItem p;
    p.name = "Chapter";
    InputFile inf;
    inf.uid      = "u0";
    inf.filePath = path;
    inf.order    = 0;
    inf.status   = FileStatus::Pending;
    p.getInputImages().push_back(std::move(inf));
    return p;
}

// A rendered input whose hash fails afterwards → Error + a returned typed failure (not left Pending).
TEST(PostRenderErrorTest, UnhashableInputBecomesErrorAndIsReturned)
{
    // A path that does not exist makes computeFileSha256() return "" — the same empty result a locked /
    // permission-denied / offline file produces. applyProcessingResults() does not check existence, so
    // this exercises exactly the post-render hash-failure branch.
    ProjectItem p = projectWithInput("does-not-exist-after-render.png");

    const auto errors = p.applyProcessingResults(
        /*records*/ {}, /*applied*/ {}, /*skipped*/ {}, /*workspaceProfiles*/ {},
        /*outDir*/ "out", /*ts*/ "2026-08-05T00:00:00Z");

    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].code,     ProcessingErrorCode::InputHashFailed);
    EXPECT_EQ(errors[0].category, ProcessingErrorCategory::Io);
    EXPECT_EQ(errors[0].file,     "does-not-exist-after-render.png");

    const InputFile& inf = p.getInputImages().front();
    EXPECT_EQ(inf.status, FileStatus::Error);       // NOT Pending — this is the loop fix
    EXPECT_TRUE(inf.sha256.empty());                // no verification baseline
    EXPECT_EQ(inf.lastProcessed, "2026-08-05T00:00:00Z"); // it *was* rendered
}

// A deliberately skipped input stays Skipped and is not reported as a post-render error.
TEST(PostRenderErrorTest, SkippedInputIsNotAnError)
{
    ProjectItem p = projectWithInput("skipped.png");

    const auto errors = p.applyProcessingResults(
        /*records*/ {}, /*applied*/ {}, /*skipped*/ {std::string("skipped.png")},
        /*workspaceProfiles*/ {}, /*outDir*/ "out", /*ts*/ "2026-08-05T00:00:00Z");

    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(p.getInputImages().front().status, FileStatus::Skipped);
}

// inputsAllProcessed() must treat Error as settled, or the project would force a full re-render forever.
TEST(PostRenderErrorTest, InputsAllProcessedTreatsErrorAsSettled)
{
    ProjectItem p;
    InputFile a; a.uid = "u0"; a.status = FileStatus::Processed; p.getInputImages().push_back(a);
    InputFile b; b.uid = "u1"; b.status = FileStatus::Error;     p.getInputImages().push_back(b);

    EXPECT_TRUE(p.inputsAllProcessed());
}

// sanitize() keeps an Error sticky while the file is unreadable, then recovers it once it can be hashed.
TEST(PostRenderErrorTest, SanitizeRecoversErrorWhenFileBecomesReadable)
{
    const fs::path dir = fs::temp_directory_path() / "pm-posterror-recover";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = (dir / "page.bin").string();
    { std::ofstream(file, std::ios::binary) << "content"; } // a real, readable file

    ProjectItem p = projectWithInput(file);
    // Simulate the post-render state: rendered but unverifiable.
    p.getInputImages().front().status = FileStatus::Error;
    p.getInputImages().front().sha256.clear();

    p.sanitize(/*workspaceProfiles*/ {});

    const InputFile& inf = p.getInputImages().front();
    EXPECT_EQ(inf.status, FileStatus::Processed) << "readable again → recovered";
    EXPECT_FALSE(inf.sha256.empty())             << "baseline adopted on recovery";

    fs::remove_all(dir);
}

// An Error input whose file is genuinely gone becomes Missing (the pre-render disk check wins).
TEST(PostRenderErrorTest, SanitizeErrorWithMissingFileBecomesMissing)
{
    ProjectItem p = projectWithInput("truly-gone.png");
    p.getInputImages().front().status = FileStatus::Error;

    p.sanitize(/*workspaceProfiles*/ {});

    EXPECT_EQ(p.getInputImages().front().status, FileStatus::Missing);
}

// FileStatus::Error round-trips through the workspace serializer.
TEST(PostRenderErrorTest, ErrorStatusSurvivesSaveLoad)
{
    const fs::path dir = fs::temp_directory_path() / "pm-posterror-serial";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string wsFile = (dir / "ws.platemaker.json").string();

    Models::Workspace ws;
    ws.version = 2;
    {
        ProjectItem p = projectWithInput("page.png");
        p.getInputImages().front().status = FileStatus::Error;
        ws.projectItems.push_back(std::move(p));
    }

    Infrastructure::WorkspaceSerializer{}.save(ws, wsFile);
    const Models::Workspace loaded = Infrastructure::WorkspaceSerializer{}.load(wsFile);

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    ASSERT_EQ(loaded.projectItems.front().getInputImages().size(), 1u);
    EXPECT_EQ(loaded.projectItems.front().getInputImages().front().status, FileStatus::Error);

    fs::remove_all(dir);
}

} // namespace
} // namespace Platemaker
