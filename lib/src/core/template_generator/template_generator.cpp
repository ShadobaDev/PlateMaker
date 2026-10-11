/**
 * \file lib/src/core/template_generator/template_generator.cpp
 * \brief TemplateGenerator implementation — renders a canvas template PNG or PSD.
 *
 * Uses libvips draw operations on an in-memory RGBA image, saved losslessly as PNG (libvips) or as a
 * layered PSD (minipsd), regardless of the workspace output format.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-02
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <platemaker/core/template_generator/template_generator.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/file/path_utf8.hpp>

#include <vips/vips.h>

// minipsd's functions stay private to this file: the lib exports none of them. The bytes go to memory
// (to be hashed, then written), so its file writer is left out.
#define MINIPSD_STATIC
#define MINIPSD_NO_STDIO
#define MINIPSD_IMPLEMENTATION
#include <minipsd.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

using Bytes = std::vector<std::uint8_t>;

/// Encodes the drawn canvas as a PSD: the template layer, and an empty one above it on request.
Bytes encodePsd(VipsImage* canvas, const TemplateOptions& options)
{
    const minipsd_layer layers[2] = {
        {"Template", static_cast<const uint8_t*>(VIPS_IMAGE_ADDR(canvas, 0, 0)), 255, 0},
        {"Layer",    nullptr,                                                   255, 0},
    };
    minipsd_doc doc{};
    doc.width       = static_cast<uint32_t>(canvas->Xsize);
    doc.height      = static_cast<uint32_t>(canvas->Ysize);
    doc.dpi         = options.dpi;
    doc.layers      = layers;
    doc.layer_count = options.emptyLayer ? 2 : 1;
    doc.compression = MINIPSD_RLE;

    Bytes bytes;
    const auto append = [](void* user, const void* data, size_t size) -> int {
        auto* out = static_cast<Bytes*>(user);
        const auto* p = static_cast<const std::uint8_t*>(data);
        out->insert(out->end(), p, p + size);
        return 1;
    };
    const int rc = minipsd_write(&doc, append, &bytes);
    if (rc == MINIPSD_ERR_SIZE)
        throw std::invalid_argument(
            "TemplateGenerator: a PSD side cannot exceed " + std::to_string(MINIPSD_MAX_SIDE) + " px, got " +
            std::to_string(doc.width) + "x" + std::to_string(doc.height));
    if (rc != MINIPSD_OK)
        throw std::runtime_error(std::string("TemplateGenerator: cannot encode the PSD: ") + minipsd_error_string(rc));
    return bytes;
}

/// Encodes the drawn canvas as a PNG (lossless; alpha and the resolution kept).
Bytes encodePng(VipsImage* canvas)
{
    void*  buf = nullptr;
    size_t len = 0;
    if (vips_pngsave_buffer(canvas, &buf, &len, nullptr) != 0) {
        const std::string err = vips_error_buffer();
        vips_error_clear();
        throw std::runtime_error("TemplateGenerator: cannot encode the PNG: " + err);
    }
    Bytes bytes(static_cast<const std::uint8_t*>(buf), static_cast<const std::uint8_t*>(buf) + len);
    g_free(buf);
    return bytes;
}

/// "k=v;k=v" into its pairs, in order.
std::vector<std::pair<std::string, std::string>> signatureFields(const std::string& signature)
{
    std::vector<std::pair<std::string, std::string>> fields;
    std::size_t start = 0;
    while (start < signature.size()) {
        std::size_t end = signature.find(';', start);
        if (end == std::string::npos) end = signature.size();
        const std::string part = signature.substr(start, end - start);
        const auto eq = part.find('=');
        if (eq != std::string::npos) fields.emplace_back(part.substr(0, eq), part.substr(eq + 1));
        start = end + 1;
    }
    return fields;
}

/// Two workspace-relative paths name the same file — as Windows sees them: either slash, any case.
bool samePath(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto norm = [](char c) {
            return c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        };
        if (norm(a[i]) != norm(b[i])) return false;
    }
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// TemplateGenerator::generate
// ---------------------------------------------------------------------------

std::string TemplateGenerator::generate(
    const Models::CanvasProfile& canvasProfile,
    const Models::OutputProfile& outputProfile,
    const std::string&           outputPath,
    const TemplateOptions&       options) const
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

    if (!(options.dpi > 0.0))
        throw std::invalid_argument(
            "TemplateGenerator: dpi must be positive, got " + std::to_string(options.dpi));
    // libvips keeps resolution in pixels per millimetre; pngsave writes it to the pHYs chunk.
    const double pxPerMm = options.dpi / 25.4;

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
        pxPerMm, pxPerMm);

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
    // 5. Save, losslessly with alpha: a layered PSD, or a PNG — encoded in memory, so the hash is of
    //    exactly the bytes written.
    // -----------------------------------------------------------------------
    Bytes bytes;
    try {
        bytes = options.format == TemplateFormat::Psd ? encodePsd(canvas, options) : encodePng(canvas);
    } catch (...) {
        g_object_unref(canvas);
        throw;
    }
    g_object_unref(canvas);

    std::ofstream out(Infrastructure::utf8ToPath(outputPath), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out)
        throw std::runtime_error("TemplateGenerator: cannot write '" + outputPath + "'");
    return Infrastructure::FileMetaData::computeSha256(bytes.data(), bytes.size());
}

#undef GUIDELINES_ENABLED

TemplateOptions TemplateGenerator::optionsFor(const Models::CanvasProfile& cp)
{
    TemplateOptions o;
    o.dpi        = cp.dpi;
    o.format     = cp.templateSettings.format;
    o.emptyLayer = cp.templateSettings.emptyLayer;
    return o;
}

// ---------------------------------------------------------------------------
// signature — canvas-only template identity
// ---------------------------------------------------------------------------

std::string TemplateGenerator::canvasSignature(const Models::CanvasProfile& cp)
{
    return templateSignature(cp, cp.templateSettings.format);
}

std::string TemplateGenerator::templateSignature(const Models::CanvasProfile& cp, Models::TemplateFormat format)
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
    s += ";dpi=" + Models::signatureDecimal(cp.dpi);
    // How the file is written is part of what it is: a PNG is not the PSD the profile now asks for. The
    // empty layer only exists in a PSD, so a PNG's signature ignores that setting.
    const bool psd = format == Models::TemplateFormat::Psd;
    s += psd ? ";fmt=psd" : ";fmt=png";
    if (psd)
        s += cp.templateSettings.emptyLayer ? ";layer=1" : ";layer=0";
    return s;
}

// ---------------------------------------------------------------------------
// check — is one tracked template still what the profile asks for?
// ---------------------------------------------------------------------------

TemplateCheck TemplateGenerator::check(const std::vector<Models::CanvasProfile>& palette,
                                       const Models::CanvasProfile&              cp,
                                       const Models::CanvasTemplateInfo&         entry,
                                       const std::string&                        absolutePath)
{
    TemplateCheck c;

    std::error_code ec;
    c.fileMissing = !std::filesystem::exists(Infrastructure::utf8ToPath(absolutePath), ec);

    // A path two profiles track is a problem only for the one whose file now holds the other's content:
    // while both were identical — or before the other wrote — the bytes are still this template's.
    //  - With a recorded hash the file says it itself: it is this entry's while the hash matches; if
    //    not, and another profile tracks the path, that profile overwrote it (not an edit elsewhere).
    //  - Without one (an entry migrated from before hashes), it was overwritten if another profile
    //    wrote the path later, with a signature other than the one this entry needs.
    const std::string fileHash = (!c.fileMissing && !entry.sha256.empty())
        ? Infrastructure::FileMetaData::computeFileSha256(absolutePath) : std::string{};
    const bool        ownBytes = !entry.sha256.empty() && fileHash == entry.sha256;
    const std::string needed   = templateSignature(cp, entry.format);
    for (const auto& other : palette) {
        if (other.id == cp.id || Models::sameProfileContent(other, cp)) continue;
        for (const auto& t : other.templates) {
            if (!samePath(t.path, entry.path) || ownBytes) continue;
            const bool overwritten = !entry.sha256.empty()
                || (t.generatedAt > entry.generatedAt && t.fingerprint != needed);
            if (overwritten) { c.sharedWith = other.name; break; }
        }
        if (!c.sharedWith.empty()) break;
    }

    // Only a recorded hash can say the file changed: an entry migrated from before hashes has none. A
    // change already explained by a namesake overwriting the file is reported as that, not twice.
    if (!c.fileMissing && !entry.sha256.empty() && c.sharedWith.empty())
        c.changedOutside = fileHash != entry.sha256;

    // Field by field, so a reason can name what changed: "dpi 25.4 → 300".
    const auto was = signatureFields(entry.fingerprint);
    for (const auto& [field, now] : signatureFields(templateSignature(cp, entry.format))) {
        std::string old;
        for (const auto& [f, v] : was)
            if (f == field) { old = v; break; }
        if (old != now) c.differences.push_back({field, old, now});
    }
    return c;
}

} // namespace Platemaker::Core
