/**
 * \file tests/lib-unit-tests/test_workspace_packager.cpp
 * \brief WorkspacePackager::plan() — every referenced file placed once, the copy rewritten to name the
 *        places, nothing a render reproduces carried, and the copy loading back from the package root.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <archive.h>
#include <archive_entry.h>

#ifndef _WIN32
#include <locale.h>   // newlocale(), uselocale()
#endif
#include <nlohmann/json.hpp>

#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
#include <platemaker/infrastructure/workspace_packager/workspace_packager.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

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

std::string readText(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

/// Unpacks \p zip into \p dir with libarchive's reader — an independent check of what write() wrote —
/// and returns the entry names in archive order.
std::vector<std::string> unzip(const fs::path& zip, const fs::path& dir)
{
    std::vector<std::string> names;
#ifndef _WIN32
    // As WorkspacePackager::unpack() reads names: this thread in a UTF-8 locale, or libarchive drops the
    // non-ASCII ones in the test process's "C" locale.
    const locale_t utf8     = newlocale(LC_CTYPE_MASK, "C.UTF-8", locale_t{});
    const locale_t previous = uselocale(utf8);
#endif
    archive* a = archive_read_new();
    archive_read_support_format_zip(a);
#ifdef _WIN32
    EXPECT_EQ(archive_read_open_filename_w(a, zip.c_str(), 1 << 16), ARCHIVE_OK) << archive_error_string(a);
#else
    EXPECT_EQ(archive_read_open_filename(a, zip.c_str(), 1 << 16), ARCHIVE_OK) << archive_error_string(a);
#endif
    archive_entry* e = nullptr;
    while (archive_read_next_header(a, &e) == ARCHIVE_OK) {
        const char* utf8 = archive_entry_pathname_utf8(e);
        names.emplace_back(utf8 ? utf8 : "");
        const fs::path out = dir / utf8ToPath(names.back());
        fs::create_directories(out.parent_path());
        std::ofstream f(out, std::ios::binary);
        const void* buf = nullptr;
        size_t      size = 0;
        la_int64_t  offset = 0;
        while (archive_read_data_block(a, &buf, &size, &offset) == ARCHIVE_OK)
            f.write(static_cast<const char*>(buf), static_cast<std::streamsize>(size));
    }
    archive_read_free(a);
#ifndef _WIN32
    uselocale(previous);
    freelocale(utf8);
#endif
    return names;
}

/// A small workspace under \p root: one project, two pages (one missing, one with a non-ASCII name), one overlay.
PackagePlan smallPlan(const fs::path& root)
{
    const fs::path ws = root / "ws";
    touchFile(root / "scans" / utf8ToPath("żółw.png"), "page-bytes");
    touchFile(ws / "overlays" / "ovl-1.svg", "<svg/>");

    Models::Workspace   workspace;
    Models::ProjectItem p;
    p.name             = "One";
    p.uid              = "proj-1";
    p.getInputImages() = {page("in-1", root / "scans" / utf8ToPath("żółw.png"), 0), page("in-2", root / "gone.png", 1)};
    p.getStripOverlays() = {overlay("ovl-1", ws / "overlays" / "ovl-1.svg", "in-1")};
    workspace.projectItems.push_back(std::move(p));

    PackagePlan plan = WorkspacePackager::plan(workspace, pathToUtf8(ws / "Chapter.platemaker.json"));
    plan.applicationName     = "Platemaker";
    plan.applicationVersion  = "9.9.9";
    plan.applicationManifest = R"({"fonts": ["Comic.ttf"]})";
    return plan;
}

/// A zip with exactly these entries, written with libarchive directly — for what write() would refuse to make.
void zipWith(const fs::path& zip, const std::vector<std::pair<std::string, std::string>>& entries)
{
    archive* a = archive_write_new();
    archive_write_set_format_zip(a);
#ifdef _WIN32
    ASSERT_EQ(archive_write_open_filename_w(a, zip.c_str()), ARCHIVE_OK);
#else
    ASSERT_EQ(archive_write_open_filename(a, zip.c_str()), ARCHIVE_OK);
#endif
    for (const auto& [name, content] : entries) {
        archive_entry* e = archive_entry_new();
        archive_entry_set_pathname_utf8(e, name.c_str());
        archive_entry_set_size(e, static_cast<la_int64_t>(content.size()));
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, 0644);
        archive_write_header(a, e);
        archive_write_data(a, content.data(), content.size());
        archive_entry_free(e);
    }
    archive_write_free(a);
}

/// One archive entry of a given type, for what zipWith() (regular files only) cannot express.
struct ZipEntry {
    std::string    name;
    std::string    content;
    unsigned int   type = AE_IFREG;   //!< AE_IFREG, AE_IFDIR or AE_IFLNK.
};

/// A zip of \p entries written with libarchive directly; a symlink's content is its target.
void zipWithKinds(const fs::path& zip, const std::vector<ZipEntry>& entries)
{
    archive* a = archive_write_new();
    archive_write_set_format_zip(a);
#ifdef _WIN32
    ASSERT_EQ(archive_write_open_filename_w(a, zip.c_str()), ARCHIVE_OK);
#else
    ASSERT_EQ(archive_write_open_filename(a, zip.c_str()), ARCHIVE_OK);
#endif
    for (const auto& en : entries) {
        archive_entry* e = archive_entry_new();
        archive_entry_set_pathname_utf8(e, en.name.c_str());
        archive_entry_set_filetype(e, en.type);
        archive_entry_set_perm(e, en.type == AE_IFDIR ? 0755 : 0644);
        if (en.type == AE_IFLNK)
            archive_entry_set_symlink(e, en.content.c_str());
        archive_entry_set_size(e, en.type == AE_IFREG ? static_cast<la_int64_t>(en.content.size()) : 0);
        archive_write_header(a, e);
        if (en.type == AE_IFREG)
            archive_write_data(a, en.content.data(), en.content.size());
        archive_entry_free(e);
    }
    archive_write_free(a);
}

} // anonymous namespace

TEST(WorkspacePackager, APackageUnpacksIntoANewFolderAndSaysWhatItHolds)
{
    const Scratch  s("unpack");
    const fs::path zip = s.root / "Chapter.platemaker.zip";
    ASSERT_TRUE(WorkspacePackager::write(smallPlan(s.root), pathToUtf8(zip)));

    const fs::path folder   = s.root / "there" / utf8ToPath("Rozdział");
    const auto     unpacked = WorkspacePackager::unpack(pathToUtf8(zip), pathToUtf8(folder));
    ASSERT_TRUE(unpacked.has_value());
    EXPECT_EQ(unpacked->workspaceFile, g(folder / "Chapter.platemaker.json"));
    EXPECT_FALSE(fs::exists(s.root / "there" / utf8ToPath("Rozdział.partial")));
    EXPECT_EQ(readText(folder / "inputs" / utf8ToPath("żółw.png")), "page-bytes");

    const PackageManifest& m = unpacked->manifest;
    EXPECT_EQ(m.format, 1);
    EXPECT_EQ(m.workspaceFileName, "Chapter.platemaker.json");
    EXPECT_EQ(m.applicationName, "Platemaker");
    EXPECT_EQ(m.applicationVersion, "9.9.9");
    EXPECT_FALSE(m.libraryVersion.empty());
    EXPECT_EQ(nlohmann::json::parse(m.applicationDetails)["fonts"][0], "Comic.ttf");
    ASSERT_EQ(m.missing.size(), 1u);
    EXPECT_EQ(m.missing[0].target, "inputs/gone.png");

    // It opens, pointing into the new folder.
    const auto ws = WorkspaceSerializer{}.load(unpacked->workspaceFile);
    EXPECT_EQ(ws.projectItems.at(0).getInputImages().at(0).filePath, g(folder / "inputs" / utf8ToPath("żółw.png")));

    // A folder that exists is never unpacked into — not even the one just made.
    EXPECT_THROW((void)WorkspacePackager::unpack(pathToUtf8(zip), pathToUtf8(folder)), std::runtime_error);
}

TEST(WorkspacePackager, LeftoversAndUnreadableZipsAreRefusedUpFront)
{
    const Scratch  s("unpack-upfront");
    const fs::path zip = s.root / "p.platemaker.zip";
    ASSERT_TRUE(WorkspacePackager::write(smallPlan(s.root), pathToUtf8(zip)));

    // An earlier unpack that died half-way left its ".partial" folder: never unpack over it.
    fs::create_directories(s.root / "out.partial");
    EXPECT_THROW((void)WorkspacePackager::unpack(pathToUtf8(zip), pathToUtf8(s.root / "out")), std::runtime_error);
    EXPECT_TRUE(fs::exists(s.root / "out.partial")) << "the leftover is the user's to inspect";
    EXPECT_FALSE(fs::exists(s.root / "out"));

    // A zip that is not there.
    EXPECT_THROW((void)WorkspacePackager::unpack(pathToUtf8(s.root / "absent.zip"), pathToUtf8(s.root / "out2")),
                 std::runtime_error);
    EXPECT_FALSE(fs::exists(s.root / "out2.partial"));

    // A file that is not a zip at all.
    touchFile(s.root / "text.zip", "just some text, no archive here");
    EXPECT_THROW((void)WorkspacePackager::unpack(pathToUtf8(s.root / "text.zip"), pathToUtf8(s.root / "out3")),
                 std::runtime_error);
    EXPECT_FALSE(fs::exists(s.root / "out3"));
    EXPECT_FALSE(fs::exists(s.root / "out3.partial"));
}

TEST(WorkspacePackager, FolderEntriesAreSkippedAndProgressReachesTheZipSize)
{
    const Scratch  s("unpack-dirs");
    const fs::path zip = s.root / "p.zip";
    zipWithKinds(zip, {{"inputs/", "", AE_IFDIR},
                       {"inputs/page.png", "page-bytes"},
                       {"w.platemaker.json", "{}"},
                       {"package.json", R"({"format": 1, "workspace": "w.platemaker.json"})"}});

    std::uint64_t lastDone = 0, lastTotal = 0;
    int           calls    = 0;
    const auto unpacked = WorkspacePackager::unpack(
        pathToUtf8(zip), pathToUtf8(s.root / "out"),
        [&](std::uint64_t done, std::uint64_t total) { lastDone = done; lastTotal = total; ++calls; });
    ASSERT_TRUE(unpacked.has_value());
    EXPECT_EQ(readText(s.root / "out" / "inputs" / "page.png"), "page-bytes");
    EXPECT_GT(calls, 0);
    EXPECT_EQ(lastTotal, fs::file_size(zip)) << "the total is the zip's own size";
    EXPECT_LE(lastDone, lastTotal);
}

TEST(WorkspacePackager, AManifestWithOddOptionalPartsStillUnpacks)
{
    // Only "workspace" is required. Optional parts of the wrong shape are ignored, not trusted.
    const Scratch  s("unpack-odd-manifest");
    const fs::path zip = s.root / "p.zip";
    zipWith(zip, {{"w.platemaker.json", "{}"},
                  {"package.json", R"({"format": 1, "workspace": "w.platemaker.json",
                                       "library": "not an object",
                                       "application": {"name": "Tool", "details": "not an object"},
                                       "missing": ["not an object", {"target": "inputs/a.png"}]})"}});

    const auto unpacked = WorkspacePackager::unpack(pathToUtf8(zip), pathToUtf8(s.root / "out"));
    ASSERT_TRUE(unpacked.has_value());
    const PackageManifest& m = unpacked->manifest;
    EXPECT_TRUE(m.libraryVersion.empty());
    EXPECT_EQ(m.applicationName, "Tool");
    EXPECT_EQ(m.applicationDetails, "{}");
    ASSERT_EQ(m.missing.size(), 1u) << "only the object entry is a missing-file record";
    EXPECT_EQ(m.missing[0].target, "inputs/a.png");
}

TEST(WorkspacePackager, EntriesThatAreNotPlainFilesOrCollideAreRefused)
{
    const Scratch s("unpack-kinds");
    const std::string manifest = R"({"format": 1, "workspace": "w.platemaker.json"})";
    const auto refused = [&](const char* zipName, const std::vector<ZipEntry>& entries) {
        const fs::path zip    = s.root / zipName;
        const fs::path folder = s.root / (std::string(zipName) + "-out");
        zipWithKinds(zip, entries);
        EXPECT_THROW((void)WorkspacePackager::unpack(pathToUtf8(zip), pathToUtf8(folder)), std::runtime_error)
            << zipName;
        EXPECT_FALSE(fs::exists(folder)) << zipName;
        EXPECT_FALSE(fs::exists(s.root / (std::string(zipName) + "-out.partial"))) << zipName;
    };

    // A symlink could point anywhere once unpacked.
    refused("link.zip", {{"w.platemaker.json", "{}"}, {"inputs", "/etc", AE_IFLNK}, {"package.json", manifest}});
    // "a" arrives as a file after "a/b.png" already made it a folder: it cannot be written.
    refused("file-over-folder.zip", {{"w.platemaker.json", "{}"}, {"a/b.png", "1"}, {"a", "2"},
                                     {"package.json", manifest}});
    // A backslash is a separator on Windows: such a name could climb out of the folder there.
    refused("backslash.zip", {{"w.platemaker.json", "{}"}, {"inputs\\..\\..\\evil.png", "x"},
                              {"package.json", manifest}});
    // The manifest's workspace name is checked like an entry name: a backslash is refused.
    refused("backslash-workspace.zip", {{"w.platemaker.json", "{}"},
                                        {"package.json", R"({"format": 1, "workspace": "sub\\w.platemaker.json"})"}});
    // A manifest that is valid JSON but not an object.
    refused("array-manifest.zip", {{"w.platemaker.json", "{}"}, {"package.json", "[1, 2, 3]"}});
}

TEST(WorkspacePackager, AnEntryWhoseDataIsCorruptIsRefused)
{
    // Compressible content is deflated; overwrite the start of its compressed bytes so inflating fails.
    const Scratch  s("unpack-corrupt");
    const fs::path zip = s.root / "p.zip";
    zipWith(zip, {{"w.platemaker.json", std::string(4096, 'A')},
                  {"package.json", R"({"format": 1, "workspace": "w.platemaker.json"})"}});

    std::string bytes = readText(zip);
    ASSERT_GT(bytes.size(), 64u);
    ASSERT_EQ(bytes.compare(0, 4, "PK\x03\x04"), 0) << "a local file header first";
    const auto le16 = [&](std::size_t at) {
        return static_cast<std::size_t>(static_cast<unsigned char>(bytes[at])) |
               static_cast<std::size_t>(static_cast<unsigned char>(bytes[at + 1])) << 8;
    };
    const std::size_t data = 30 + le16(26) + le16(28);   // header + name + extra field
    for (std::size_t i = 0; i < 8; ++i)
        bytes[data + i] = static_cast<char>(0xFF);
    { std::ofstream(zip, std::ios::binary | std::ios::trunc) << bytes; }

    EXPECT_THROW((void)WorkspacePackager::unpack(pathToUtf8(zip), pathToUtf8(s.root / "out")), std::runtime_error);
    EXPECT_FALSE(fs::exists(s.root / "out"));
    EXPECT_FALSE(fs::exists(s.root / "out.partial"));
}

TEST(WorkspacePackager, AnArchiveWhoseLaterHeaderIsBrokenIsRefused)
{
    // The first entry reads fine; the second local header has lost its signature, so the reader fails
    // between entries. The partly-unpacked folder must go with it.
    const Scratch  s("unpack-badheader");
    const fs::path zip = s.root / "p.zip";
    zipWith(zip, {{"first.png", "first"}, {"w.platemaker.json", "{}"},
                  {"package.json", R"({"format": 1, "workspace": "w.platemaker.json"})"}});
    std::string bytes = readText(zip);
    const auto second = bytes.find("PK\x03\x04", 4);
    ASSERT_NE(second, std::string::npos);
    bytes.replace(second, 4, "XXXX");
    { std::ofstream(zip, std::ios::binary | std::ios::trunc) << bytes; }

    EXPECT_THROW((void)WorkspacePackager::unpack(pathToUtf8(zip), pathToUtf8(s.root / "out")), std::runtime_error);
    EXPECT_FALSE(fs::exists(s.root / "out"));
    EXPECT_FALSE(fs::exists(s.root / "out.partial"));
}

TEST(WorkspacePackager, ASourceThatChangesWhileItIsPackedLeavesNothing)
{
    // The sizes are taken up front; a source deleted or truncated before its turn comes must be refused
    // and the half-written package removed. The progress callback, fired after the first file, is where
    // the "other program" strikes.
    for (const bool truncate : {false, true}) {
        const Scratch  s(truncate ? "write-truncated" : "write-vanished");
        PackagePlan    plan = smallPlan(s.root);
        const fs::path second = s.root / "second.png";
        touchFile(second, std::string(1000, 'p'));
        plan.files.push_back({pathToUtf8(second), "inputs/second.png"});

        const fs::path zip  = s.root / "p.platemaker.zip";
        bool           done = false;
        const auto     strike = [&](std::uint64_t, std::uint64_t) {
            if (done) return;
            done = true;
            if (truncate) touchFile(second, "short");
            else          fs::remove(second);
        };
        EXPECT_THROW((void)WorkspacePackager::write(plan, pathToUtf8(zip), strike), std::runtime_error)
            << (truncate ? "truncated" : "deleted");
        EXPECT_FALSE(fs::exists(zip));
        EXPECT_FALSE(fs::exists(s.root / "p.platemaker.zip.tmp")) << "the temporary package is removed";
    }
}

TEST(WorkspacePackager, APackageThatCannotReplaceItsDestinationIsRefused)
{
    // The whole zip is written, but a non-empty folder stands where it must go: neither the rename nor
    // the copy fallback can replace it, and the temporary package must not be left behind.
    const Scratch  s("write-blocked");
    const fs::path zip = s.root / "p.platemaker.zip";
    fs::create_directories(zip / "occupied");

    EXPECT_THROW((void)WorkspacePackager::write(smallPlan(s.root), pathToUtf8(zip)), std::runtime_error);
    EXPECT_TRUE(fs::is_directory(zip));
    EXPECT_FALSE(fs::exists(s.root / "p.platemaker.zip.tmp"));
}

TEST(WorkspacePackager, AnUnpackThatCannotFinishLeavesNothing)
{
    const Scratch s("unpack-refuse");
    const auto    refused = [&](const char* zipName, const std::vector<std::pair<std::string, std::string>>& entries) {
        const fs::path zip    = s.root / zipName;
        const fs::path folder = s.root / (std::string(zipName) + "-out");
        zipWith(zip, entries);
        EXPECT_THROW((void)WorkspacePackager::unpack(pathToUtf8(zip), pathToUtf8(folder)), std::runtime_error)
            << zipName;
        EXPECT_FALSE(fs::exists(folder)) << zipName;
        EXPECT_FALSE(fs::exists(s.root / (std::string(zipName) + "-out.partial"))) << zipName;
        EXPECT_FALSE(fs::exists(s.root / "evil.txt")) << zipName;
    };
    const std::string manifest = R"({"format": 1, "workspace": "w.platemaker.json"})";

    refused("escapes.zip", {{"w.platemaker.json", "{}"}, {"../evil.txt", "x"}, {"package.json", manifest}});
    refused("rooted.zip", {{"w.platemaker.json", "{}"}, {"/evil.txt", "x"}, {"package.json", manifest}});
    refused("twice.zip", {{"w.platemaker.json", "{}"}, {"a.png", "1"}, {"A.PNG", "2"}, {"package.json", manifest}});
    refused("no-manifest.zip", {{"w.platemaker.json", "{}"}});
    refused("no-workspace.zip", {{"package.json", manifest}});
    refused("nested-workspace.zip", {{"sub/w.platemaker.json", "{}"},
                                     {"package.json", R"({"format": 1, "workspace": "sub/w.platemaker.json"})"}});
}

TEST(WorkspacePackager, ACancelledUnpackLeavesNothing)
{
    const Scratch  s("unpack-cancel");
    const fs::path zip = s.root / "p.platemaker.zip";
    ASSERT_TRUE(WorkspacePackager::write(smallPlan(s.root), pathToUtf8(zip)));

    CancellationToken cancel;
    cancel.cancel();
    EXPECT_FALSE(WorkspacePackager::unpack(pathToUtf8(zip), pathToUtf8(s.root / "out"), {}, &cancel).has_value());
    EXPECT_FALSE(fs::exists(s.root / "out"));
    EXPECT_FALSE(fs::exists(s.root / "out.partial"));
}

TEST(WorkspacePackager, APackageUnpacksAndOpensWithItsManifestLast)
{
    const Scratch s("write");
    PackagePlan   plan = smallPlan(s.root);
    // What a consumer adds: a file of its own, named by nothing in the model.
    touchFile(s.root / "fonts" / "Comic.ttf", "font-bytes");
    plan.files.push_back({g(s.root / "fonts" / "Comic.ttf"), "fonts/Comic.ttf"});

    std::uint64_t lastDone = 0, lastTotal = 0;
    const fs::path zip = s.root / "out" / "Chapter.platemaker.zip";
    fs::create_directories(zip.parent_path());
    ASSERT_TRUE(WorkspacePackager::write(plan, pathToUtf8(zip),
                                         [&](std::uint64_t done, std::uint64_t total) {
                                             EXPECT_GE(done, lastDone);
                                             lastDone  = done;
                                             lastTotal = total;
                                         }));
    EXPECT_EQ(lastDone, lastTotal);
    EXPECT_FALSE(fs::exists(zip.parent_path() / "Chapter.platemaker.zip.tmp"));

    const fs::path root  = s.root / "unpacked";
    const auto     names = unzip(zip, root);
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.back(), k_packageManifestFileName);
    EXPECT_EQ(readText(root / "inputs" / utf8ToPath("żółw.png")), "page-bytes");   // the bytes, and a UTF-8 name
    EXPECT_EQ(readText(root / "fonts" / "Comic.ttf"), "font-bytes");
    EXPECT_FALSE(fs::exists(root / "inputs" / "gone.png"));

    const auto copy = WorkspaceSerializer{}.load(pathToUtf8(root / "Chapter.platemaker.json"));
    ASSERT_EQ(copy.projectItems.size(), 1u);
    EXPECT_EQ(copy.projectItems[0].getInputImages()[0].filePath, g(root / "inputs" / utf8ToPath("żółw.png")));
    EXPECT_EQ(copy.projectItems[0].getStripOverlays()[0].assetPath, g(root / "overlays" / "ovl-1.svg"));

    const auto manifest = nlohmann::json::parse(readText(root / k_packageManifestFileName));
    EXPECT_EQ(manifest["workspace"], "Chapter.platemaker.json");
    EXPECT_EQ(manifest["library"]["name"], "libplatemaker");
    EXPECT_FALSE(manifest["library"]["version"].get<std::string>().empty());
    EXPECT_EQ(manifest["application"]["name"], "Platemaker");
    EXPECT_EQ(manifest["application"]["version"], "9.9.9");
    EXPECT_EQ(manifest["application"]["details"]["fonts"][0], "Comic.ttf");
    ASSERT_EQ(manifest["missing"].size(), 1u);
    EXPECT_EQ(manifest["missing"][0]["target"], "inputs/gone.png");
    EXPECT_FALSE(manifest["exported"].get<std::string>().empty());
}

TEST(WorkspacePackager, APlanThatCannotBeWrittenIsRefusedBeforeAnythingIs)
{
    const Scratch  s("refuse");
    const fs::path zip = s.root / "p.platemaker.zip";

    const auto refused = [&](PackagePlan plan) {
        EXPECT_THROW(WorkspacePackager::write(plan, pathToUtf8(zip)), std::runtime_error);
        EXPECT_FALSE(fs::exists(zip));
        EXPECT_FALSE(fs::exists(s.root / "p.platemaker.zip.tmp"));
    };

    PackagePlan escaping = smallPlan(s.root);
    escaping.files.push_back({escaping.files[0].source, "../outside.png"});
    refused(escaping);

    PackagePlan rooted = smallPlan(s.root);
    rooted.files.push_back({rooted.files[0].source, "/etc/x.png"});
    refused(rooted);

    PackagePlan twice = smallPlan(s.root);
    twice.files.push_back({twice.files[0].source, "OVERLAYS/ovl-1.svg"});   // the same place, another case
    refused(twice);

    PackagePlan notAnObject = smallPlan(s.root);
    notAnObject.applicationManifest = "[1, 2]";
    refused(notAnObject);

    PackagePlan unreadable = smallPlan(s.root);
    unreadable.files.push_back({g(s.root / "not-there.png"), "inputs/not-there.png"});
    refused(unreadable);
}

TEST(WorkspacePackager, ACancelledPackageLeavesNothingBehind)
{
    const Scratch  s("cancel");
    const fs::path zip = s.root / "p.platemaker.zip";
    touchFile(zip, "an older package");   // must survive a cancelled export untouched

    CancellationToken cancel;
    cancel.cancel();
    EXPECT_FALSE(WorkspacePackager::write(smallPlan(s.root), pathToUtf8(zip), {}, &cancel));
    EXPECT_EQ(readText(zip), "an older package");
    EXPECT_FALSE(fs::exists(s.root / "p.platemaker.zip.tmp"));
}

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
        ed.setCanvasProfileTemplates(id, {info});
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
    EXPECT_TRUE(in1.sha256.empty());                          // the last render's hash: no render here
    EXPECT_EQ(in1.filePath, g(root / "inputs" / "001.png"));
    EXPECT_EQ(in1.status, Models::FileStatus::Pending);       // not rendered, in this copy
    EXPECT_TRUE(in1.contributesTo.empty());
    EXPECT_TRUE(in1.thumbnailPath.empty());
    EXPECT_EQ(p1.getInputImages()[2].filePath, g(root / "inputs" / "003.png"));
    EXPECT_EQ(p1.getStripOverlays()[0].assetPath, g(root / "overlays" / "ovl-1.svg"));
    EXPECT_EQ(p1.getStripOverlays()[1].assetPath, g(root / "overlays" / "ovl-1.svg"));
    EXPECT_EQ(p1.getStripOverlays()[1].anchorInputUid, "in-2");
    EXPECT_EQ(p1.getStripOverlays()[0].sha256, "sha-ovl-1");   // an overlay's hash names its content

    const auto& p2 = copy.projectItems[1];
    EXPECT_EQ(p2.getInputImages()[0].filePath, g(root / "inputs" / "001 (2).png"));
    EXPECT_EQ(p2.getInputImages()[1].filePath, g(root / "inputs" / "001.png"));   // the shared page, once

    EXPECT_EQ(copy.canvasProfiles().at(0).templates.at(0).path, "templates/3p-m.png");
}

TEST(WorkspacePackager, APackageMirrorsTheWorkspaceFolderAndBringsTheRestIn)
{
    // The library knows no subfolder of a workspace by name: whatever is in the folder keeps its place,
    // and only what the workspace names from elsewhere is given one.
    const Scratch  s("mirror");
    const fs::path ws = s.root / "Chapter";
    touchFile(ws / "inputs" / "001.png");            // the consumer's own "inputs/", in the folder
    touchFile(ws / "pages" / "002.png");
    touchFile(ws / "cover.png");                     // directly in the folder
    touchFile(ws / "art" / "bubble.svg");            // a subfolder the library has never heard of
    touchFile(ws / "package.json");                  // the manifest's name, taken by the workspace
    touchFile(s.root / "scans" / "001.png");         // from elsewhere, and named like one in the folder
    touchFile(s.root / "elsewhere" / "logo.svg");

    Models::Workspace   workspace;
    Models::ProjectItem p;
    p.name = "One";
    p.uid  = "proj-1";
    // The page from elsewhere comes first: the folder's own file still keeps its name.
    p.getInputImages()   = {page("in-0", s.root / "scans" / "001.png", 0), page("in-1", ws / "inputs" / "001.png", 1),
                            page("in-2", ws / "pages" / "002.png", 2), page("in-3", ws / "cover.png", 3),
                            page("in-4", ws / "pages" / "gone.png", 4)};
    p.getStripOverlays() = {overlay("ovl-1", ws / "art" / "bubble.svg", "in-1"),
                            overlay("ovl-2", s.root / "elsewhere" / "logo.svg", "in-2"),
                            overlay("ovl-3", ws / "package.json", "in-3")};
    workspace.projectItems.push_back(std::move(p));

    const PackagePlan plan = WorkspacePackager::plan(workspace, pathToUtf8(ws / "Chapter.platemaker.json"));

    std::map<std::string, std::string> packed;
    for (const auto& f : plan.files)
        packed.emplace(f.target, g(utf8ToPath(f.source)));
    EXPECT_EQ(packed, (std::map<std::string, std::string>{
                          {"inputs/001.png", g(ws / "inputs" / "001.png")},
                          {"inputs/001 (2).png", g(s.root / "scans" / "001.png")},
                          {"pages/002.png", g(ws / "pages" / "002.png")},
                          {"cover.png", g(ws / "cover.png")},
                          {"art/bubble.svg", g(ws / "art" / "bubble.svg")},
                          {"external/logo.svg", g(s.root / "elsewhere" / "logo.svg")},
                          {"package (2).json", g(ws / "package.json")},
                      }));
    ASSERT_EQ(plan.missing.size(), 1u);
    EXPECT_EQ(plan.missing[0].target, "pages/gone.png");   // missing, and still where the folder had it
}

TEST(WorkspacePackager, TheArchiveHeadersMatchTheLibraryThatShips)
{
    // The headers are fetched separately from the DLL (see the root CMakeLists.txt); a mismatch would
    // describe structures and calls the library does not have.
    EXPECT_EQ(archive_version_number(), ARCHIVE_VERSION_NUMBER);
}

} // namespace Platemaker::Infrastructure
