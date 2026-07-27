/**
 * \file
 * \brief Regression tests for paths containing characters outside ASCII.
 *
 * The bug these exist for: every path in the library's API is UTF-8, but a narrow
 * std::string means different things to different parts of the standard library. On Windows
 * std::ifstream hands the bytes to fopen(), which reads them in the ANSI code page, while
 * libstdc++'s std::filesystem::path reads them as UTF-8. So on a path like "G:/Mój dysk/…"
 * fs::exists() said the file was there and std::ifstream could not open it.
 *
 * The consequences looked like two unrelated faults: inputs stayed Pending after a
 * successful render (their hash silently came back empty), so every render redid all the
 * work; and a workspace could be saved but never reopened, because save() opened through an
 * fs::path and load() through a std::string.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-19
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/project_item.hpp>
#include <platemaker/models/workspace.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace Platemaker {

namespace {

// The directory name is written with a \u escape inside a u8"" literal, so it does not depend
// on this file's own encoding or on the compiler's input charset — the very kind of implicit
// assumption these tests exist to remove. U+00F3 is "ó", as in Google Drive's "Mój dysk".
const std::u8string k_nonAsciiDirName = u8"pm_test_path_ó_dir";

//! The same name as a UTF-8 std::string, built without going through the helpers under test.
std::string nonAsciiDirNameUtf8()
{
    return std::string(k_nonAsciiDirName.begin(), k_nonAsciiDirName.end());
}

/**
 * \brief A temp directory whose name contains a non-ASCII character.
 *
 * Built with std::filesystem directly rather than via utf8ToPath(), so the fixture cannot
 * mask a fault in the helper it is used to test.
 */
class NonAsciiDir {
public:
    NonAsciiDir()
        : m_dir(std::filesystem::temp_directory_path() /
                std::filesystem::path(k_nonAsciiDirName))
    {
        std::error_code ec;
        std::filesystem::create_directories(m_dir, ec);
    }

    ~NonAsciiDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_dir, ec);
    }

    NonAsciiDir(const NonAsciiDir&)            = delete;
    NonAsciiDir& operator=(const NonAsciiDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_dir; }

    //! A child path as the UTF-8 string the library's API takes.
    [[nodiscard]] std::string childUtf8(const std::string& name) const
    {
        const std::u8string s = (m_dir / std::filesystem::path(name)).u8string();
        return std::string(s.begin(), s.end());
    }

    //! Writes \p content to a child file, using the wide path so setup never depends on the fix.
    [[nodiscard]] std::string writeChild(const std::string& name,
                                         const std::string& content) const
    {
        std::ofstream f(m_dir / std::filesystem::path(name), std::ios::binary);
        f << content;
        f.close();
        return childUtf8(name);
    }

private:
    std::filesystem::path m_dir;
};

constexpr const char* k_content = "platemaker path encoding regression payload";

} // namespace

// ===========================================================================
// The helpers themselves
// ===========================================================================

TEST(PathUtf8Test, RoundTripPreservesTheBytes)
{
    const std::string original = nonAsciiDirNameUtf8();
    const std::string back     = Infrastructure::pathToUtf8(
                                     Infrastructure::utf8ToPath(original));
    EXPECT_EQ(back, original);
}

TEST(PathUtf8Test, ResolvesToTheSameFileAsAWidePath)
{
    const NonAsciiDir dir;
    ASSERT_TRUE(std::filesystem::exists(dir.path()));

    const std::string child = dir.writeChild("probe.txt", k_content);

    // The helper's path and the wide path built by the fixture must be the same file.
    EXPECT_TRUE(std::filesystem::exists(Infrastructure::utf8ToPath(child)));
    EXPECT_EQ(std::filesystem::file_size(Infrastructure::utf8ToPath(child)),
              std::string(k_content).size());
}

// ===========================================================================
// Hashing — the failure that left inputs stuck on Pending
// ===========================================================================

