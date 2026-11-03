/**
 * \file
 * \brief Unit tests for ProfileBundleSerializer — the portable profile-set (.platemaker.profiles.json).
 *
 * Pins the round trip and the two bundle invariants applied on write (no templates, no presets),
 * plus version validation and the on-disk save/load path.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-27
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/profile_bundle_serializer/profile_bundle_serializer.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>

#include <filesystem>
#include <fstream>
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
    if (!templatePath.empty()) {
        Models::CanvasTemplateInfo t;
        t.path = std::move(templatePath);
        cp.templates.push_back(std::move(t));
    }
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

    auto cp = canvas("cp-1", "Manga B4", 1600, 10240);
    cp.dpi              = 600.0;
    cp.templateSettings = {Models::TemplateFormat::Png, true};
    auto op = output("op-1", "My Output", 1000, 2000);
    op.dpi = 150.0;
    const std::string text = s.serialize({cp}, {op});

    const Infrastructure::ProfileBundle b = s.deserialize(text);

    ASSERT_EQ(b.canvasProfiles.size(), 1u);
    EXPECT_EQ(b.canvasProfiles[0].name, "Manga B4");
    EXPECT_EQ(b.canvasProfiles[0].canvasSize.width, 1600);
    EXPECT_EQ(b.canvasProfiles[0].margins.top, 10);
    EXPECT_EQ(b.canvasProfiles[0].margins.left, 40);
    EXPECT_EQ(b.canvasProfiles[0].dpi, 600.0) << "part of the profile, not a local path: it travels";
    EXPECT_EQ(b.canvasProfiles[0].templateSettings.format, Models::TemplateFormat::Png);
    EXPECT_TRUE(b.canvasProfiles[0].templateSettings.emptyLayer);

    ASSERT_EQ(b.outputProfiles.size(), 1u);
    EXPECT_EQ(b.outputProfiles[0].name, "My Output");
    EXPECT_EQ(b.outputProfiles[0].targetWidth, 1000);
    EXPECT_EQ(b.outputProfiles[0].sliceHeight, 2000);
    EXPECT_EQ(b.outputProfiles[0].dpi, 150.0);
}

TEST(ProfileBundleSerializerTest, SerializeStripsTemplates)
{
    Infrastructure::ProfileBundleSerializer s;

    const std::string text = s.serialize(
        {canvas("cp-1", "WithTemplate", 800, 1280, "templates/t.png")}, {});
    const auto b = s.deserialize(text);

    ASSERT_EQ(b.canvasProfiles.size(), 1u);
    EXPECT_TRUE(b.canvasProfiles[0].templates.empty()); // never carried in a bundle
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

TEST(ProfileBundleSerializerTest, DeserializeRejectsAVersionThatIsNotAnInteger)
{
    Infrastructure::ProfileBundleSerializer s;
    EXPECT_THROW((void)s.deserialize(R"({"version": "1", "canvasProfiles": []})"), std::runtime_error);
}

TEST(ProfileBundleSerializerTest, DeserializeRejectsAPaletteOfTheWrongType)
{
    // The version is fine, the palette is not an array of profiles: a schema error, not a crash.
    Infrastructure::ProfileBundleSerializer s;
    EXPECT_THROW((void)s.deserialize(R"({"version": 1, "canvasProfiles": 5})"), std::runtime_error);
}

TEST(ProfileBundleSerializerTest, LoadNamesTheFileThatFailedToParse)
{
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "pm_bundle_bad.platemaker.profiles.json";
    { std::ofstream(path) << R"({"canvasProfiles": []})"; }   // no version

    Infrastructure::ProfileBundleSerializer s;
    try {
        (void)s.load(path.string());
        ADD_FAILURE() << "a bundle without a version must not load";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find(path.string()), std::string::npos)
            << "the error must say which file: " << e.what();
    }
    std::error_code ec;
    fs::remove(path, ec);
}

TEST(ProfileBundleSerializerTest, SaveIntoAMissingFolderThrows)
{
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "pm-no-such-folder-7f3a" / "lib.platemaker.profiles.json";
    ASSERT_FALSE(fs::exists(path.parent_path()));
    Infrastructure::ProfileBundleSerializer s;
    EXPECT_THROW(s.save({canvas("cp-1", "A", 100, 100)}, {}, path.string()), std::runtime_error);
}

TEST(ProfileBundleSerializerTest, SaveOverSomethingThatCannotBeReplacedThrows)
{
    // The temp file is written, but neither the rename nor the copy fallback can replace a non-empty
    // folder standing at the destination: save() must report it instead of claiming success.
    namespace fs = std::filesystem;
    const fs::path dir  = fs::temp_directory_path() / "pm-bundle-blocked";
    const fs::path path = dir / "lib.platemaker.profiles.json";
    fs::remove_all(dir);
    fs::create_directories(path / "occupied");

    Infrastructure::ProfileBundleSerializer s;
    EXPECT_THROW(s.save({canvas("cp-1", "A", 100, 100)}, {}, path.string()), std::runtime_error);
    EXPECT_TRUE(fs::is_directory(path)) << "the destination is left as it was";
    fs::remove_all(dir);
}

} // namespace Platemaker
