/**
 * \file
 * \brief Unit tests for ProfileBundleSerializer — the portable profile-set (.platemaker.profiles.json).
 *
 * Pins the round trip and the two bundle invariants applied on write (no templateInfo, no presets),
 * plus version validation and the on-disk save/load path.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-27
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/profile_bundle_serializer/profile_bundle_serializer.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>

#include <filesystem>
#include <stdexcept>
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
    cp.margins           = {10, 20, 30, 40};
    cp.templateInfo.path = std::move(templatePath);
    return cp;
}

Models::OutputProfile output(std::string id, std::string name, int w, int h)
{
    Models::OutputProfile op;
    op.id          = std::move(id);
    op.name        = std::move(name);
    op.targetWidth = w;
    op.sliceHeight = h;
    return op;
}

} // namespace

// ---------------------------------------------------------------------------
// Round trip + invariants (string form)
// ---------------------------------------------------------------------------

TEST(ProfileBundleSerializerTest, RoundTripPreservesFields)
{
    Infrastructure::ProfileBundleSerializer s;

    const std::string text = s.serialize(
        {canvas("cp-1", "Manga B4", 1600, 10240)},
        {output("op-1", "My Output", 1000, 2000)});

    const Infrastructure::ProfileBundle b = s.deserialize(text);

    ASSERT_EQ(b.canvasProfiles.size(), 1u);
    EXPECT_EQ(b.canvasProfiles[0].name, "Manga B4");
    EXPECT_EQ(b.canvasProfiles[0].canvasSize.width, 1600);
    EXPECT_EQ(b.canvasProfiles[0].margins.top, 10);
    EXPECT_EQ(b.canvasProfiles[0].margins.left, 40);

    ASSERT_EQ(b.outputProfiles.size(), 1u);
    EXPECT_EQ(b.outputProfiles[0].name, "My Output");
    EXPECT_EQ(b.outputProfiles[0].targetWidth, 1000);
    EXPECT_EQ(b.outputProfiles[0].sliceHeight, 2000);
}

TEST(ProfileBundleSerializerTest, SerializeStripsTemplateInfo)
{
    Infrastructure::ProfileBundleSerializer s;

    const std::string text = s.serialize(
        {canvas("cp-1", "WithTemplate", 800, 1280, "templates/t.png")}, {});
    const auto b = s.deserialize(text);

    ASSERT_EQ(b.canvasProfiles.size(), 1u);
    EXPECT_TRUE(b.canvasProfiles[0].templateInfo.path.empty()); // never carried in a bundle
}

TEST(ProfileBundleSerializerTest, SerializeDropsPresetOutputProfiles)
{
    Infrastructure::ProfileBundleSerializer s;

    const std::string text = s.serialize(
        {},
        {output(std::string(Models::k_tapasPresetId), "Tapas", 940, 1504), // preset id → dropped
         output("op-user", "Mine", 800, 1280)});
    const auto b = s.deserialize(text);

    ASSERT_EQ(b.outputProfiles.size(), 1u);
    EXPECT_EQ(b.outputProfiles[0].name, "Mine");
}

// ---------------------------------------------------------------------------
// Version validation
// ---------------------------------------------------------------------------

TEST(ProfileBundleSerializerTest, DeserializeRejectsMissingVersion)
{
    Infrastructure::ProfileBundleSerializer s;
    EXPECT_THROW((void)s.deserialize(R"({"canvasProfiles":[],"outputProfiles":[]})"),
                 std::runtime_error);
}

TEST(ProfileBundleSerializerTest, DeserializeRejectsFutureVersion)
{
    Infrastructure::ProfileBundleSerializer s;
    EXPECT_THROW((void)s.deserialize(R"({"version":9999,"canvasProfiles":[],"outputProfiles":[]})"),
                 std::runtime_error);
}

TEST(ProfileBundleSerializerTest, DeserializeRejectsGarbage)
{
    Infrastructure::ProfileBundleSerializer s;
    EXPECT_THROW((void)s.deserialize("not json at all"), std::runtime_error);
}

TEST(ProfileBundleSerializerTest, ToleratesAnAbsentPalette)
{
    Infrastructure::ProfileBundleSerializer s;
    // A canvas-only bundle is valid — an absent outputProfiles array is empty, not an error.
    const auto b = s.deserialize(R"({"version":1,"canvasProfiles":[]})");
    EXPECT_TRUE(b.canvasProfiles.empty());
    EXPECT_TRUE(b.outputProfiles.empty());
}

// ---------------------------------------------------------------------------
// On-disk save / load
// ---------------------------------------------------------------------------

TEST(ProfileBundleSerializerTest, SaveThenLoadRoundTripsOnDisk)
{
    namespace fs = std::filesystem;
    Infrastructure::ProfileBundleSerializer s;

    const fs::path path =
        fs::temp_directory_path() /
        ("pm_bundle_test_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
         ".platemaker.profiles.json");

    s.save({canvas("cp-1", "OnDisk", 1200, 1600)},
           {output("op-1", "OnDiskOut", 800, 1280)}, path.string());

    const auto b = s.load(path.string());
    ASSERT_EQ(b.canvasProfiles.size(), 1u);
    EXPECT_EQ(b.canvasProfiles[0].name, "OnDisk");
    ASSERT_EQ(b.outputProfiles.size(), 1u);
    EXPECT_EQ(b.outputProfiles[0].name, "OnDiskOut");

    std::error_code ec;
    fs::remove(path, ec); // best-effort cleanup
}

TEST(ProfileBundleSerializerTest, LoadThrowsOnMissingFile)
{
    Infrastructure::ProfileBundleSerializer s;
    EXPECT_THROW((void)s.load("this/file/does/not/exist.platemaker.profiles.json"),
                 std::runtime_error);
}

} // namespace Platemaker
