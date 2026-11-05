/**
 * \file tests/lib-fault-tests/test_libvips_failures.cpp
 * \brief Every libvips call the library checks, failing: the error it reports and nothing left behind.
 *
 * No image makes libvips fail on demand, so these paths are reached by making the call fail (fault.hpp).
 * Each test says what the caller is told — a typed exception naming the step, or the documented empty
 * result — and that every libvips object made on the way was released (Fault::liveObjects()). The guards
 * against libvips returning something other than it promises (no width, no alpha) get that image instead.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "fault.hpp"

#include <platemaker/core/colour_corrector/colour_corrector.hpp>
#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>
#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/core/scaler/scaler.hpp>
#include <platemaker/core/strip_overlay_compositor/strip_overlay_compositor.hpp>
#include <platemaker/core/template_generator/template_generator.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/colour_correction.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>

#include <vips/vips.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Platemaker {
namespace {

using Fault::thrown;

/// What one test arms: the call, which one of its calls, and what the caller is told.
struct Failure {
    const char* function;
    int         n;
    const char* message;
};

/// \p image as a buffer, with a reference kept here: a call that consumes the buffer leaves it alive, so
/// the objects counted before and after are the same ones.
Core::PixelBuffer shared(VipsImage* image)
{
    g_object_ref(image);
    return Core::PixelBuffer{image};
}

/// A 0-pixel-wide image: what no libvips loader returns, for the guards that check anyway.
VipsImage* noWidthImage()
{
    VipsImage* image = Fault::blackImage(1, 1, 3);
    image->Xsize     = 0;
    return image;
}

class LibvipsFailureTest : public FaultTest {};

// --- ColourCorrector ---------------------------------------------------------------------------------

TEST_F(LibvipsFailureTest, AGradeThatFailsSaysWhichStepAndLeaksNothing)
{
    Models::ColourCorrection cc;   // every step: curves, levels, saturation, on a page with alpha
    cc.curves.master = {{0.0, 0.1}, {1.0, 0.9}};
    cc.brightness    = 0.1;
    cc.saturation    = 0.5;
    VipsImage* const page = Fault::blackImage(8, 8, 4);

    for (const Failure& f : {Failure{"vips_extract_band", 1, "extract colour bands"},
                             Failure{"vips_extract_band", 2, "extract alpha band"},
                             Failure{"vips_image_new_from_memory_copy", 1, "build curve LUT"},
                             Failure{"vips_maplut", 1, "apply curves"},
                             Failure{"vips_image_new_matrixv", 1, "build saturation matrix"},
                             Failure{"vips_cast", 1, "cast back to source format"},
                             Failure{"vips_bandjoin2", 1, "rejoin alpha"}}) {
        SCOPED_TRACE(f.function);
        const int before = Fault::liveObjects();
        Fault::failCall(f.function, f.n);
        EXPECT_CONTAINS(thrown([&] { (void)Core::ColourCorrector{}.applyToBuffer(shared(page), cc); }), f.message);
        EXPECT_EQ(Fault::liveObjects(), before);
    }
    g_object_unref(page);
}

TEST_F(LibvipsFailureTest, AGradeOfRgbaBytesThatFailsSaysWhichStepAndLeaksNothing)
{
    Models::ColourCorrection cc;
    cc.brightness = 0.1;
    std::vector<unsigned char> rgba(4 * 4 * 4, 128);

    for (const Failure& f : {Failure{"vips_image_new_from_memory_copy", 1, "wrap RGBA buffer"},
                             Failure{"vips_copy", 1, "tag sRGB"},
                             Failure{"vips_image_write_to_memory", 1, "read graded pixels"}}) {
        SCOPED_TRACE(f.function);
        const int before = Fault::liveObjects();
        Fault::failCall(f.function, f.n);
        EXPECT_CONTAINS(thrown([&] { Core::ColourCorrector{}.applyToRgba(rgba.data(), 4, 4, cc); }), f.message);
        EXPECT_EQ(Fault::liveObjects(), before);
    }
    EXPECT_EQ(rgba, std::vector<unsigned char>(4 * 4 * 4, 128));   // untouched
}

TEST_F(LibvipsFailureTest, AGradeThatLosesTheAlphaBandIsRefused)
{
    Models::ColourCorrection cc;
    cc.brightness = 0.1;
    std::vector<unsigned char> rgba(4 * 4 * 4, 128);
    VipsImage* const rgb = Fault::blackImage(4, 4, 3);

    Fault::returnImage("vips_bandjoin2", rgb);   // the alpha rejoined, but three bands come back
    EXPECT_CONTAINS(thrown([&] { Core::ColourCorrector{}.applyToRgba(rgba.data(), 4, 4, cc); }),
                    "unexpected graded image layout");
    EXPECT_EQ(rgba, std::vector<unsigned char>(4 * 4 * 4, 128));
    g_object_unref(rgb);
}

// --- PixelBuffer --------------------------------------------------------------------------------------

TEST_F(LibvipsFailureTest, ACloneThatFailsThrows)
{
    const Core::PixelBuffer image{Fault::blackImage(4, 4, 3)};
    Fault::failCall("vips_copy");
    EXPECT_CONTAINS(thrown([&] { (void)image.clone(); }), "vips_copy failed");
}

// --- ScaledStrip --------------------------------------------------------------------------------------

TEST_F(LibvipsFailureTest, ASliceWhosePartFailsReleasesTheOthers)
{
    Core::ScaledStrip strip;
    strip.append(Core::ScaledImage{Core::PixelBuffer{Fault::blackImage(10, 10, 3)}, "a"});
    strip.append(Core::ScaledImage{Core::PixelBuffer{Fault::blackImage(10, 10, 3)}, "b"});
    Infrastructure::CancellationToken cancel;
    const int before = Fault::liveObjects();

    Fault::failCall("vips_extract_area", 2);   // the second page's part; the first one's is made
    EXPECT_CONTAINS(thrown([&] {
                        strip.sliceAll(20, Models::LastSlicePolicy::KeepAsIs, cancel,
                                       [](Core::SliceResult&&) { return true; });
                    }),
                    "vips_extract_area failed (slice 0)");
    EXPECT_EQ(Fault::liveObjects(), before);
}

TEST_F(LibvipsFailureTest, ASliceOfAnotherHeightThanAskedThrows)
{
    Core::ScaledStrip strip;
    strip.append(Core::ScaledImage{Core::PixelBuffer{Fault::blackImage(10, 20, 3)}, "a"});
    Infrastructure::CancellationToken cancel;
    VipsImage* const padded = Fault::blackImage(10, 21, 3);

    Fault::returnImage("vips_extract_area", padded);   // the black-band bug: one row more than the window
    EXPECT_CONTAINS(thrown([&] {
                        strip.sliceAll(20, Models::LastSlicePolicy::KeepAsIs, cancel,
                                       [](Core::SliceResult&&) { return true; });
                    }),
                    "slice 0 built height 21 != requested 20");
    g_object_unref(padded);
}

TEST_F(LibvipsFailureTest, ATailThatCannotBePaddedThrows)
{
    Core::ScaledStrip strip;
    strip.append(Core::ScaledImage{Core::PixelBuffer{Fault::blackImage(10, 15, 3)}, "a"});
    Infrastructure::CancellationToken cancel;

    Fault::failCall("vips_embed");
    EXPECT_CONTAINS(thrown([&] {
                        strip.sliceAll(10, Models::LastSlicePolicy::PadWhite, cancel,
                                       [](Core::SliceResult&&) { return true; });
                    }),
                    "(PadWhite) — vips_embed failed");
}

// --- Scaler -------------------------------------------------------------------------------------------

TEST_F(LibvipsFailureTest, ScalingAFileThatFailsSaysWhichStepAndLeaksNothing)
{
    const Fault::Scratch scratch;
    VipsImage* const     page = Fault::blackImage(20, 10, 3);
    const std::string    file = Fault::save(page, scratch.file("page.png"));
    g_object_unref(page);
    VipsImage* const noWidth = noWidthImage();

    for (const auto& [arm, message] :
         std::vector<std::pair<std::function<void()>, const char*>>{
             {[] { Fault::failCall("vips_autorot"); }, "vips_autorot failed"},
             {[] { Fault::failCall("vips_resize"); }, "vips_resize failed"},
             {[&] { Fault::returnImage("vips_image_new_from_file", noWidth); }, "has zero width"}}) {
        SCOPED_TRACE(message);
        const int before = Fault::liveObjects();
        arm();
        EXPECT_CONTAINS(thrown([&] { (void)Core::Scaler{}.scale(file, 10); }), message);
        EXPECT_EQ(Fault::liveObjects(), before);
    }
    g_object_unref(noWidth);
}

TEST_F(LibvipsFailureTest, ScalingABufferThatFailsThrows)
{
    VipsImage* const page = Fault::blackImage(20, 10, 3);
    Fault::failCall("vips_resize");
    EXPECT_CONTAINS(thrown([&] { (void)Core::Scaler{}.scale(shared(page), "page", 10); }), "vips_resize failed");
    g_object_unref(page);

    EXPECT_CONTAINS(thrown([&] { (void)Core::Scaler{}.scale(Core::PixelBuffer{noWidthImage()}, "page", 10); }),
                    "source buffer has zero width");
}

// --- StripOverlayCompositor ----------------------------------------------------------------------------

TEST_F(LibvipsFailureTest, AnOverlayThatCannotBeMadeRgbaIsNoOverlay)
{
    const Fault::Scratch scratch;
    VipsImage* const     grey = Fault::blackImage(4, 4, 1, VIPS_INTERPRETATION_B_W);
    VipsImage* const     rgb  = Fault::blackImage(4, 4, 3);
    const std::string    greyFile = Fault::save(grey, scratch.file("grey.png"));
    const std::string    rgbFile  = Fault::save(rgb, scratch.file("rgb.png"));
    g_object_unref(grey);
    g_object_unref(rgb);
    const Core::StripOverlayCompositor compositor;

    for (const auto& [function, file] : {std::pair{"vips_colourspace", greyFile}, std::pair{"vips_addalpha", rgbFile}}) {
        SCOPED_TRACE(function);
        const int before = Fault::liveObjects();
        Fault::failCall(function);
        EXPECT_FALSE(compositor.rasterizeOverlay(file).isValid());
        EXPECT_EQ(Fault::liveObjects(), before);
        vips_error_clear();
    }
}

TEST_F(LibvipsFailureTest, AnOverlayOfAnotherDepthIsCastToBytesOrIsNoOverlay)
{
    const Fault::Scratch scratch;
    VipsImage* const     bytes = Fault::blackImage(4, 4, 4);
    VipsImage*           floats = nullptr;
    ASSERT_EQ(vips_cast(bytes, &floats, VIPS_FORMAT_FLOAT, nullptr), 0) << vips_error_buffer();
    const std::string file = Fault::save(floats, scratch.file("float.v"));   // sRGB, so only the cast changes it
    g_object_unref(bytes);
    g_object_unref(floats);
    const Core::StripOverlayCompositor compositor;

    const int                 casts  = Fault::calls("vips_cast");
    const Core::OverlayRaster raster = compositor.rasterizeOverlayRgba(file);
    EXPECT_EQ(raster.rgba.size(), 4u * 4u * 4u);
    EXPECT_EQ(Fault::calls("vips_cast"), casts + 1);

    for (const char* function : {"vips_cast", "vips_image_write_to_memory"}) {
        SCOPED_TRACE(function);
        const int before = Fault::liveObjects();
        Fault::failCall(function);
        EXPECT_FALSE(compositor.rasterizeOverlayRgba(file).isValid());
        EXPECT_EQ(Fault::liveObjects(), before);
        vips_error_clear();
    }
}

TEST_F(LibvipsFailureTest, AnSvgWithoutAlphaGainsOneOrIsNoOverlay)
{
    const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"/>)";
    VipsImage* const  rgb = Fault::blackImage(4, 4, 3);
    const Core::StripOverlayCompositor compositor;

    Fault::returnImage("vips_svgload_buffer", rgb);
    EXPECT_EQ(compositor.rasterizeSvgRgba(svg).rgba.size(), 4u * 4u * 4u);

    const int before = Fault::liveObjects();
    Fault::returnImage("vips_svgload_buffer", rgb);
    Fault::failCall("vips_addalpha");
    EXPECT_FALSE(compositor.rasterizeSvgRgba(svg).isValid());
    EXPECT_EQ(Fault::liveObjects(), before);
    g_object_unref(rgb);
}

TEST_F(LibvipsFailureTest, AnOverlayWithNoWidthIsNoOverlay)
{
    VipsImage* const noWidth = noWidthImage();
    Fault::returnImage("vips_image_new_from_file", noWidth);
    EXPECT_FALSE(Core::StripOverlayCompositor{}.rasterizeOverlayAtWidth("any.png", 10).isValid());
    g_object_unref(noWidth);
}

TEST_F(LibvipsFailureTest, AnOverlayThatCannotBePlacedFailsTheSliceAndLeaksNothing)
{
    VipsImage* const slice = Fault::blackImage(10, 10, 3);
    std::vector<Core::LoadedOverlay> overlays(1);
    overlays[0].bitmap = Core::PixelBuffer{Fault::blackImage(2, 2, 4)};
    overlays[0].w      = 2;
    overlays[0].h      = 2;

    const int before = Fault::liveObjects();
    Fault::failCall("vips_embed");
    EXPECT_CONTAINS(thrown([&] { (void)Core::StripOverlayCompositor{}.composite(shared(slice), 0, overlays); }),
                    "embed overlay layer");
    EXPECT_EQ(Fault::liveObjects(), before);
    g_object_unref(slice);
}

// --- TemplateGenerator --------------------------------------------------------------------------------

Models::CanvasProfile templateProfile()
{
    Models::CanvasProfile cp;
    cp.name       = "T";
    cp.canvasSize = {40, 30};
    cp.margins    = {2, 3, 4, 5};
    return cp;
}

TEST_F(LibvipsFailureTest, ATemplateThatCannotBeDrawnOrEncodedThrowsAndLeaksNothing)
{
    const Fault::Scratch     scratch;
    Core::TemplateOptions    psd;
    psd.format = Models::TemplateFormat::Psd;

    for (const auto& [f, options] :
         {std::pair{Failure{"vips_image_new_memory", 1, "vips_image_new_memory() returned null"}, Core::TemplateOptions{}},
          std::pair{Failure{"vips_image_write_prepare", 1, "vips_image_write_prepare failed"}, Core::TemplateOptions{}},
          std::pair{Failure{"vips_pngsave_buffer", 1, "cannot encode the PNG"}, Core::TemplateOptions{}},
          std::pair{Failure{"vips_copy", 1, "cannot encode the PSD"}, psd},
          std::pair{Failure{"vips_target_new_to_memory", 1, "cannot encode the PSD"}, psd}}) {
        SCOPED_TRACE(f.function);
        const int before = Fault::liveObjects();
        Fault::failCall(f.function, f.n);
        EXPECT_CONTAINS(thrown([&] {
                            (void)Core::TemplateGenerator{}.generate(templateProfile(), {}, scratch.file("t"), options);
                        }),
                        f.message);
        EXPECT_EQ(Fault::liveObjects(), before);
    }
}

TEST_F(LibvipsFailureTest, ARectangleThatCannotBeDrawnLeavesTheTemplateWithoutIt)
{
    const Fault::Scratch scratch;
    Fault::failCall("vips_draw_rect");
    EXPECT_FALSE(Core::TemplateGenerator{}.generate(templateProfile(), {}, scratch.file("t.png")).empty());
}

// --- ProcessingPipeline::decodePageToRgba ---------------------------------------------------------------

TEST_F(LibvipsFailureTest, APageThatCannotBeDecodedToRgbaThrowsAndLeaksNothing)
{
    const Fault::Scratch scratch;
    VipsImage* const     page = Fault::blackImage(20, 10, 3);
    Models::InputFile    input;
    input.filePath = Fault::save(page, scratch.file("page.png"));
    g_object_unref(page);
    Models::OutputProfile out;
    out.targetWidth = 20;
    out.sliceHeight = 10;
    std::vector<unsigned char> rgba(20 * 10 * 4);

    for (const Failure& f : {Failure{"vips_addalpha", 1, "add alpha"},
                             Failure{"vips_image_new_from_memory", 1, "read pixels"},
                             Failure{"vips_image_write", 1, "read pixels"}}) {
        SCOPED_TRACE(f.function);
        const int before = Fault::liveObjects();
        Fault::failCall(f.function, f.n);
        EXPECT_CONTAINS(thrown([&] {
                            Core::ProcessingPipeline::decodePageToRgba(input, out, {}, {}, rgba.data(), 20, 10);
                        }),
                        f.message);
        EXPECT_EQ(Fault::liveObjects(), before);
    }
}

// --- ImageIO ------------------------------------------------------------------------------------------

TEST_F(LibvipsFailureTest, DecodingAndEncodingThatFailThrowAndLeakNothing)
{
    const Fault::Scratch          scratch;
    VipsImage* const              page = Fault::blackImage(8, 8, 3);
    const std::string             file = Fault::save(page, scratch.file("page.png"));
    const Infrastructure::ImageIO io;

    int before = Fault::liveObjects();
    Fault::failCall("vips_autorot");
    EXPECT_CONTAINS(thrown([&] { (void)io.decode(file); }), "vips_autorot failed");
    EXPECT_EQ(Fault::liveObjects(), before);

    before = Fault::liveObjects();
    Fault::failCall("vips_copy");
    EXPECT_CONTAINS(thrown([&] { io.encode(shared(page), scratch.file("out.png"), Models::OutputProfile{}); }),
                    "cannot set the output resolution");
    EXPECT_EQ(Fault::liveObjects(), before);
    g_object_unref(page);
}

TEST_F(LibvipsFailureTest, AColourProfileThatCannotBeAppliedIsSkipped)
{
    const Fault::Scratch scratch;
    VipsImage* const     page = Fault::blackImage(8, 8, 3);
    const std::string    file = Fault::save(page, scratch.file("page.png"));
    g_object_unref(page);

    Fault::failCall("vips_icc_transform");
    const Core::PixelBuffer decoded = Infrastructure::ImageIO{}.decode(file);
    EXPECT_EQ(decoded.width(), 8);
    EXPECT_STREQ(vips_error_buffer(), "");
}

} // namespace
} // namespace Platemaker
