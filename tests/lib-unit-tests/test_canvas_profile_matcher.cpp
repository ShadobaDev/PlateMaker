/**
 * \file
 * \brief Unit tests for CanvasProfileMatcher — partition-based profile resolution.
 *
 * Tests cover:
 *  - Empty projectProfileIds (all workspace profiles treated as project-linked)
 *  - Per-project profile list: Matched / FoundInWorkspaceOnly / NotFoundAnywhere
 *  - Priority order: first entry in projectProfileIds wins
 *  - Multiple workspace-only candidates returned in FoundInWorkspaceOnly
 *  - Edge cases: empty workspace, width match but height mismatch
 *
 * No image files are needed — the matcher works on CanvasProfile metadata only.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-14
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/core/canvas_profile_matcher/canvas_profile_matcher.hpp>
#include <platemaker/models/canvas_profile.hpp>

using namespace Platemaker::Core;
using namespace Platemaker::Models;

namespace {

CanvasProfile makeProfile(std::string id, std::string name, int w, int h)
{
    CanvasProfile cp;
    cp.id         = std::move(id);
    cp.name       = std::move(name);
    cp.canvasSize = {w, h};
    return cp;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Empty projectProfileIds — all workspace profiles land in subA
// ---------------------------------------------------------------------------

TEST(CanvasProfileMatcherTest, EmptyProjectIdsFirstProfileMatches)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
        makeProfile("cp-2", "Webtoon 2p", 1600, 5120),
    };

    CanvasProfileMatcher matcher(profiles); // no projectProfileIds

    const auto r = matcher.resolveForSize(1600, 10240);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::Matched);
    ASSERT_NE(r.profile, nullptr);
    EXPECT_EQ(r.profile->id, "cp-1");
}

TEST(CanvasProfileMatcherTest, EmptyProjectIdsSecondProfileMatches)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
        makeProfile("cp-2", "Webtoon 2p", 1600, 5120),
    };

    CanvasProfileMatcher matcher(profiles);

    const auto r = matcher.resolveForSize(1600, 5120);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::Matched);
    ASSERT_NE(r.profile, nullptr);
    EXPECT_EQ(r.profile->id, "cp-2");
}

TEST(CanvasProfileMatcherTest, EmptyProjectIdsNoMatchReturnsNotFoundAnywhere)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
    };

    CanvasProfileMatcher matcher(profiles);

    const auto r = matcher.resolveForSize(800, 1280);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::NotFoundAnywhere);
    EXPECT_EQ(r.profile, nullptr);
    EXPECT_TRUE(r.workspaceCandidates.empty());
}

TEST(CanvasProfileMatcherTest, EmptyWorkspaceReturnsNotFoundAnywhere)
{
    CanvasProfileMatcher matcher({});

    const auto r = matcher.resolveForSize(1600, 10240);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::NotFoundAnywhere);
    EXPECT_EQ(r.profile, nullptr);
}

// ---------------------------------------------------------------------------
// Per-project profile list — partition into subA (linked) / subB (workspace-only)
// ---------------------------------------------------------------------------

TEST(CanvasProfileMatcherTest, MatchedFromProjectList)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
        makeProfile("cp-2", "Webtoon 2p", 1600, 5120),
    };

    CanvasProfileMatcher matcher(profiles, {"cp-1"});

    const auto r = matcher.resolveForSize(1600, 10240);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::Matched);
    ASSERT_NE(r.profile, nullptr);
    EXPECT_EQ(r.profile->id, "cp-1");
    EXPECT_TRUE(r.workspaceCandidates.empty());
}

TEST(CanvasProfileMatcherTest, FoundInWorkspaceOnlyWhenNotLinkedToProject)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
        makeProfile("cp-2", "Webtoon 2p", 1600, 5120),
    };

    // Project only links cp-1; cp-2 is in workspace but not in project list.
    CanvasProfileMatcher matcher(profiles, {"cp-1"});

    const auto r = matcher.resolveForSize(1600, 5120);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::FoundInWorkspaceOnly);
    EXPECT_EQ(r.profile, nullptr);
    ASSERT_EQ(r.workspaceCandidates.size(), 1u);
    EXPECT_EQ(r.workspaceCandidates[0]->id, "cp-2");
}

TEST(CanvasProfileMatcherTest, NotFoundAnywhereWithProjectList)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
    };

    CanvasProfileMatcher matcher(profiles, {"cp-1"});

    const auto r = matcher.resolveForSize(800, 1280); // dimensions not in any profile
    EXPECT_EQ(r.status, ProfileMatchResult::Status::NotFoundAnywhere);
    EXPECT_EQ(r.profile, nullptr);
    EXPECT_TRUE(r.workspaceCandidates.empty());
}

// ---------------------------------------------------------------------------
// Priority order
// ---------------------------------------------------------------------------

TEST(CanvasProfileMatcherTest, ProjectListPriorityOrderFirstEntryWins)
{
    // Two workspace profiles with identical dimensions — both are valid candidates.
    // The conflict guard prevents this in a real project, but the matcher itself
    // must still honour priority order when it occurs.
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-a", "Profile A", 1600, 10240),
        makeProfile("cp-b", "Profile B", 1600, 10240),
    };

    // cp-b is listed first in projectProfileIds → it has higher priority.
    CanvasProfileMatcher matcher(profiles, {"cp-b", "cp-a"});

    const auto r = matcher.resolveForSize(1600, 10240);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::Matched);
    ASSERT_NE(r.profile, nullptr);
    EXPECT_EQ(r.profile->id, "cp-b");
}

TEST(CanvasProfileMatcherTest, ProjectListPriorityOrderSecondEntryWinsWhenFirstMisses)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
        makeProfile("cp-2", "Webtoon 2p", 1600, 5120),
    };

    // Both are project-linked; cp-1 is first but doesn't match 5120 height.
    CanvasProfileMatcher matcher(profiles, {"cp-1", "cp-2"});

    const auto r = matcher.resolveForSize(1600, 5120);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::Matched);
    ASSERT_NE(r.profile, nullptr);
    EXPECT_EQ(r.profile->id, "cp-2");
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(CanvasProfileMatcherTest, WidthMatchButHeightMismatchIsNotFound)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
    };

    CanvasProfileMatcher matcher(profiles);

    const auto r = matcher.resolveForSize(1600, 9999); // wrong height
    EXPECT_EQ(r.status, ProfileMatchResult::Status::NotFoundAnywhere);
}

TEST(CanvasProfileMatcherTest, HeightMatchButWidthMismatchIsNotFound)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
    };

    CanvasProfileMatcher matcher(profiles);

    const auto r = matcher.resolveForSize(800, 10240); // wrong width
    EXPECT_EQ(r.status, ProfileMatchResult::Status::NotFoundAnywhere);
}

TEST(CanvasProfileMatcherTest, MultipleWorkspaceOnlyCandidatesAllReturned)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Project profile", 800,  1280),
        makeProfile("cp-2", "Workspace A",     1600, 10240),
        makeProfile("cp-3", "Workspace B",     1600, 10240), // same dims as cp-2
    };

    // Project only links cp-1; cp-2 and cp-3 are workspace-only.
    CanvasProfileMatcher matcher(profiles, {"cp-1"});

    const auto r = matcher.resolveForSize(1600, 10240);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::FoundInWorkspaceOnly);
    ASSERT_EQ(r.workspaceCandidates.size(), 2u);
}

TEST(CanvasProfileMatcherTest, UnknownIdInProjectListIsIgnored)
{
    const std::vector<CanvasProfile> profiles = {
        makeProfile("cp-1", "Webtoon 4p", 1600, 10240),
    };

    // "cp-nonexistent" is not in the workspace — should be silently skipped.
    CanvasProfileMatcher matcher(profiles, {"cp-nonexistent"});

    // cp-1 is not linked to the project → FoundInWorkspaceOnly.
    const auto r = matcher.resolveForSize(1600, 10240);
    EXPECT_EQ(r.status, ProfileMatchResult::Status::FoundInWorkspaceOnly);
    ASSERT_EQ(r.workspaceCandidates.size(), 1u);
    EXPECT_EQ(r.workspaceCandidates[0]->id, "cp-1");
}
