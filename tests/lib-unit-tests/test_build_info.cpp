/**
 * \file
 * \brief Unit tests for the build self-description and linked-component report.
 *
 * These pin the runtime answers the library gives about itself: its own version (the runtime twin
 * of the compile-time \c version.hpp), the toolchain it was built with, and the third-party
 * components it links with their SPDX licences. Because the test binary is built against the very
 * library it loads, the runtime version must equal the header constant and the mismatch check must
 * pass — a failure here would mean the two sources have drifted.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-25
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/build_info/build_info.hpp>
#include <platemaker/version.hpp>

#include <algorithm>
#include <string>

namespace Platemaker::Infrastructure {

namespace {

const LinkedComponent* find(const std::vector<LinkedComponent>& all, const std::string& name)
{
    auto it = std::find_if(all.begin(), all.end(),
                           [&](const LinkedComponent& c) { return c.name == name; });
    return it == all.end() ? nullptr : &*it;
}

} // namespace

TEST(BuildInfo, RuntimeVersionMatchesHeaderConstant)
{
    // The test links the same library it loads, so the DLL's baked-in version must equal the
    // compile-time constant the test sees.
    EXPECT_EQ(buildInfo().version, std::string{version_string});
    EXPECT_TRUE(runtimeMatchesHeader());
}

TEST(BuildInfo, ReportsOwnLicence)
{
    EXPECT_EQ(buildInfo().licence, "LGPL-3.0-or-later");
}

TEST(BuildInfo, ReportsCompilerAndPlatform)
{
    const BuildInfo info = buildInfo();
    EXPECT_FALSE(info.compiler.empty());
    EXPECT_FALSE(info.platform.empty());
    // The fallbacks only fire on a toolchain we do not recognise; on any supported build they must not.
    EXPECT_EQ(info.compiler.find("unknown"), std::string::npos);
    EXPECT_EQ(info.platform.find("unknown"), std::string::npos);
}

TEST(LinkedComponents, ReportsLibvipsWithLgplLicence)
{
    const auto components = linkedComponents();
    const LinkedComponent* vips = find(components, "libvips");
    ASSERT_NE(vips, nullptr);
    EXPECT_FALSE(vips->version.empty());
    EXPECT_EQ(vips->licence, "LGPL-2.1-or-later");
}

TEST(LinkedComponents, ReportsNlohmannJsonWithMitLicence)
{
    const auto components = linkedComponents();
    const LinkedComponent* json = find(components, "nlohmann/json");
    ASSERT_NE(json, nullptr);
    EXPECT_FALSE(json->version.empty());
    EXPECT_EQ(json->licence, "MIT");
}

TEST(LinkedComponents, DoesNotReportTestOnlyGoogleTest)
{
    // GoogleTest is never shipped, so it must not appear in a licence/About report.
    EXPECT_EQ(find(linkedComponents(), "GoogleTest"), nullptr);
    EXPECT_EQ(find(linkedComponents(), "gtest"), nullptr);
}

TEST(LinkedComponents, EveryUrlIsGitHub)
{
    // The GUI only renders links it can trust; the lib guarantees GitHub-only so the "url" field
    // can be surfaced without the GUI vetting each host by hand.
    const auto components = linkedComponents();
    ASSERT_FALSE(components.empty());
    for (const LinkedComponent& c : components) {
        EXPECT_EQ(c.url.rfind("https://github.com/", 0), 0u)
            << c.name << " has a non-GitHub url: " << c.url;
    }
}

} // namespace Platemaker::Infrastructure
