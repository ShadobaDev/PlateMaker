/**
 * \file tests/lib-fault-tests/test_file_failures.cpp
 * \brief Files that cannot be written or read whole, archives that fail mid-way, paths that cannot be
 *        resolved: what the caller is told, and that nothing half-made is left behind.
 *
 * A full disk is /dev/full where the library opens the file itself, a failing write() or read() (fault.hpp)
 * where it opens it in a folder of its own; a path that cannot be resolved is a symbolic link to itself.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "fault.hpp"

#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/profile_bundle_serializer/profile_bundle_serializer.hpp>
#include <platemaker/infrastructure/workspace_packager/workspace_packager.hpp>
#include <platemaker/models/workspace.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace Platemaker::Infrastructure {
namespace {

namespace fs = std::filesystem;
using Fault::thrown;

class FileFailureTest : public FaultTest {};

void writeFile(const std::string& path, const std::string& bytes)
{
    std::ofstream(path, std::ios::binary) << bytes;
}

/// A plan of one file and the workspace copy.
PackagePlan onePagePlan(const Fault::Scratch& scratch)
{
    writeFile(scratch.file("page.png"), std::string(1000, 'p'));
    PackagePlan plan;
    plan.workspaceFileName = "Chapter.platemaker.json";
    plan.workspaceJson     = "{}";
    plan.files.push_back({scratch.file("page.png"), "inputs/page.png"});
    return plan;
}

// --- Writing a package ----------------------------------------------------------------------------------

TEST_F(FileFailureTest, APackageThatFailsMidWayNamesTheEntryAndLeavesNothing)
{
    const Fault::Scratch scratch;
    const PackagePlan    plan = onePagePlan(scratch);
    const std::string    zip  = scratch.file("out.zip");

    for (const char* function : {"archive_write_header", "archive_write_data"}) {
        SCOPED_TRACE(function);
        Fault::failCall(function);
        const std::string message = thrown([&] { (void)WorkspacePackager::write(plan, zip); });
        EXPECT_CONTAINS(message, "inputs/page.png");
        EXPECT_FALSE(fs::exists(zip));
        EXPECT_FALSE(fs::exists(zip + ".tmp"));
    }
}

TEST_F(FileFailureTest, APackageThatCannotBeOpenedSaysWhyFromLibarchive)
{
    const Fault::Scratch scratch;
    const std::string    zip = scratch.file("no such folder/out.zip");

    const std::string message = thrown([&] { (void)WorkspacePackager::write(onePagePlan(scratch), zip); });
    EXPECT_CONTAINS(message, "WorkspacePackager::write() — " + scratch.file("no such folder/out.zip.tmp") + ": ");
    EXPECT_EQ(message.find("archive error"), std::string::npos);   // libarchive's own reason
}

// --- Unpacking one --------------------------------------------------------------------------------------

TEST_F(FileFailureTest, AnEntryThatCannotBeWrittenFailsTheUnpackAndLeavesNothing)
{
    const Fault::Scratch scratch;
    const std::string    zip = scratch.file("in.zip");
    ASSERT_TRUE(WorkspacePackager::write(onePagePlan(scratch), zip));
    const int before = Fault::calls("write");
    ASSERT_TRUE(WorkspacePackager::unpack(zip, scratch.file("whole")));
    const int writes = Fault::calls("write") - before;
    ASSERT_GT(writes, 0);

    // The disk fills at each write in turn — the last one too, which only closing the file makes.
    for (int n = 1; n <= writes; ++n) {
        SCOPED_TRACE(n);
        const std::string folder = scratch.file("unpacked");
        Fault::failCall("write", n);
        EXPECT_CONTAINS(thrown([&] { (void)WorkspacePackager::unpack(zip, folder); }), "cannot write");
        EXPECT_FALSE(fs::exists(folder));
        EXPECT_FALSE(fs::exists(folder + ".partial"));
    }
}

TEST_F(FileFailureTest, WithoutAUtf8LocaleAnAsciiPackageStillUnpacks)
{
    const Fault::Scratch scratch;
    const std::string    zip = scratch.file("in.zip");
    ASSERT_TRUE(WorkspacePackager::write(onePagePlan(scratch), zip));

    Fault::failCall("newlocale");   // no C.UTF-8 on this system: the thread keeps the locale it had
    ASSERT_TRUE(WorkspacePackager::unpack(zip, scratch.file("out")));
    EXPECT_TRUE(fs::exists(scratch.file("out/inputs/page.png")));
}

TEST_F(FileFailureTest, AnEntryNameReadsInTheLocaleWhenNotInUtf8AndIsRefusedWhenNotAtAll)
{
    const Fault::Scratch scratch;
    const std::string    zip = scratch.file("in.zip");
    ASSERT_TRUE(WorkspacePackager::write(onePagePlan(scratch), zip));

    Fault::failCall("archive_entry_pathname_utf8");   // the first entry's name, in the locale's form instead
    ASSERT_TRUE(WorkspacePackager::unpack(zip, scratch.file("out")));
    EXPECT_TRUE(fs::exists(scratch.file("out/inputs/page.png")));

    Fault::failCall("archive_entry_pathname_utf8");
    Fault::failCall("archive_entry_pathname");        // and no name at all
    EXPECT_CONTAINS(thrown([&] { (void)WorkspacePackager::unpack(zip, scratch.file("out2")); }),
                    "'' would land outside the folder");
    EXPECT_FALSE(fs::exists(scratch.file("out2")));
}

TEST_F(FileFailureTest, AnArchiveErrorWithoutAReasonSaysSo)
{
    const Fault::Scratch scratch;
    writeFile(scratch.file("text.zip"), "no archive here");

    Fault::failCall("archive_error_string");
    EXPECT_CONTAINS(thrown([&] { (void)WorkspacePackager::unpack(scratch.file("text.zip"), scratch.file("out")); }),
                    ": archive error");
}

// --- Planning one ---------------------------------------------------------------------------------------

TEST_F(FileFailureTest, PathsThatCannotBeResolvedArePlannedAsSpelled)
{
    const Fault::Scratch scratch;
    const fs::path       loop = scratch.file("loop");
    fs::create_symlink(loop, loop);   // resolving it never ends

    Models::Workspace   workspace;
    Models::ProjectItem project;
    project.uid = "proj-1";
    Models::InputFile page;
    page.uid      = "in-1";
    page.filePath = pathToUtf8(loop / "page.png");
    project.getInputImages() = {page};
    workspace.projectItems.push_back(std::move(project));

    const PackagePlan plan = WorkspacePackager::plan(workspace, pathToUtf8(loop / "Chapter.platemaker.json"));
    ASSERT_EQ(plan.missing.size(), 1u);   // not a file it can read: missing, under the name it was given
    EXPECT_EQ(plan.missing[0].source, page.filePath);
}

// --- Replacing a file whole -------------------------------------------------------------------------------

TEST_F(FileFailureTest, AFileThatCannotBeWrittenWholeIsNotReplaced)
{
    const Fault::Scratch scratch;
    const std::string    bundle = scratch.file("palette.platemaker.profiles.json");
    writeFile(bundle, "old");
    fs::create_symlink("/dev/full", bundle + ".tmp");   // the temporary file lands on a full disk

    EXPECT_CONTAINS(thrown([&] { ProfileBundleSerializer{}.save({}, {}, bundle); }), "write error");
    EXPECT_FALSE(fs::exists(fs::symlink_status(bundle + ".tmp")));
    std::ifstream in(bundle);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), {}), "old");
}

// --- Hashing a file ---------------------------------------------------------------------------------------

TEST_F(FileFailureTest, AFileThatCannotBeReadWholeHasNoHash)
{
    const Fault::Scratch scratch;
    const std::string    file = scratch.file("big.bin");
    writeFile(file, std::string(256 * 1024, 'b'));
    ASSERT_FALSE(FileMetaData::computeFileSha256(file).empty());

    Fault::failCall("read", 2);   // the first block is read, the second is not
    EXPECT_EQ(FileMetaData::computeFileSha256(file), "");
}

} // namespace
} // namespace Platemaker::Infrastructure