TEST(NonAsciiPathTest, FileHashIsNotEmptyAndMatchesTheAsciiPathHash)
{
    const NonAsciiDir dir;
    const std::string nonAscii = dir.writeChild("payload.bin", k_content);

    // The same bytes under a plain ASCII path, as the control.
    const std::filesystem::path asciiPath =
        std::filesystem::temp_directory_path() / "pm_test_path_ascii.bin";
    {
        std::ofstream f(asciiPath, std::ios::binary);
        f << k_content;
    }

    const std::string hashNonAscii =
        Infrastructure::FileMetaData::computeFileSha256(nonAscii);
    const std::string hashAscii =
        Infrastructure::FileMetaData::computeFileSha256(
            Infrastructure::pathToUtf8(asciiPath));

    std::error_code ec;
    std::filesystem::remove(asciiPath, ec);

    // Before the fix this came back empty: the file could not be opened at all.
    EXPECT_FALSE(hashNonAscii.empty());
    EXPECT_EQ(hashNonAscii, hashAscii);
}

TEST(NonAsciiPathTest, SanitizeReportsProcessedRatherThanPending)
{
    // The end-to-end shape of the render bug: an input that was hashed at render time must
    // come back as Processed. With hashing broken the hash was never stored, sanitize() saw
    // an empty hash, called it Pending, and the next render redid everything.
    const NonAsciiDir dir;
    const std::string input = dir.writeChild("page_001.png", k_content);

    Models::ProjectItem project;
    project.name = "Chapter 01";

    Models::InputFile inf;
    inf.uuid     = "file-0";
    inf.order    = 0;
    inf.filePath = input;
    inf.sha256   = Infrastructure::FileMetaData::computeFileSha256(input);
    ASSERT_FALSE(inf.sha256.empty());
    project.getInputImages().push_back(inf);

    project.sanitize({});

    ASSERT_EQ(project.getInputImages().size(), 1u);
    EXPECT_EQ(project.getInputImages()[0].status, Models::FileStatus::Processed);
}

// ===========================================================================
// Workspace round trip — the "saved once, never reopens" failure
// ===========================================================================

TEST(NonAsciiPathTest, WorkspaceSavedUnderANonAsciiPathCanBeReopened)
{
    // This is the symptom that was mistaken for a Google Drive limitation: Drive creates a
    // localised folder ("Mój dysk"), save() opened through an fs::path and worked, load()
    // opened through a std::string and did not.
    const NonAsciiDir dir;
    const std::string wsPath = dir.childUtf8("ws.platemaker.json");

    Models::Workspace original;
    original.version = 2;

    Models::CanvasProfile cp;
    cp.id         = "cp-test";
    cp.name       = "Webtoon";
    cp.canvasSize = {1600, 10240};
    cp.margins    = {0, 0, 0, 0};
    Infrastructure::WorkspaceEditor(original).replaceCanvasProfiles({cp}); // keeps the supplied id

    const Infrastructure::WorkspaceSerializer ser;
    ASSERT_NO_THROW(ser.save(original, wsPath));
    ASSERT_TRUE(std::filesystem::exists(Infrastructure::utf8ToPath(wsPath)));

    Models::Workspace loaded;
    ASSERT_NO_THROW(loaded = ser.load(wsPath));

    ASSERT_EQ(loaded.canvasProfiles().size(), 1u);
    EXPECT_EQ(loaded.canvasProfiles()[0].id,   "cp-test");
    EXPECT_EQ(loaded.canvasProfiles()[0].name, "Webtoon");
}

TEST(NonAsciiPathTest, WorkspaceRemembersNonAsciiInputPathsExactly)
{
    // The stored path is what a later render and sanitize() work from, so it has to survive
    // the JSON round trip byte for byte.
    const NonAsciiDir dir;
    const std::string wsPath = dir.childUtf8("ws2.platemaker.json");
    const std::string input  = dir.writeChild("page_002.png", k_content);

    Models::Workspace original;
    original.version = 2;

    Models::ProjectItem project;
    project.name = "Chapter 01";
    Models::InputFile inf;
    inf.uuid     = "file-0";
    inf.filePath = input;
    project.getInputImages().push_back(inf);
    original.projectItems.push_back(std::move(project));

    const Infrastructure::WorkspaceSerializer ser;
    ASSERT_NO_THROW(ser.save(original, wsPath));

    Models::Workspace loaded;
    ASSERT_NO_THROW(loaded = ser.load(wsPath));

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    ASSERT_EQ(loaded.projectItems[0].getInputImages().size(), 1u);
    EXPECT_EQ(loaded.projectItems[0].getInputImages()[0].filePath, input);

    // And the round-tripped path still resolves to the file on disk.
    EXPECT_TRUE(std::filesystem::exists(Infrastructure::utf8ToPath(
        loaded.projectItems[0].getInputImages()[0].filePath)));
}

} // namespace Platemaker
