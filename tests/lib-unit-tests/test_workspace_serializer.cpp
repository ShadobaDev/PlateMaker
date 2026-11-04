/**
 * \file
 * \brief Unit tests for WorkspaceSerializer — JSON round-trip, defaults, migration.
 *
 * These tests exercise the serialiser in isolation using in-memory JSON
 * strings and temporary files (via std::filesystem temp_directory_path).
 * No real image files are needed — the serialiser deals only with metadata.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-02
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <gtest/gtest.h>

#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/workspace.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

#include <nlohmann/json.hpp>

#include <utility>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Platemaker::Infrastructure {

namespace {

/// Build a minimal valid Workspace suitable for round-trip tests.
Models::Workspace makeMinimalWorkspace()
{
    Models::CanvasProfile canvas;
    canvas.id           = "cp-test-001";
    canvas.name         = "Webtoon Standard";
    canvas.canvasSize   = {1600, 10240};
    canvas.margins      = {100, 100, 100, 100};
    canvas.visualColour = {255, 105, 180, 128}; // hot pink @ 50 % alpha
    canvas.dpi          = 350.5;                // not the default, so a lost field shows
    canvas.templateSettings = {Models::TemplateFormat::Png, true};

    // A genuine user profile — deliberately *not* preset-shaped (WebP, not the preset's PNG), so it
    // is not collapsed into a catalogue reference on load.
    Models::OutputProfile output;
    output.id              = "op-test-001";
    output.name            = "Webtoon Export";
    output.targetWidth     = 800;
    output.sliceHeight     = 1280;
    output.lastSlicePolicy = Models::LastSlicePolicy::KeepAsIs;
    output.outputFormat    = Models::OutputFormat::WebP;
    output.startIndex      = 1;
    output.dpi             = 96.0;   // not the default

    Models::Workspace ws;
    ws.version         = 3;   // the current schema
    ws.outputDirectory = "/tmp/out";
    ws.stripDirty      = true;
    WorkspaceEditor ed(ws);
    ed.replaceCanvasProfiles({canvas}); // keeps the supplied ids (only mints when absent)
    ed.replaceOutputProfiles({output});
    return ws;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Round-trip: save → load → compare
// ---------------------------------------------------------------------------

TEST(WorkspaceSerializerTest, RoundTripPreservesVersion)
{
    const WorkspaceSerializer ser;
    const Models::Workspace   original = makeMinimalWorkspace();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_roundtrip.platemaker.json";

    ASSERT_NO_THROW(ser.save(original, tmp.string()));

    Models::Workspace loaded;
    ASSERT_NO_THROW(loaded = ser.load(tmp.string()));

    EXPECT_EQ(loaded.version, original.version);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, RoundTripPreservesCanvasProfile)
{
    const WorkspaceSerializer ser;
    const Models::Workspace   original = makeMinimalWorkspace();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_canvas.platemaker.json";

    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    ASSERT_EQ(loaded.canvasProfiles().size(), 1u);
    EXPECT_EQ(loaded.canvasProfiles()[0].name,              original.canvasProfiles()[0].name);
    EXPECT_EQ(loaded.canvasProfiles()[0].canvasSize.width,  original.canvasProfiles()[0].canvasSize.width);
    EXPECT_EQ(loaded.canvasProfiles()[0].canvasSize.height, original.canvasProfiles()[0].canvasSize.height);
    EXPECT_EQ(loaded.canvasProfiles()[0].margins.top,       original.canvasProfiles()[0].margins.top);
    EXPECT_EQ(loaded.canvasProfiles()[0].visualColour.r,    original.canvasProfiles()[0].visualColour.r);
    EXPECT_EQ(loaded.canvasProfiles()[0].dpi,               350.5);
    EXPECT_EQ(loaded.canvasProfiles()[0].templateSettings.format, Models::TemplateFormat::Png);
    EXPECT_TRUE(loaded.canvasProfiles()[0].templateSettings.emptyLayer);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, RoundTripPreservesOutputProfile)
{
    const WorkspaceSerializer ser;
    const Models::Workspace   original = makeMinimalWorkspace();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_output.platemaker.json";

    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    // A genuine user profile round-trips unchanged, and load() no longer appends a preset —
    // presets live in the catalogue, not in the workspace.
    ASSERT_EQ(loaded.outputProfiles().size(), 1u);
    EXPECT_EQ(loaded.outputProfiles()[0].id,           original.outputProfiles()[0].id);
    EXPECT_EQ(loaded.outputProfiles()[0].targetWidth,  original.outputProfiles()[0].targetWidth);
    EXPECT_EQ(loaded.outputProfiles()[0].sliceHeight,  original.outputProfiles()[0].sliceHeight);
    EXPECT_EQ(loaded.outputProfiles()[0].outputFormat, original.outputProfiles()[0].outputFormat);
    EXPECT_EQ(loaded.outputProfiles()[0].startIndex,   original.outputProfiles()[0].startIndex);
    EXPECT_EQ(loaded.outputProfiles()[0].dpi,          96.0);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, AVersion2TemplateBecomesOneEntryOfItsFormat)
{
    // Version 2 tracked one file per profile under "templateInfo", with a signature that predates the
    // resolution and the format. It loads as one entry of the file's format, its signature finished
    // with what that file really is: 25.4 DPI (libvips' 1 px/mm) and its extension. No hash.
    const WorkspaceSerializer ser;
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_v2_template.platemaker.json";
    ser.save(makeMinimalWorkspace(), tmp.string());
    nlohmann::json j;
    { std::ifstream in(tmp); in >> j; }
    j["version"] = 2;
    auto& cp = j["canvasProfiles"][0];
    cp.erase("templates");
    cp["templateInfo"] = {{"path", "templates/Webtoon_Standard.png"},
                          {"fingerprint", "cw=1600;ch=10240;mt=100;mr=100;mb=100;ml=100;vc=255,105,180,128;bg=0,0,0,0"},
                          {"generatedAt", "2026-09-01T10:00:00Z"}};
    { std::ofstream out(tmp); out << j.dump(2); }

    const auto loaded = ser.load(tmp.string());
    EXPECT_EQ(loaded.version, 3);
    ASSERT_EQ(loaded.canvasProfiles()[0].templates.size(), 1u);
    const auto& t = loaded.canvasProfiles()[0].templates[0];
    EXPECT_EQ(t.path, "templates/Webtoon_Standard.png");
    EXPECT_EQ(t.format, Models::TemplateFormat::Png);
    EXPECT_EQ(t.fingerprint,
              "cw=1600;ch=10240;mt=100;mr=100;mb=100;ml=100;vc=255,105,180,128;bg=0,0,0,0;dpi=25.4;fmt=png");
    EXPECT_TRUE(t.sha256.empty());
    EXPECT_EQ(t.generatedAt, "2026-09-01T10:00:00Z");

    // A PSD tracked in version 2 (after the resolution was) keeps its signature's dpi, gains its format.
    cp["templateInfo"] = {{"path", "templates/x.PSD"}, {"fingerprint", "cw=1;dpi=300"}};
    { std::ofstream out(tmp); out << j.dump(2); }
    const auto psd = ser.load(tmp.string()).canvasProfiles()[0].templates.at(0);
    EXPECT_EQ(psd.format, Models::TemplateFormat::Psd);
    EXPECT_EQ(psd.fingerprint, "cw=1;dpi=300;fmt=psd");

    // An empty record was "no template": nothing to carry.
    cp["templateInfo"] = {{"path", ""}};
    { std::ofstream out(tmp); out << j.dump(2); }
    EXPECT_TRUE(ser.load(tmp.string()).canvasProfiles()[0].templates.empty());

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, AVersion2TemplateWithNothingToFinishIsLeftAsIs)
{
    // The v2 signature migration only appends what is missing: an entry with no signature stays
    // without one, and one that already names its format keeps it unchanged.
    const WorkspaceSerializer ser;
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "pm_test_v2_template_b.json";
    ser.save(makeMinimalWorkspace(), tmp.string());
    nlohmann::json j;
    { std::ifstream in(tmp); in >> j; }
    j["version"] = 2;
    auto& cp = j["canvasProfiles"][0];
    cp.erase("templates");

    cp["templateInfo"] = {{"path", "templates/a.png"}};
    { std::ofstream out(tmp); out << j.dump(2); }
    EXPECT_TRUE(ser.load(tmp.string()).canvasProfiles()[0].templates.at(0).fingerprint.empty());

    cp["templateInfo"] = {{"path", "templates/b.png"}, {"fingerprint", "cw=1;dpi=72;fmt=png"}};
    { std::ofstream out(tmp); out << j.dump(2); }
    EXPECT_EQ(ser.load(tmp.string()).canvasProfiles()[0].templates.at(0).fingerprint, "cw=1;dpi=72;fmt=png");

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, LoadPreProjectFormatWithOnlyProcessedFilesIsRefused)
{
    // Either legacy key alone identifies the old layout.
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "pm_test_legacy_processed.json";
    { std::ofstream f(tmp); f << R"({"version": 1, "processedFiles": [], "canvasProfiles": [], "outputProfiles": []})"; }
    EXPECT_THROW((void)WorkspaceSerializer{}.load(tmp.string()), std::runtime_error);
    std::filesystem::remove(tmp);
}

namespace {

/// A workspace that writes every enumeration value at least once: an output profile per format /
/// slice policy / subsampling, an input per file status, an overlay per blend mode, a template per format.
Models::Workspace everyEnumWorkspace()
{
    Models::Workspace ws = makeMinimalWorkspace();
    WorkspaceEditor   ed(ws);

    std::vector<Models::OutputProfile> outs;
    const Models::OutputFormat    formats[]  = {Models::OutputFormat::PNG, Models::OutputFormat::JPEG,
                                                Models::OutputFormat::WebP};
    const Models::LastSlicePolicy policies[] = {Models::LastSlicePolicy::Crop, Models::LastSlicePolicy::PadWhite,
                                                Models::LastSlicePolicy::KeepAsIs};
    const Models::JpegSubsampling subs[]     = {Models::JpegSubsampling::YUV_444, Models::JpegSubsampling::YUV_422,
                                                Models::JpegSubsampling::YUV_420};
    for (int i = 0; i < 3; ++i) {
        Models::OutputProfile op;
        op.id                      = "op-e" + std::to_string(i);
        op.name                    = "E" + std::to_string(i);
        op.targetWidth             = 700 + i;
        op.outputFormat            = formats[i];
        op.lastSlicePolicy         = policies[i];
        op.jpegOptions.subsampling = subs[i];
        outs.push_back(op);
    }
    ed.replaceOutputProfiles(outs);

    Models::CanvasTemplateInfo png; png.path = "t/a.png"; png.format = Models::TemplateFormat::Png;
    Models::CanvasTemplateInfo psd; psd.path = "t/a.psd"; psd.format = Models::TemplateFormat::Psd;
    ed.setCanvasProfileTemplates(ws.canvasProfiles()[0].id, {png, psd});

    Models::ProjectItem& p = ed.addProject("Enums");
    const Models::FileStatus statuses[] = {
        Models::FileStatus::Pending, Models::FileStatus::Processed, Models::FileStatus::Modified,
        Models::FileStatus::Missing, Models::FileStatus::Desynchronized, Models::FileStatus::Done,
        Models::FileStatus::Skipped, Models::FileStatus::Error};
    for (int i = 0; i < 8; ++i) {
        Models::InputFile inf;
        inf.uid      = "in-" + std::to_string(i);
        inf.filePath = "/pages/p" + std::to_string(i) + ".png";
        inf.order    = i;
        inf.status   = statuses[i];
        p.getInputImages().push_back(inf);
    }
    const Models::BlendMode blends[] = {Models::BlendMode::Over, Models::BlendMode::Multiply,
                                        Models::BlendMode::Screen, Models::BlendMode::Overlay,
                                        Models::BlendMode::Darken, Models::BlendMode::Lighten};
    for (int i = 0; i < 6; ++i) {
        Models::StripOverlay ov;
        ov.uid       = "ovl-" + std::to_string(i);
        ov.assetPath = "/assets/b" + std::to_string(i) + ".svg";
        ov.blend     = blends[i];
        p.getStripOverlays().push_back(ov);
    }
    return ws;
}

} // namespace

TEST(WorkspaceSerializerTest, EveryEnumerationValueRoundTripsByName)
{
    const WorkspaceSerializer   ser;
    const Models::Workspace     original = everyEnumWorkspace();
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "pm_test_every_enum.json";
    ser.save(original, tmp.string());
    const Models::Workspace loaded = ser.load(tmp.string());
    std::filesystem::remove(tmp);

    ASSERT_EQ(loaded.outputProfiles().size(), original.outputProfiles().size());
    for (std::size_t i = 0; i < original.outputProfiles().size(); ++i) {
        EXPECT_EQ(loaded.outputProfiles()[i].outputFormat,    original.outputProfiles()[i].outputFormat);
        EXPECT_EQ(loaded.outputProfiles()[i].lastSlicePolicy, original.outputProfiles()[i].lastSlicePolicy);
        EXPECT_EQ(loaded.outputProfiles()[i].jpegOptions.subsampling,
                  original.outputProfiles()[i].jpegOptions.subsampling);
    }
    ASSERT_EQ(loaded.canvasProfiles()[0].templates.size(), 2u);
    EXPECT_EQ(loaded.canvasProfiles()[0].templates[0].format, Models::TemplateFormat::Png);
    EXPECT_EQ(loaded.canvasProfiles()[0].templates[1].format, Models::TemplateFormat::Psd);

    const auto& before = original.projectItems.back();
    const auto& after  = loaded.projectItems.back();
    ASSERT_EQ(after.getInputImages().size(), before.getInputImages().size());
    for (std::size_t i = 0; i < before.getInputImages().size(); ++i)
        EXPECT_EQ(after.getInputImages()[i].status, before.getInputImages()[i].status) << "input " << i;
    ASSERT_EQ(after.getStripOverlays().size(), before.getStripOverlays().size());
    for (std::size_t i = 0; i < before.getStripOverlays().size(); ++i)
        EXPECT_EQ(after.getStripOverlays()[i].blend, before.getStripOverlays()[i].blend) << "overlay " << i;
}

TEST(WorkspaceSerializerTest, AnUnknownEnumerationNameFallsBackToTheFirstValue)
{
    // A file written by a newer build may name a value this build does not know: it must load, taking
    // the first value of that enumeration rather than failing the whole workspace.
    const WorkspaceSerializer   ser;
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "pm_test_unknown_enum.json";
    ser.save(everyEnumWorkspace(), tmp.string());
    nlohmann::json j;
    { std::ifstream in(tmp); in >> j; }
    j["outputProfiles"][1]["lastSlicePolicy"] = "SomethingNew";
    j["outputProfiles"][1]["outputFormat"]    = "AVIF";
    j["projectItems"].back()["stripOverlays"][1]["blend"] = "ColorDodge";
    { std::ofstream out(tmp); out << j.dump(2); }

    const Models::Workspace loaded = ser.load(tmp.string());
    std::filesystem::remove(tmp);
    EXPECT_EQ(loaded.outputProfiles()[1].lastSlicePolicy, Models::LastSlicePolicy::Crop);
    EXPECT_EQ(loaded.outputProfiles()[1].outputFormat,    Models::OutputFormat::PNG);
    EXPECT_EQ(loaded.projectItems.back().getStripOverlays()[1].blend, Models::BlendMode::Over);
}

TEST(WorkspaceSerializerTest, OptionalFieldsAbsentFromAnOlderFileTakeTheirDefaults)
{
    // Fields added over time are optional on load. A file whose objects carry none of them must load
    // with every default in place — and with the ids that are minted when missing, minted.
    const WorkspaceSerializer   ser;
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "pm_test_sparse.json";
    Models::Workspace ws = everyEnumWorkspace();
    {
        WorkspaceEditor ed(ws);
        Models::ProjectItem& graded = ed.addProject("Graded");
        graded.colourCorrection.brightness = 0.3;
        graded.colourCorrection.curves.master = {{0.0, 0.0}, {1.0, 1.0}};
        Models::OutputFile out;
        out.uid      = "out-1";
        out.fileName = "output_001.png";
        ws.projectItems.front().getOutputImages().push_back(out);
    }
    ser.save(ws, tmp.string());
    nlohmann::json j;
    { std::ifstream in(tmp); in >> j; }

    auto& ops = j["outputProfiles"];
    ops[0]["pngOptions"]  = nlohmann::json::object();
    ops[0]["webpOptions"] = nlohmann::json::object();
    ops[1].erase("pngOptions");
    ops[1].erase("webpOptions");
    auto& cp = j["canvasProfiles"][0];
    cp["templateSettings"] = nlohmann::json::object();
    cp["templates"][0].erase("path");

    auto& enums = j["projectItems"][0];   // the "Enums" project
    for (const char* k : {"name", "uid"}) enums.erase(k);
    enums["colourCorrection"] = nlohmann::json::object();
    enums["stripOverlays"][0] = nlohmann::json::object();
    for (const char* k : {"uid", "canvasProfileId", "canvasFingerprint", "width", "height"})
        enums["inputFiles"][0].erase(k);
    j["projectItems"][1]["colourCorrection"]["curves"] = nlohmann::json::object();
    j["projectItems"][0]["outputFiles"][0].erase("uid");
    { std::ofstream out(tmp); out << j.dump(2); }

    const Models::Workspace loaded = ser.load(tmp.string());
    std::filesystem::remove(tmp);

    const Models::PngOptions  png;
    const Models::WebpOptions webp;
    for (int i : {0, 1}) {
        const auto& op = loaded.outputProfiles()[static_cast<std::size_t>(i)];
        EXPECT_EQ(op.pngOptions.compression, png.compression) << i;
        EXPECT_EQ(op.pngOptions.interlaced,  png.interlaced) << i;
        EXPECT_EQ(op.webpOptions.quality,    webp.quality) << i;
        EXPECT_EQ(op.webpOptions.lossless,   webp.lossless) << i;
        EXPECT_EQ(op.webpOptions.effort,     webp.effort) << i;
    }
    const Models::CanvasTemplateSettings settings;
    EXPECT_EQ(loaded.canvasProfiles()[0].templateSettings.format,     settings.format);
    EXPECT_EQ(loaded.canvasProfiles()[0].templateSettings.emptyLayer, settings.emptyLayer);
    ASSERT_FALSE(loaded.canvasProfiles()[0].templates.empty());
    EXPECT_EQ(loaded.canvasProfiles()[0].templates[0].format, Models::TemplateFormat::Png)
        << "no path and no format: the PNG default";

    const auto& p = loaded.projectItems[0];
    EXPECT_TRUE(p.name.empty());
    EXPECT_FALSE(p.uid.empty()) << "a project without a uid gets one on load";
    EXPECT_DOUBLE_EQ(p.colourCorrection.brightness, 0.0);
    EXPECT_DOUBLE_EQ(p.colourCorrection.contrast,   1.0);
    EXPECT_DOUBLE_EQ(p.colourCorrection.saturation, 1.0);
    EXPECT_FALSE(Models::hasAnyCurve(p.colourCorrection.curves));
    const auto& ov = p.getStripOverlays()[0];
    EXPECT_TRUE(ov.enabled);
    EXPECT_EQ(ov.blend, Models::BlendMode::Over);
    EXPECT_TRUE(ov.anchorInputUid.empty());
    EXPECT_DOUBLE_EQ(ov.wFrac, 0.0);
    const auto& in0 = p.getInputImages()[0];
    EXPECT_FALSE(in0.uid.empty()) << "an input without a uid gets one on load";
    EXPECT_EQ(in0.width, 0);
    EXPECT_EQ(in0.height, 0);
    EXPECT_TRUE(in0.canvasProfileId.empty());

    const auto& graded = loaded.projectItems[1];
    EXPECT_NEAR(graded.colourCorrection.brightness, 0.3, 1e-9) << "fields present still load";
    EXPECT_FALSE(Models::hasAnyCurve(graded.colourCorrection.curves)) << "an empty curves object is no curve";
    EXPECT_FALSE(loaded.projectItems[0].getOutputImages()[0].uid.empty()) << "an output gets a uid on load";
}

TEST(WorkspaceSerializerTest, ProfilesWrittenBeforeDpiLoadWithTheDefaults)
{
    // Strip the new fields from a saved file, as a pre-DPI build wrote it: they are additive.
    const WorkspaceSerializer ser;
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_pre_dpi.platemaker.json";
    ser.save(makeMinimalWorkspace(), tmp.string());
    nlohmann::json j;
    { std::ifstream in(tmp); in >> j; }
    for (auto& cp : j["canvasProfiles"]) { cp.erase("dpi"); cp.erase("templateSettings"); }
    for (auto& op : j["outputProfiles"]) op.erase("dpi");
    { std::ofstream out(tmp); out << j.dump(2); }

    const auto loaded = ser.load(tmp.string());
    const Models::CanvasProfile defaults;
    EXPECT_EQ(loaded.canvasProfiles()[0].dpi, 300.0);
    EXPECT_EQ(loaded.canvasProfiles()[0].templateSettings.format, defaults.templateSettings.format);
    EXPECT_FALSE(loaded.canvasProfiles()[0].templateSettings.emptyLayer);
    EXPECT_EQ(loaded.outputProfiles()[0].dpi, 72.0);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, RoundTripPreservesInputDimensions)
{
    // The per-input display W×H recorded at render time must survive save/load — it is what lets
    // detectCanvasConfigChange() re-match each page offline instead of blanket-invalidating a project.
    const WorkspaceSerializer ser;
    Models::Workspace         original = makeMinimalWorkspace();

    Models::ProjectItem proj;
    proj.name = "Chapter";
    proj.uid  = "proj-dim-001";
    Models::InputFile inf;
    inf.uid      = "file-001";
    inf.filePath = "page_000.png";
    inf.width    = 1080;
    inf.height   = 1920;
    proj.getInputImages().push_back(std::move(inf));
    original.projectItems.push_back(std::move(proj));

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_dims.platemaker.json";
    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    ASSERT_EQ(loaded.projectItems[0].getInputImages().size(), 1u);
    EXPECT_EQ(loaded.projectItems[0].getInputImages()[0].width,  1080);
    EXPECT_EQ(loaded.projectItems[0].getInputImages()[0].height, 1920);

    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

TEST(WorkspaceSerializerTest, LoadMissingFileThrowsRuntimeError)
{
    const WorkspaceSerializer ser;
    EXPECT_THROW(
        (void)ser.load("/nonexistent/path/workspace.platemaker.json"),
        std::runtime_error
    );
}

TEST(WorkspaceSerializerTest, LoadMalformedJsonThrowsRuntimeError)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_malformed.json";

    { std::ofstream f(tmp); f << "{ this is not valid json @@@ }"; }

    const WorkspaceSerializer ser;
    EXPECT_THROW((void)ser.load(tmp.string()), std::runtime_error);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, LoadMissingVersionFieldThrowsRuntimeError)
{
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_noversion.json";

    { std::ofstream f(tmp); f << R"({"canvasProfiles":[],"outputProfiles":[]})"; }

    const WorkspaceSerializer ser;
    EXPECT_THROW((void)ser.load(tmp.string()), std::runtime_error);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, LoadVersionThatIsNotAnIntegerThrows)
{
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "pm_test_textversion.json";
    { std::ofstream f(tmp); f << R"({"version": "3", "canvasProfiles": [], "outputProfiles": []})"; }
    EXPECT_THROW((void)WorkspaceSerializer{}.load(tmp.string()), std::runtime_error);
    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, LoadPrePlatemakerProjectFormatExplainsTheIncompatibility)
{
    // A workspace from before projects existed stored "pages" at the top level. It must be refused
    // with a message that tells the user what to do, not half-loaded.
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "pm_test_legacy_pages.json";
    { std::ofstream f(tmp); f << R"({"version": 1, "pages": [], "canvasProfiles": [], "outputProfiles": []})"; }

    const WorkspaceSerializer ser;
    try {
        (void)ser.load(tmp.string());
        ADD_FAILURE() << "a legacy workspace must not load";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("workspace create"), std::string::npos) << e.what();
    }
    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, LoadPaletteOfTheWrongTypeIsASchemaError)
{
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "pm_test_badschema.json";
    { std::ofstream f(tmp); f << R"({"version": 1, "canvasProfiles": "not a list", "outputProfiles": []})"; }

    const WorkspaceSerializer ser;
    try {
        (void)ser.load(tmp.string());
        ADD_FAILURE() << "a malformed palette must not load";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("schema error"), std::string::npos) << e.what();
    }
    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, SaveIntoAMissingFolderThrows)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "pm-no-such-folder-91c2" / "ws.platemaker.json";
    ASSERT_FALSE(std::filesystem::exists(path.parent_path()));
    EXPECT_THROW(WorkspaceSerializer{}.save(makeMinimalWorkspace(), path.string()), std::runtime_error);
}

TEST(WorkspaceSerializerTest, SaveOverSomethingThatCannotBeReplacedThrows)
{
    namespace fs = std::filesystem;
    const fs::path dir  = fs::temp_directory_path() / "pm-ws-blocked";
    const fs::path path = dir / "ws.platemaker.json";
    fs::remove_all(dir);
    fs::create_directories(path / "occupied");

    EXPECT_THROW(WorkspaceSerializer{}.save(makeMinimalWorkspace(), path.string()), std::runtime_error);
    EXPECT_TRUE(fs::is_directory(path)) << "the destination is left as it was";
    EXPECT_FALSE(fs::exists(fs::path(path.string() + ".tmp"))) << "nor anything beside it";
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// CanvasProfile::id round-trip and back-compat
// ---------------------------------------------------------------------------

TEST(WorkspaceSerializerTest, RoundTripPreservesCanvasProfileId)
{
    const WorkspaceSerializer ser;
    const Models::Workspace   original = makeMinimalWorkspace();

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_cp_id.platemaker.json";

    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    ASSERT_EQ(loaded.canvasProfiles().size(), 1u);
    EXPECT_EQ(loaded.canvasProfiles()[0].id, "cp-test-001");

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, BackCompatLoadWithoutIdGetsAMintedId)
{
    // Workspace JSON that predates the 'id' field on CanvasProfile.
    //
    // Such a profile used to have its id *derived from its name* ("cp-" + name).  That was
    // a second identity scheme and it was not unique either — two profiles sharing a name
    // shared an id.  Since 0.2.1 the serializer mints a random unique id instead and
    // relinks the legacy references (covered by test_profile_ids.cpp); all this test cares
    // about is that the profile comes back usable, with *some* id that is not the old
    // name-derived one.
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_backcompat_id.json";

    {
        std::ofstream f(tmp);
        f << R"({
            "version": 1,
            "canvasProfiles": [{
                "name": "Webtoon Standard",
                "canvasSize": {"width": 1600, "height": 10240},
                "margins": {"top": 0, "right": 0, "bottom": 0, "left": 0}
            }],
            "outputProfiles": [],
            "activeCanvasProfileName": "Webtoon Standard",
            "activeOutputProfileName": "",
            "outputDirectory": ""
        })";
    }

    const WorkspaceSerializer ser;
    Models::Workspace loaded;
    ASSERT_NO_THROW(loaded = ser.load(tmp.string()));

    ASSERT_EQ(loaded.canvasProfiles().size(), 1u);
    EXPECT_FALSE(loaded.canvasProfiles()[0].id.empty());
    EXPECT_NE(loaded.canvasProfiles()[0].id, "cp-Webtoon Standard");
    EXPECT_EQ(loaded.canvasProfiles()[0].id.rfind("cp-", 0), 0u); // keeps the readable prefix

    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Optional processing steps (colour correction + strip overlays)
// ---------------------------------------------------------------------------

TEST(WorkspaceSerializerTest, RoundTripPreservesProcessingSteps)
{
    // Colour-correction params and strip overlays are per-project config; they must survive save/load
    // so a graded / annotated chapter reopens exactly as configured.
    const WorkspaceSerializer ser;
    Models::Workspace         original = makeMinimalWorkspace();

    Models::ProjectItem proj;
    proj.name = "Chapter";
    proj.uid  = "proj-proc-001";
    proj.colourCorrection.brightness        = 0.1;
    proj.colourCorrection.contrast          = 1.2;
    proj.colourCorrection.saturation        = 0.8;
    proj.colourCorrection.curves.master     = {{0.0, 0.0}, {0.5, 0.8}, {1.0, 1.0}};
    proj.colourCorrection.curves.red          = {{0.0, 0.0}, {1.0, 0.5}};
    proj.colourCorrection.excludedInputUids = {"file-001", "file-009"};
    Models::StripOverlay ovl;
    ovl.uid            = "ovl-1";
    ovl.assetPath      = "/tmp/bubble.png";
    ovl.sha256         = "deadbeef";
    ovl.anchorInputUid = "file-004";   // page-anchored: y is relative to that page's top
    ovl.xFrac          = 0.05;
    ovl.yFrac          = 1.875;
    ovl.enabled        = true;
    ovl.blend          = Models::BlendMode::Multiply;
    proj.getStripOverlays().push_back(std::move(ovl));
    original.projectItems.push_back(std::move(proj));

    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_processing.platemaker.json";
    ser.save(original, tmp.string());
    const auto loaded = ser.load(tmp.string());

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    const auto& p = loaded.projectItems.front();
    EXPECT_FALSE(Models::isNeutral(p.colourCorrection));
    EXPECT_DOUBLE_EQ(p.colourCorrection.brightness, 0.1);
    EXPECT_DOUBLE_EQ(p.colourCorrection.contrast,   1.2);
    EXPECT_DOUBLE_EQ(p.colourCorrection.saturation, 0.8);
    ASSERT_EQ(p.colourCorrection.curves.master.size(), 3u);
    EXPECT_DOUBLE_EQ(p.colourCorrection.curves.master[1].x, 0.5);
    EXPECT_DOUBLE_EQ(p.colourCorrection.curves.master[1].y, 0.8);
    ASSERT_EQ(p.colourCorrection.curves.red.size(), 2u);
    EXPECT_DOUBLE_EQ(p.colourCorrection.curves.red[1].y, 0.5);
    EXPECT_TRUE(p.colourCorrection.curves.green.empty());
    EXPECT_EQ(p.colourCorrection.excludedInputUids,
              (std::vector<std::string>{"file-001", "file-009"}));
    ASSERT_EQ(p.getStripOverlays().size(), 1u);
    EXPECT_EQ(p.getStripOverlays()[0].uid,        "ovl-1");
    EXPECT_EQ(p.getStripOverlays()[0].assetPath, "/tmp/bubble.png");
    EXPECT_EQ(p.getStripOverlays()[0].sha256,     "deadbeef");
    EXPECT_EQ(p.getStripOverlays()[0].anchorInputUid, "file-004")
        << "the anchor must survive the round-trip — without it the overlay reloads as an absolute "
           "strip-Y and lands on whatever artwork now happens to sit there";
    EXPECT_DOUBLE_EQ(p.getStripOverlays()[0].xFrac, 0.05);
    EXPECT_DOUBLE_EQ(p.getStripOverlays()[0].yFrac, 1.875);
    EXPECT_TRUE(p.getStripOverlays()[0].enabled);
    EXPECT_EQ(p.getStripOverlays()[0].blend, Models::BlendMode::Multiply);

    std::filesystem::remove(tmp);
}

TEST(WorkspaceSerializerTest, LegacyProjectLoadsProcessingDefaults)
{
    // A project written before optional processing steps existed — no colourCorrection / stripOverlays
    // keys.  It must load with the step disabled and no overlays, so the render is unchanged.
    const std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "pm_test_processing_legacy.platemaker.json";

    {
        std::ofstream f(tmp);
        f << R"({
            "version": 2,
            "canvasProfiles": [],
            "outputProfiles": [],
            "outputDirectory": "",
            "projectItems": [{
                "name": "Legacy",
                "uid": "proj-legacy",
                "inputFiles": [],
                "outputFiles": [],
                "outputDirectory": ""
            }]
        })";
    }

    const WorkspaceSerializer ser;
    Models::Workspace loaded;
    ASSERT_NO_THROW(loaded = ser.load(tmp.string()));

    ASSERT_EQ(loaded.projectItems.size(), 1u);
    const auto& p = loaded.projectItems.front();
    EXPECT_TRUE(Models::isNeutral(p.colourCorrection));
    EXPECT_TRUE(p.colourCorrection.excludedInputUids.empty());
    EXPECT_TRUE(p.getStripOverlays().empty());

    std::filesystem::remove(tmp);
}

// ---------------------------------------------------------------------------
// Paths relative to the workspace file — a folder that is moved, renamed or zipped still opens
// ---------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

/// A scratch root of its own per test, with a non-ASCII name: the paths travel as UTF-8 end to end.
struct RelScratch {
    fs::path root;
    explicit RelScratch(const char* name)
        : root(fs::temp_directory_path() / utf8ToPath(std::string("pm_relpaths_ż_") + name))
    {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~RelScratch() { std::error_code ec; fs::remove_all(root, ec); }
};

/// The form paths are compared in: UTF-8, normalised, '/' separators — what consumers write.
std::string g(const fs::path& p)
{
    const std::u8string s = p.lexically_normal().generic_u8string();
    return std::string(s.begin(), s.end());
}

void touchFile(const fs::path& p)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

std::string readText(const fs::path& p)
{
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::size_t occurrences(const std::string& text, const std::string& what)
{
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size()))
        ++n;
    return n;
}

/// A workspace with one project holding the given overlays (by asset path).
Models::Workspace withOverlays(const std::vector<std::string>& assets)
{
    Models::Workspace   ws = makeMinimalWorkspace();
    Models::ProjectItem proj;
    proj.name = "Chapter";
    proj.uid  = "proj-rel-001";
    int n = 0;
    for (const auto& a : assets) {
        Models::StripOverlay o;
        o.uid       = "ovl-" + std::to_string(++n);
        o.assetPath = a;
        proj.getStripOverlays().push_back(std::move(o));
    }
    ws.projectItems.push_back(std::move(proj));
    return ws;
}

} // anonymous namespace

TEST(WorkspaceSerializerRelativePaths, OnlyAnAssetInsideTheFolderGetsARelativeCopy)
{
    const RelScratch s("inside");
    const fs::path   inside  = s.root / "ws" / "overlays" / "a.svg";
    const fs::path   outside = s.root / "elsewhere" / "b.svg";
    touchFile(inside);
    touchFile(outside);
    const fs::path file = s.root / "ws" / "c.platemaker.json";

    const WorkspaceSerializer ser;
    const Models::Workspace   ws = withOverlays({g(inside), g(outside)});
    ser.save(ws, pathToUtf8(file));

    const std::string text = readText(file);
    EXPECT_EQ(occurrences(text, "\"assetPathRelative\""), 1u);
    EXPECT_NE(text.find("\"assetPathRelative\": \"overlays/a.svg\""), std::string::npos);
    // serialize() has no location, so it carries no relative copy — and change detection, which compares
    // two serialize() results, is the same wherever the workspace is saved.
    EXPECT_EQ(ser.serialize(ws).find("assetPathRelative"), std::string::npos);

    // In place, nothing changes: both paths come back exactly as written.
    const auto loaded = ser.load(pathToUtf8(file));
    ASSERT_EQ(loaded.projectItems.at(0).getStripOverlays().size(), 2u);
    EXPECT_EQ(loaded.projectItems[0].getStripOverlays()[0].assetPath, g(inside));
    EXPECT_EQ(loaded.projectItems[0].getStripOverlays()[1].assetPath, g(outside));
}

TEST(WorkspaceSerializerRelativePaths, AMovedFolderStillFindsItsOverlays)
{
    const RelScratch s("moved");
    const fs::path   asset = s.root / "before" / "overlays" / "a.svg";
    touchFile(asset);

    const WorkspaceSerializer ser;
    ser.save(withOverlays({g(asset)}), pathToUtf8(s.root / "before" / "c.platemaker.json"));

    fs::rename(s.root / "before", s.root / "after");   // moved, renamed, or zipped and unpacked elsewhere

    const auto loaded = ser.load(pathToUtf8(s.root / "after" / "c.platemaker.json"));
    EXPECT_EQ(loaded.projectItems.at(0).getStripOverlays().at(0).assetPath,
              g(s.root / "after" / "overlays" / "a.svg"));
}

TEST(WorkspaceSerializerRelativePaths, ARelativeCopyThatFindsNothingLeavesTheAbsolutePath)
{
    // The workspace file copied on its own, without its folder: the relative copy names nothing, so the
    // absolute path — still valid on this machine — is the one that stands. It is also what a reader from
    // before the key would use.
    const RelScratch s("copied");
    const fs::path   asset = s.root / "ws" / "overlays" / "a.svg";
    touchFile(asset);

    const WorkspaceSerializer ser;
    ser.save(withOverlays({g(asset)}), pathToUtf8(s.root / "ws" / "c.platemaker.json"));
    fs::create_directories(s.root / "copy");
    fs::copy_file(s.root / "ws" / "c.platemaker.json", s.root / "copy" / "c.platemaker.json");

    const auto loaded = ser.load(pathToUtf8(s.root / "copy" / "c.platemaker.json"));
    EXPECT_EQ(loaded.projectItems.at(0).getStripOverlays().at(0).assetPath, g(asset));
}

TEST(WorkspaceSerializerRelativePaths, PathsWithNoRootAreReadAgainstTheFolder)
{
    // What a hand-written or packaged file carries. The output directory is the exception: a CLI user's
    // relative --output is resolved against the working directory when rendering, and stays that way.
    const RelScratch s("rootless");

    Models::Workspace ws = withOverlays({"overlays/x.svg"});
    Models::InputFile page;
    page.uid      = "file-001";
    page.filePath = "pages/001.png";
    ws.projectItems[0].getInputImages().push_back(page);
    ws.projectItems[0].getOutputDirectory() = "out";

    const WorkspaceSerializer ser;
    const fs::path            file = s.root / "c.platemaker.json";
    ser.save(ws, pathToUtf8(file));
    const auto loaded = ser.load(pathToUtf8(file));

    const auto& p = loaded.projectItems.at(0);
    EXPECT_EQ(p.getStripOverlays().at(0).assetPath, g(s.root / "overlays" / "x.svg"));
    EXPECT_EQ(p.getInputImages().at(0).filePath, g(s.root / "pages" / "001.png"));
    EXPECT_EQ(p.getOutputDirectory(), "out");
}

} // namespace Platemaker::Infrastructure
