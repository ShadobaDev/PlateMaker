/**
 * \file tests/lib-fault-tests/test_fault_harness.cpp
 * \brief The harness itself: for each kind of wrapper, the library's call passes through untouched
 *        until a test arms it, and then fails the way the real function fails.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "fault.hpp"

#include <platemaker/core/margin_cropper/margin_cropper.hpp>
#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/workspace_packager/workspace_packager.hpp>

#include <vips/vips.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace Platemaker {
namespace {

namespace fs = std::filesystem;

class VipsEnvironment : public ::testing::Environment {
public:
    void SetUp() override { ASSERT_EQ(VIPS_INIT("platemaker-lib-fault-tests"), 0) << vips_error_buffer(); }
    void TearDown() override { vips_shutdown(); }
};

const auto* const g_vipsEnv = ::testing::AddGlobalTestEnvironment(new VipsEnvironment);

/// A black sRGB image of \p bands bands.
Core::PixelBuffer blackImage(int width, int height, int bands)
{
    VipsImage* black = nullptr;
    VipsImage* srgb  = nullptr;
    EXPECT_EQ(vips_black(&black, width, height, "bands", bands, nullptr), 0) << vips_error_buffer();
    EXPECT_EQ(vips_copy(black, &srgb, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr), 0) << vips_error_buffer();
    g_object_unref(black);
    return Core::PixelBuffer{srgb};
}

/// What the message of \p f's exception says, or "" when it throws none.
template <typename F>
std::string thrown(F&& f)
{
    try {
        f();
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

using FaultHarnessTest = FaultTest;

// --- The harness -------------------------------------------------------------------------------------

TEST_F(FaultHarnessTest, AFunctionWithoutAWrapperCannotBeArmed)
{
    EXPECT_THROW(Fault::failCall("vips_no_such_function"), std::logic_error);
    EXPECT_THROW((void)Fault::calls("vips_no_such_function"), std::logic_error);
}

TEST_F(FaultHarnessTest, OnlyTheArmedCallFailsAndOnlyOnce)
{
    const Core::PixelBuffer image = blackImage(10, 10, 3);
    const Core::MarginCropper cropper;
    Fault::failCall("vips_extract_area", 2);

    EXPECT_NO_THROW((void)cropper.crop(image, {1, 1, 1, 1}));
    EXPECT_THROW((void)cropper.crop(image, {1, 1, 1, 1}), std::runtime_error);
    EXPECT_NO_THROW((void)cropper.crop(image, {1, 1, 1, 1}));
    EXPECT_EQ(Fault::calls("vips_extract_area"), 3);
}

TEST_F(FaultHarnessTest, AnArmedFaultNeverReachedIsReported)
{
    Fault::failCall("vips_extract_area");
    EXPECT_EQ(Fault::reset(), "vips_extract_area");
}

// --- One per kind of wrapper ---------------------------------------------------------------------------

TEST_F(FaultHarnessTest, ALibvipsOperationFailsWithItsError)
{
    const Core::PixelBuffer image = blackImage(10, 10, 3);
    const Core::MarginCropper cropper;

    const Core::PixelBuffer cropped = cropper.crop(image, {1, 2, 3, 4});
    EXPECT_EQ(cropped.width(), 4);   // 10 - right 2 - left 4: the arguments arrived
    EXPECT_EQ(cropped.height(), 6);

    Fault::failCall("vips_extract_area");
    EXPECT_TRUE(contains(thrown([&] { (void)cropper.crop(image, {1, 2, 3, 4}); }),
                         "injected failure in vips_extract_area"));
}

TEST_F(FaultHarnessTest, ALibvipsFunctionThatIsNoOperationFailsWithItsError)
{
    const auto sliceTwoPages = [] {
        Core::ScaledStrip strip;
        strip.append(Core::ScaledImage{blackImage(10, 10, 3), "rgb"});
        strip.append(Core::ScaledImage{blackImage(10, 10, 4), "rgba"});   // the RGB page gains an alpha
        Infrastructure::CancellationToken cancel;
        int bands = 0;
        strip.sliceAll(20, Models::LastSlicePolicy::KeepAsIs, cancel, [&](Core::SliceResult&& s) {
            bands = vips_image_get_bands(s.image.vipsImage());
            return true;
        });
        return bands;
    };

    EXPECT_EQ(sliceTwoPages(), 4);

    Fault::failCall("vips_addalpha");
    EXPECT_TRUE(contains(thrown(sliceTwoPages), "injected failure in vips_addalpha"));
}

TEST_F(FaultHarnessTest, ALibarchiveFunctionFailsWithItsResult)
{
    const fs::path zip = fs::temp_directory_path() / "pm_fault_harness.zip";
    Infrastructure::PackagePlan plan;
    plan.workspaceFileName = "workspace.pmw";
    plan.workspaceJson     = "{}";

    EXPECT_TRUE(Infrastructure::WorkspacePackager::write(plan, Infrastructure::pathToUtf8(zip)));
    EXPECT_TRUE(fs::exists(zip));
    fs::remove(zip);

    Fault::failCall("archive_write_new");
    EXPECT_TRUE(contains(thrown([&] { (void)Infrastructure::WorkspacePackager::write(plan, Infrastructure::pathToUtf8(zip)); }),
                         "cannot create an archive"));
    EXPECT_FALSE(fs::exists(zip));
}

} // namespace
} // namespace Platemaker
