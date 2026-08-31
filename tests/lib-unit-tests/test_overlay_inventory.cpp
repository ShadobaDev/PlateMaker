/**
 * \file
 * \brief Unit tests for ProjectItem's lib-owned overlay inventory — uid minting, SHA-256, dedup, remove.
 *
 * Overlays are a resource inventory parallel to input files: the consumer creates the bitmap, the
 * library inventories it. addOverlay() mints the uid, hashes the content, and dedups identical content.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-31
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/models/project_item.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace Platemaker::Models {

namespace {

namespace fs = std::filesystem;

/// A temp directory unique to one test, removed on destruction.
class TempDir {
public:
    explicit TempDir(const std::string& tag)
        : m_path(fs::temp_directory_path() /
                 ("pm-ovl-inv-" + tag + "-" +
                  std::to_string(::testing::UnitTest::GetInstance()->random_seed())))
    {
        fs::remove_all(m_path);
        fs::create_directories(m_path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(m_path, ec); }

    std::string write(const std::string& name, const std::string& bytes) const
    {
        const fs::path p = m_path / name;
        std::ofstream(p, std::ios::binary) << bytes;
        return p.string();
    }

private:
    fs::path m_path;
};

} // namespace

TEST(OverlayInventoryTest, AddOverlayMintsUidHashesAndPlaces)
{
    TempDir tmp("add");
    const std::string f = tmp.write("a.png", "BUBBLE-BYTES-1");

    ProjectItem proj;
    const std::string uid = proj.addOverlay(f, 10, 20, BlendMode::Multiply);

    ASSERT_EQ(proj.getStripOverlays().size(), 1u);
    const auto& o = proj.getStripOverlays().front();
    EXPECT_EQ(o.uid, uid);
    EXPECT_EQ(o.uid.rfind("ovl-", 0), 0u); // minted with the readable prefix
    EXPECT_FALSE(o.sha256.empty());        // content hashed by the lib
    EXPECT_EQ(o.bitmapPath, f);
    EXPECT_EQ(o.x, 10);
    EXPECT_EQ(o.y, 20);
    EXPECT_EQ(o.blend, BlendMode::Multiply);
    EXPECT_TRUE(o.enabled);
}

TEST(OverlayInventoryTest, DedupReusesPathForIdenticalContent)
{
    TempDir tmp("dedup");
    const std::string f1 = tmp.write("a.png", "SAME-BYTES");
    const std::string f2 = tmp.write("b.png", "SAME-BYTES"); // identical content, different path
    const std::string f3 = tmp.write("c.png", "OTHER-BYTES");

    ProjectItem proj;
    const std::string u1 = proj.addOverlay(f1, 0, 0);
    const std::string u2 = proj.addOverlay(f2, 5, 5); // same content → reuse f1's stored path
    const std::string u3 = proj.addOverlay(f3, 9, 9); // different content → keeps its own path

    ASSERT_EQ(proj.getStripOverlays().size(), 3u);
    const auto& ovs = proj.getStripOverlays();

    EXPECT_NE(u1, u2); // distinct placements even when they share a bitmap
    EXPECT_EQ(ovs[0].sha256, ovs[1].sha256);
    EXPECT_EQ(ovs[1].bitmapPath, f1) << "dedup: identical content reuses the already-stored path";
    EXPECT_NE(ovs[2].sha256, ovs[0].sha256);
    EXPECT_EQ(ovs[2].bitmapPath, f3);
    // The placement of the deduped overlay is still its own.
    EXPECT_EQ(ovs[1].x, 5);
    EXPECT_EQ(ovs[1].y, 5);
}

TEST(OverlayInventoryTest, RemoveOverlayById)
{
    TempDir tmp("remove");
    const std::string f1 = tmp.write("a.png", "AAA");
    const std::string f2 = tmp.write("b.png", "BBB");

    ProjectItem proj;
    const std::string u1 = proj.addOverlay(f1, 0, 0);
    const std::string u2 = proj.addOverlay(f2, 0, 0);

    EXPECT_TRUE(proj.removeOverlay(u1));
    EXPECT_FALSE(proj.removeOverlay("ovl-nonexistent"));
    ASSERT_EQ(proj.getStripOverlays().size(), 1u);
    EXPECT_EQ(proj.getStripOverlays().front().uid, u2);
}

} // namespace Platemaker::Models
