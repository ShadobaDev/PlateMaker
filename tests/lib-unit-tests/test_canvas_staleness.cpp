/**
 * \file
 * \brief Unit tests for canvas-profile staleness detection.
 *
 * Editing a canvas profile changes neither the input files nor the output files, so
 * hashes can never notice that a page went stale — before per-input fingerprints, a
 * margin change left the project claiming to be up to date while its outputs were
 * wrong.  These tests pin both directions: what must invalidate, and what must not.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-07-17
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <gtest/gtest.h>

#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

#include <string>
#include <vector>

namespace Platemaker::Models {
namespace {

CanvasProfile makeProfile(std::string id, int w, int h, int margin)
{
    CanvasProfile cp;
    cp.id         = std::move(id);
    cp.name       = "profile";
    cp.canvasSize = Size{w, h};
    cp.margins    = Margins{margin, margin, margin, margin};
    return cp;
}

/// A project in the state a completed render leaves behind: one page, one output,
/// with the canvas baseline recorded.
ProjectItem makeRenderedProject(const std::vector<CanvasProfile>& profiles,
                                const std::string&                usedProfileId)
{
    ProjectItem p;
    p.name = "Chapter";

    InputFile inf;
    inf.filePath = "page_000.png";
    inf.sha256   = "deadbeef";
    inf.status   = FileStatus::Processed;
    inf.contributesTo = {"output_001.png"};
    if (!usedProfileId.empty()) {
        for (const auto& cp : profiles) {
            if (cp.id == usedProfileId) {
                inf.canvasProfileId   = cp.id;
                inf.canvasFingerprint = canvasRenderFingerprint(cp);
                break;
            }
        }
    }
    p.getInputImages().push_back(std::move(inf));

    OutputFile outf;
    outf.fileName = "output_001.png";
    outf.sha256   = "cafe";
    outf.status   = FileStatus::Done;
    p.getOutputImages().push_back(std::move(outf));

    p.canvasProfileIdsAtRender = p.effectiveCanvasProfileIds(profiles);
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// canvasRenderFingerprint — scope of the signature
// ---------------------------------------------------------------------------

TEST(CanvasRenderFingerprintTest, ChangesWithMargins)
{
    const auto a = makeProfile("p1", 1600, 10240, 100);
    const auto b = makeProfile("p1", 1600, 10240, 120);
    EXPECT_NE(canvasRenderFingerprint(a), canvasRenderFingerprint(b));
}

TEST(CanvasRenderFingerprintTest, ChangesWithCanvasSize)
{
    const auto a = makeProfile("p1", 1600, 10240, 100);
    const auto b = makeProfile("p1", 1600, 10000, 100);
    EXPECT_NE(canvasRenderFingerprint(a), canvasRenderFingerprint(b));
}

TEST(CanvasRenderFingerprintTest, IgnoresTemplateOnlyColours)
{
    // visualColour / backgroundColour only reach the generated template PNG — they
    // never touch a render. Folding them in (as TemplateGenerator::signature() must,
    // for template identity) would force a full re-render of every chapter each time
    // the overlay colour is nudged.
    auto a = makeProfile("p1", 1600, 10240, 100);
    auto b = makeProfile("p1", 1600, 10240, 100);
    b.visualColour     = RGBA{1, 2, 3, 4};
    b.backgroundColour = RGBA{5, 6, 7, 8};

    EXPECT_EQ(canvasRenderFingerprint(a), canvasRenderFingerprint(b));
}

TEST(CanvasRenderFingerprintTest, IsUnambiguousAcrossFieldBoundaries)
{
    // Tagged fields must keep "16,0" from colliding with "1,60" and friends.
    const auto a = makeProfile("p1", 1600, 10240, 100);
    auto b = a;
    b.margins = Margins{10, 100, 100, 100};   // top 100→10
    auto c = a;
    c.margins = Margins{100, 10, 100, 100};   // right 100→10

    EXPECT_NE(canvasRenderFingerprint(a), canvasRenderFingerprint(b));
    EXPECT_NE(canvasRenderFingerprint(b), canvasRenderFingerprint(c));
}

// ---------------------------------------------------------------------------
// effectiveCanvasProfileIds
// ---------------------------------------------------------------------------

TEST(EffectiveCanvasProfileIdsTest, EmptyProjectListMeansEveryWorkspaceProfile)
{
    // Mirrors CanvasProfileMatcher: no per-project filter → accept all.
    const std::vector<CanvasProfile> ws{
        makeProfile("p1", 1600, 10240, 100),
        makeProfile("p2", 800, 1280, 0),
    };
    ProjectItem p;
    EXPECT_EQ(p.effectiveCanvasProfileIds(ws), (std::vector<std::string>{"p1", "p2"}));
}

TEST(EffectiveCanvasProfileIdsTest, KeepsProjectOrder)
{
    const std::vector<CanvasProfile> ws{
        makeProfile("p1", 1600, 10240, 100),
        makeProfile("p2", 800, 1280, 0),
    };
    ProjectItem p;
    p.canvasProfileIds = {"p2", "p1"};
    EXPECT_EQ(p.effectiveCanvasProfileIds(ws), (std::vector<std::string>{"p2", "p1"}));
}

TEST(EffectiveCanvasProfileIdsTest, DropsIdsMissingFromWorkspace)
{
    // A dangling id matches nothing, so it cannot influence a render either way.
    const std::vector<CanvasProfile> ws{makeProfile("p1", 1600, 10240, 100)};
    ProjectItem p;
    p.canvasProfileIds = {"p1", "ghost"};
    EXPECT_EQ(p.effectiveCanvasProfileIds(ws), (std::vector<std::string>{"p1"}));
}

// ---------------------------------------------------------------------------
// detectCanvasConfigChange — the bug this exists to prevent
// ---------------------------------------------------------------------------

TEST(CanvasConfigChangeTest, MarginEditInvalidatesTheAffectedPage)
{
    std::vector<CanvasProfile> ws{makeProfile("p1", 1600, 10240, 100)};
    const auto project = makeRenderedProject(ws, "p1");

    ASSERT_FALSE(project.detectCanvasConfigChange(ws).any())
        << "nothing touched yet — must be quiet";

    ws[0].margins.top = 150;   // the artist edits the profile

    const auto change = project.detectCanvasConfigChange(ws);
    EXPECT_TRUE(change.any());
    EXPECT_FALSE(change.listChanged) << "the list is the same; only content changed";
    EXPECT_EQ(change.changedInputs, (std::vector<std::string>{"page_000.png"}));
}

TEST(CanvasConfigChangeTest, ColourEditDoesNotInvalidate)
{
    // The regression that would silently cost a full re-render per colour tweak.
    std::vector<CanvasProfile> ws{makeProfile("p1", 1600, 10240, 100)};
    const auto project = makeRenderedProject(ws, "p1");

    ws[0].visualColour     = RGBA{0, 255, 0, 200};
    ws[0].backgroundColour = RGBA{255, 255, 255, 255};

    EXPECT_FALSE(project.detectCanvasConfigChange(ws).any())
        << "template-only fields must never invalidate a render";
}

TEST(CanvasConfigChangeTest, DeletingTheUsedProfileInvalidates)
{
    std::vector<CanvasProfile> ws{makeProfile("p1", 1600, 10240, 100)};
    const auto project = makeRenderedProject(ws, "p1");

    ws.clear();

    const auto change = project.detectCanvasConfigChange(ws);
    EXPECT_TRUE(change.any());
    EXPECT_EQ(change.changedInputs, (std::vector<std::string>{"page_000.png"}));
}

TEST(CanvasConfigChangeTest, AddingAProfileInvalidatesViaTheList)
{
    // The hole per-input fingerprints cannot close on their own: a page that matched
    // no profile has no baseline to compare against, so a newly added profile that
    // now matches it would go unnoticed. The list check is what catches it.
    std::vector<CanvasProfile> ws{makeProfile("p1", 1600, 10240, 100)};
    const auto project = makeRenderedProject(ws, "p1");

    ws.push_back(makeProfile("p2", 800, 1280, 0));

    const auto change = project.detectCanvasConfigChange(ws);
    EXPECT_TRUE(change.any());
    EXPECT_TRUE(change.listChanged);
}

TEST(CanvasConfigChangeTest, ReorderingProfilesInvalidatesViaTheList)
{
    // Order decides which profile wins for a given W×H, so it can change output.
    std::vector<CanvasProfile> ws{
        makeProfile("p1", 1600, 10240, 100),
        makeProfile("p2", 1600, 10240, 50),
    };
    auto project = makeRenderedProject(ws, "p1");

    std::swap(ws[0], ws[1]);

    EXPECT_TRUE(project.detectCanvasConfigChange(ws).listChanged);
}

TEST(CanvasConfigChangeTest, ProjectWithNoProfilesStaysQuiet)
{
    const std::vector<CanvasProfile> ws;
    const auto project = makeRenderedProject(ws, "");

    EXPECT_FALSE(project.detectCanvasConfigChange(ws).any());
}

TEST(CanvasConfigChangeTest, AddingAProfileToAProjectThatHadNoneInvalidates)
{
    // Guards a subtle trap: "rendered with no profiles" also has an empty
    // canvasProfileIdsAtRender, so emptiness must not be used as the "never rendered"
    // marker — otherwise this change would be missed entirely.
    std::vector<CanvasProfile> ws;
    const auto project = makeRenderedProject(ws, "");

    ws.push_back(makeProfile("p1", 1600, 10240, 100));

    EXPECT_TRUE(project.detectCanvasConfigChange(ws).any())
        << "a project rendered without profiles must notice one appearing";
}

TEST(CanvasConfigChangeTest, NeverRenderedProjectReportsNothing)
{
    // No outputs → no baseline. Its inputs are Pending, which already forces a run.
    std::vector<CanvasProfile> ws{makeProfile("p1", 1600, 10240, 100)};
    ProjectItem p;
    InputFile inf;
    inf.filePath = "page_000.png";
    inf.status   = FileStatus::Pending;
    p.getInputImages().push_back(std::move(inf));

    EXPECT_FALSE(p.detectCanvasConfigChange(ws).any());
}

TEST(CanvasConfigChangeTest, LegacyWorkspaceWithoutFingerprintsReRendersOnce)
{
    // Workspaces written before fingerprints existed carry no baseline. A project
    // using profiles must report stale and take one full re-render to establish one —
    // its outputs may genuinely be wrong, since not noticing that was the bug.
    const std::vector<CanvasProfile> ws{makeProfile("p1", 1600, 10240, 100)};

    auto project = makeRenderedProject(ws, "p1");
    // Simulate a load from the old schema: fields absent → empty.
    project.getInputImages()[0].canvasProfileId.clear();
    project.getInputImages()[0].canvasFingerprint.clear();
    project.canvasProfileIdsAtRender.clear();

    EXPECT_TRUE(project.detectCanvasConfigChange(ws).listChanged);
}

TEST(CanvasConfigChangeTest, BaselineSurvivesAMove)
{
    // ProjectItem's move operations are written by hand and enumerate every member,
    // so a field omitted from them is silently default-constructed. Loading a
    // workspace moves ProjectItems, which made the baseline arrive empty and every
    // project report "profiles changed" forever. Nothing else notices — the JSON is
    // written correctly, so only exercising a move catches it.
    const std::vector<CanvasProfile> ws{makeProfile("p1", 1600, 10240, 100)};

    ProjectItem original = makeRenderedProject(ws, "p1");
    ASSERT_FALSE(original.detectCanvasConfigChange(ws).any());

    const ProjectItem moved = std::move(original);
    EXPECT_FALSE(moved.detectCanvasConfigChange(ws).any())
        << "move construction dropped the canvas baseline";

    ProjectItem assigned;
    assigned = makeRenderedProject(ws, "p1");
    EXPECT_FALSE(assigned.detectCanvasConfigChange(ws).any())
        << "move assignment dropped the canvas baseline";
}

// ---------------------------------------------------------------------------
// outputProfileSignature — the half provenance cannot see
// ---------------------------------------------------------------------------

TEST(OutputProfileSignatureTest, CatchesFormatAndQualityChanges)
{
    // Switching format or nudging quality leaves the geometry — and therefore every
    // sourceMap — identical while changing every output byte. The signature is the
    // only mechanism that can notice, which is why it stays separate from the
    // per-input canvas fingerprints.
    OutputProfile a;
    a.outputFormat = OutputFormat::PNG;

    OutputProfile b = a;
    b.outputFormat = OutputFormat::JPEG;
    EXPECT_NE(outputProfileSignature(a), outputProfileSignature(b));

    OutputProfile c = a;
    c.pngOptions.compression = a.pngOptions.compression + 1;
    EXPECT_NE(outputProfileSignature(a), outputProfileSignature(c));

    OutputProfile d = a;
    d.jpegOptions.quality = a.jpegOptions.quality - 1;
    EXPECT_NE(outputProfileSignature(a), outputProfileSignature(d));
}

} // namespace Platemaker::Models
