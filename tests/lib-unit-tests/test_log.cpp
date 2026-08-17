/**
 * \file tests/lib-unit-tests/test_log.cpp
 * \brief Unit tests for the component-gated diagnostic logger (Infrastructure::Log).
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include <platemaker/infrastructure/log/log.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Log = Platemaker::Infrastructure::Log;

namespace {

// A sink that records what it receives, so tests can assert on the gating without touching stderr.
class CaptureSink {
public:
    CaptureSink()
    {
        Log::setSink([this](std::uint64_t component, std::string_view message) {
            m_records.emplace_back(component, std::string(message));
        });
    }
    ~CaptureSink()
    {
        Log::setSink({});                    // restore the default stderr sink
        Log::setEnabledComponents(Log::None); // leave global state clean for other tests
    }

    std::vector<std::pair<std::uint64_t, std::string>> m_records;
};

} // namespace

TEST(LogTest, SilentUntilAComponentIsEnabled)
{
    CaptureSink sink;
    Log::setEnabledComponents(Log::None);

    EXPECT_FALSE(Log::isEnabled(Log::Scaler));
    Log::write(Log::Scaler, "should be dropped");
    PLATEMAKER_LOG(Log::Scaler, std::string("also dropped"));

    EXPECT_TRUE(sink.m_records.empty());
}

TEST(LogTest, OnlyEnabledComponentsPassThrough)
{
    CaptureSink sink;
    Log::setEnabledComponents(Log::Scaler);

    EXPECT_TRUE(Log::isEnabled(Log::Scaler));
    EXPECT_FALSE(Log::isEnabled(Log::ScaledStrip));

    Log::write(Log::Scaler, "shown");
    Log::write(Log::ScaledStrip, "hidden");   // a different component stays gated

    ASSERT_EQ(sink.m_records.size(), 1u);
    EXPECT_EQ(sink.m_records[0].first, static_cast<std::uint64_t>(Log::Scaler));
    EXPECT_EQ(sink.m_records[0].second, "shown");
}

TEST(LogTest, MacroSkipsBuildingMessageWhenDisabled)
{
    CaptureSink sink;
    Log::setEnabledComponents(Log::None);

    bool built = false;
    // The message expression must not run while the component is disabled.
    PLATEMAKER_LOG(Log::ScaledStrip, [&] { built = true; return std::string("x"); }());
    EXPECT_FALSE(built);

    Log::setEnabledComponents(Log::ScaledStrip);
    PLATEMAKER_LOG(Log::ScaledStrip, [&] { built = true; return std::string("x"); }());
    EXPECT_TRUE(built);
    ASSERT_EQ(sink.m_records.size(), 1u);
    EXPECT_EQ(sink.m_records[0].second, "x");
}

TEST(LogTest, EnableDisableAreAdditiveBitwiseOps)
{
    CaptureSink sink;
    Log::setEnabledComponents(Log::None);

    Log::enable(Log::Scaler);
    Log::enable(Log::ScaledStrip);
    EXPECT_EQ(Log::enabledComponents(),
              static_cast<std::uint64_t>(Log::Scaler | Log::ScaledStrip));
    EXPECT_TRUE(Log::isEnabled(Log::Scaler));
    EXPECT_TRUE(Log::isEnabled(Log::ScaledStrip));

    Log::disable(Log::Scaler);
    EXPECT_FALSE(Log::isEnabled(Log::Scaler));
    EXPECT_TRUE(Log::isEnabled(Log::ScaledStrip));
}

TEST(LogTest, ComponentNamesAreStable)
{
    EXPECT_STREQ(Log::componentName(Log::ProcessingPipeline), "ProcessingPipeline");
    EXPECT_STREQ(Log::componentName(Log::Scaler), "Scaler");
    EXPECT_STREQ(Log::componentName(Log::ScaledStrip), "ScaledStrip");
}
