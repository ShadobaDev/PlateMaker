/**
 * \file tests/lib-fault-tests/test_fault_harness.cpp
 * \brief The harness itself: for each kind of wrapper, the library's call passes through untouched
 *        until a test arms it, and then fails the way the real function fails, or returns the image given.
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

/// A black sRGB image of \p bands bands.
Core::PixelBuffer blackImage(int width, int height, int bands)
{
    return Core::PixelBuffer{Fault::blackImage(width, height, bands)};
}

using Fault::thrown;

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

TEST_F(FaultHarnessTest, AnArmedCallCanReturnAnImageInstead)
{
    const Core::PixelBuffer image = blackImage(10, 10, 3);
    VipsImage* const        other = Fault::blackImage(2, 3, 1);
    Fault::returnImage("vips_extract_area", other);

    const Core::PixelBuffer cropped = Core::MarginCropper{}.crop(image, {1, 1, 1, 1});
    EXPECT_EQ(cropped.vipsImage(), other);
    g_object_unref(other);
}

TEST_F(FaultHarnessTest, ObjectsAliveAreCounted)
{
    const int before = Fault::liveObjects();
    VipsImage* const image = Fault::blackImage(2, 2, 1);
    EXPECT_GT(Fault::liveObjects(), before);   // the image, and what made it while it is alive
    g_object_unref(image);
    EXPECT_EQ(Fault::liveObjects(), before);
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
    EXPECT_CONTAINS(thrown([&] { (void)cropper.crop(image, {1, 2, 3, 4}); }), "injected failure in vips_extract_area");
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
    EXPECT_CONTAINS(thrown(sliceTwoPages), "injected failure in vips_addalpha");
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
    EXPECT_CONTAINS(thrown([&] { (void)Infrastructure::WorkspacePackager::write(plan, Infrastructure::pathToUtf8(zip)); }),
                    "cannot create an archive");
    EXPECT_FALSE(fs::exists(zip));
}

} // namespace
} // namespace Platemaker
