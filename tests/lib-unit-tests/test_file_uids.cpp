/**
 * \file
 * \brief Unit tests for local file uids on ProjectItem — uniqueness and the load/merge repair.
 *
 * The uids (`file-<hex>`, `out-N`) are short local identifiers, not RFC 4122 UUIDs. They used to be
 * derived from the list position (`"file-" + index`), which handed the same id to different files
 * across re-scans. These pin that every input gets a unique, non-empty uid, and that
 * ensureUniqueFileUids() mints missing ones and breaks up duplicates while leaving a clean set stable.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-28
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/models/project_item.hpp>

#include <string>
#include <unordered_set>
#include <platemaker/infrastructure/project_editor/project_editor.hpp>

namespace Platemaker::Models {
namespace {

std::size_t uniqueInputUids(const ProjectItem& p)
{
    std::unordered_set<std::string> s;
    for (const auto& f : p.getInputImages())
        s.insert(f.uid);
    return s.size();
}

} // namespace

TEST(FileUidTest, MergeFileScanGivesEveryInputAUniqueNonEmptyUid)
{
    // Paths need not exist: a brand-new file just hashes to empty and is treated as new, which is the
    // path that mints the uid.
    ProjectItem p;
    Platemaker::Infrastructure::ProjectEditor{p}.mergeFileScan({"a.png", "b.png", "c.png"});

    ASSERT_EQ(p.getInputImages().size(), 3u);
    for (const auto& f : p.getInputImages()) {
        EXPECT_FALSE(f.uid.empty());
        EXPECT_EQ(f.uid.rfind("file-", 0), 0u) << "expected a 'file-' prefix, got " << f.uid;
    }
    EXPECT_EQ(uniqueInputUids(p), 3u);

    // A re-scan that keeps two files and adds two more must not reissue a colliding id — the old
    // "file-" + position scheme gave a new file the same id as a kept one (the "two file-19s" bug).
    Platemaker::Infrastructure::ProjectEditor{p}.mergeFileScan({"a.png", "b.png", "d.png", "e.png"});
    ASSERT_EQ(p.getInputImages().size(), 4u);
    EXPECT_EQ(uniqueInputUids(p), 4u);
}

TEST(FileUidTest, EnsureUniqueFileUidsMintsEmptyAndDeduplicates)
{
    ProjectItem p;
    Platemaker::Infrastructure::ProjectEditor{p}.mergeFileScan({"a.png", "b.png", "c.png"});
    auto& inputs = p.getInputImages();

    // The two states an old workspace could carry: a collision, and an empty id (from the old "uuid"
    // key, which is no longer read).
    inputs[0].uid = "file-dup";
    inputs[1].uid = "file-dup";
    inputs[2].uid.clear();

    p.ensureUniqueFileUids();

    EXPECT_EQ(inputs[0].uid, "file-dup") << "the first holder keeps its id";
    EXPECT_NE(inputs[1].uid, "file-dup") << "the duplicate is reissued";
    EXPECT_FALSE(inputs[1].uid.empty());
    EXPECT_FALSE(inputs[2].uid.empty()) << "the empty id is minted";
    EXPECT_EQ(uniqueInputUids(p), 3u);
}

} // namespace Platemaker::Models
