/**
 * \file lib/src/core/template_generator/template_generator.cpp
 * \brief TemplateGenerator implementation — renders a canvas template PNG.
 *
 * Uses libvips draw operations on an in-memory RGBA image.  The image is
 * always saved as a lossless PNG regardless of the workspace output format.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-02
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/core/template_generator/template_generator.hpp>

#include <vips/vips.h>

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Platemaker::Core {

namespace {

/// Converts an RGBA struct to a 4-element double array expected by vips_draw_*.
inline void toInk(const Models::RGBA& col, double (&ink)[4]) noexcept
{
    ink[0] = static_cast<double>(col.r);
    ink[1] = static_cast<double>(col.g);
    ink[2] = static_cast<double>(col.b);
    ink[3] = static_cast<double>(col.a);
}

/**
 * \brief Fills a rectangle on \p img with \p col.
 *
 * Silently ignores out-of-bounds or zero-area rectangles.  Does not abort
 * the template generation if a single draw call fails (best-effort).
 */
void fillRect(VipsImage* img,
              int left, int top, int width, int height,
              const Models::RGBA& col) noexcept
{
    if (width <= 0 || height <= 0) return;
    double ink[4];
    toInk(col, ink);
    if (vips_draw_rect(img, ink, 4, left, top, width, height, "fill", 1, nullptr) != 0)
        vips_error_clear(); // non-fatal, best-effort
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// TemplateGenerator::generate
// ---------------------------------------------------------------------------

void TemplateGenerator::generate(
    const Models::CanvasProfile& canvasProfile,
    const Models::OutputProfile& outputProfile,
    const std::string&           outputPath) const
{
    using namespace Models;

    // Compile-time switch for the safe-area border and slice-guide lines (sections 3 & 4 below).
    // They are off for now; the code is kept behind this flag for possible future reuse. While it is
    // 0, the guide lines are the only thing that reads outputProfile, so the parameter is unused.
#define GUIDELINES_ENABLED 0

#if !GUIDELINES_ENABLED
    (void)outputProfile; // unused while the guide lines (its only consumer) are compiled out
#endif
    const int W = canvasProfile.canvasSize.width;
    const int H = canvasProfile.canvasSize.height;

    if (W <= 0 || H <= 0)
        throw std::invalid_argument(
            "TemplateGenerator: canvas size must be positive, got " +
            std::to_string(W) + "x" + std::to_string(H));

    const Margins& m  = canvasProfile.margins;
    const int safeL   = m.left;
    const int safeT   = m.top;
    const int safeR   = W - m.right;
    const int safeB   = H - m.bottom;
    const int safeW   = safeR - safeL;
    const int safeHpx = safeB - safeT;

    if (safeW <= 0 || safeHpx <= 0)
        throw std::invalid_argument(
            "TemplateGenerator: margins exceed canvas dimensions");

    // -----------------------------------------------------------------------
    // Create a writable 4-band (RGBA) image in memory.
    //
    // We use vips_image_new_memory() + vips_image_init_fields() +
    // vips_image_write_prepare() so that vips_draw_* operations can modify
    // the pixel buffer in-place.  This approach is compatible with all
    // libvips 8.x versions (no vips_copy_memory dependency).
    // -----------------------------------------------------------------------
    VipsImage* canvas = vips_image_new_memory();
    if (!canvas) {
        throw std::runtime_error(
            "TemplateGenerator: vips_image_new_memory() returned null");
    }

    vips_image_init_fields(
        canvas,
        W, H,
        4,                          // bands: R G B A
        VIPS_FORMAT_UCHAR,
        VIPS_CODING_NONE,
        VIPS_INTERPRETATION_sRGB,
        1.0, 1.0);

    if (vips_image_write_prepare(canvas) != 0) {
        g_object_unref(canvas);
        const std::string err = vips_error_buffer();
        vips_error_clear();
        throw std::runtime_error(
            "TemplateGenerator: vips_image_write_prepare failed: " + err);
    }

    // Zero-initialise to fully transparent black (all channels = 0).
    std::memset(VIPS_IMAGE_ADDR(canvas, 0, 0),
                0,
                static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4u);

    // -----------------------------------------------------------------------
    // 1. Fill entire canvas with background colour.
    // -----------------------------------------------------------------------
    fillRect(canvas, 0, 0, W, H, canvasProfile.backgroundColour);

    // -----------------------------------------------------------------------
    // 2. Paint margin zones with the visual overlay colour.
    //    Four rectangular strips: top / bottom / left (between top+bottom) /
    //    right (between top+bottom).
    // -----------------------------------------------------------------------
    const RGBA& vis = canvasProfile.visualColour;
    fillRect(canvas, 0,     0,     W,       m.top,    vis); // top strip
    fillRect(canvas, 0,     safeB, W,       m.bottom, vis); // bottom strip
    fillRect(canvas, 0,     m.top, m.left,  safeHpx,  vis); // left strip
    fillRect(canvas, safeR, m.top, m.right, safeHpx,  vis); // right strip

    // -----------------------------------------------------------------------
    // 3. Draw safe-area border (2 px, dark charcoal, nearly opaque).  [compiled out — see the switch]
    // -----------------------------------------------------------------------
#if GUIDELINES_ENABLED
    constexpr RGBA kBorder{40, 40, 40, 220};
    constexpr int  kBW = 2;
    fillRect(canvas, safeL,        safeT,        safeW, kBW,         kBorder); // top
    fillRect(canvas, safeL,        safeB - kBW,  safeW, kBW,         kBorder); // bottom
    fillRect(canvas, safeL,        safeT,        kBW,   safeHpx,     kBorder); // left
    fillRect(canvas, safeR - kBW,  safeT,        kBW,   safeHpx,     kBorder); // right
#endif

    // -----------------------------------------------------------------------
    // 4. Draw horizontal slice guide lines inside the safe area.
    //
    //    The guide positions are in canvas-space pixels.  The slice height is
    //    given in output-space pixels (after scaling to targetWidth), so we
    //    scale it back to canvas space using:
    //        scaledSliceH = sliceHeight × (canvasWidth / targetWidth)
    //
    //    Guide lines start at (safeT + scaledSliceH) and repeat every
    //    scaledSliceH pixels downward, stopping before safeB.
    // -----------------------------------------------------------------------
#if GUIDELINES_ENABLED
    if (outputProfile.targetWidth > 0 && outputProfile.sliceHeight > 0) {
        const double scaleFactor =
            static_cast<double>(W) / static_cast<double>(outputProfile.targetWidth);
        const double scaledSliceH =
            static_cast<double>(outputProfile.sliceHeight) * scaleFactor;

        // Blue-grey, semi-transparent guide line.
        constexpr RGBA kGuide{60, 80, 200, 180};

        double guideYf = static_cast<double>(safeT) + scaledSliceH;
        while (true) {
            const int gy = static_cast<int>(std::round(guideYf));
            if (gy >= safeB) break;
            fillRect(canvas, safeL, gy, safeW, 1, kGuide);
            guideYf += scaledSliceH;
        }
    }
#endif

    // -----------------------------------------------------------------------
    // 5. Save as PNG (always lossless; alpha preserved).
    // -----------------------------------------------------------------------
    if (vips_pngsave(canvas, outputPath.c_str(), nullptr) != 0) {
        g_object_unref(canvas);
        const std::string err = vips_error_buffer();
        vips_error_clear();
        throw std::runtime_error(
            "TemplateGenerator: cannot save PNG to '" + outputPath + "': " + err);
    }

    g_object_unref(canvas);
}

#undef GUIDELINES_ENABLED

// ---------------------------------------------------------------------------
// signature — canvas-only template identity
// ---------------------------------------------------------------------------

std::string TemplateGenerator::signature(const Models::CanvasProfile& cp)
{
    // Deterministic, human-inspectable concatenation of the fields that define
    // the rendered template. Field tags guard against ambiguous concatenations.
    std::string s;
    s.reserve(96);
    s += "cw=" + std::to_string(cp.canvasSize.width);
    s += ";ch=" + std::to_string(cp.canvasSize.height);
    s += ";mt=" + std::to_string(cp.margins.top);
    s += ";mr=" + std::to_string(cp.margins.right);
    s += ";mb=" + std::to_string(cp.margins.bottom);
    s += ";ml=" + std::to_string(cp.margins.left);
    s += ";vc=" + std::to_string(cp.visualColour.r) + ',' +
                  std::to_string(cp.visualColour.g) + ',' +
                  std::to_string(cp.visualColour.b) + ',' +
                  std::to_string(cp.visualColour.a);
    s += ";bg=" + std::to_string(cp.backgroundColour.r) + ',' +
                  std::to_string(cp.backgroundColour.g) + ',' +
                  std::to_string(cp.backgroundColour.b) + ',' +
                  std::to_string(cp.backgroundColour.a);
    return s;
}

} // namespace Platemaker::Core
