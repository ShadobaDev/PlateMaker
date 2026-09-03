/**
 * \file lib/src/core/colour_corrector/colour_corrector.cpp
 * \brief ColourCorrector implementation — brightness / contrast / saturation via libvips.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-08-30
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/core/colour_corrector/colour_corrector.hpp>

#include <platemaker/infrastructure/log/log.hpp>

#include <vips/vips.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace { namespace Log = Platemaker::Infrastructure::Log; }

namespace Platemaker::Core {

namespace {

//! Full-scale value for a band format — the point brightness/contrast maths is expressed in native
//! units (so mid-grey and the additive lift track the bit depth). Defaults to 8-bit for anything else.
double formatMax(VipsBandFormat fmt)
{
    switch (fmt) {
        case VIPS_FORMAT_USHORT: return 65535.0;
        case VIPS_FORMAT_UINT:   return 4294967295.0;
        default:                 return 255.0; // uchar and everything else
    }
}

//! Evaluate a tone curve at \p t (0..1). Empty = identity; points must be sorted ascending by x;
//! outside the point range the nearest endpoint's y is held (clamp).
double evalCurve(const std::vector<Models::CurvePoint>& pts, double t)
{
    if (pts.empty())          return t;
    if (t <= pts.front().x)   return pts.front().y;
    if (t >= pts.back().x)    return pts.back().y;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (t <= pts[i].x) {
            const auto&  a  = pts[i - 1];
            const auto&  b  = pts[i];
            const double dx = b.x - a.x;
            const double f  = dx > 1e-9 ? (t - a.x) / dx : 0.0;
            return a.y + f * (b.y - a.y);
        }
    }
    return pts.back().y;
}

//! Clamp a normalised [0,1] value to a rounded 0..255 byte.
std::uint8_t toByte(double v01)
{
    const double s = v01 * 255.0;
    if (s <= 0.0)   return 0;
    if (s >= 255.0) return 255;
    return static_cast<std::uint8_t>(s + 0.5);
}

//! Unref \p img if non-null and throw — keeps the error paths leak-free without a web of gotos.
[[noreturn]] void failVips(VipsImage* a, VipsImage* b, const std::string& what)
{
    const std::string err = vips_error_buffer();
    vips_error_clear();
    if (a) g_object_unref(a);
    if (b) g_object_unref(b);
    throw std::runtime_error("ColourCorrector::apply — " + what + ": " + err);
}

} // namespace

PixelBuffer ColourCorrector::apply(PixelBuffer buffer, const Models::ColourCorrection& cc) const
{
    // A neutral grade touches no pixels — enabling the step with default scalars and no curves is a
    // true no-op. (ICC → sRGB is handled at load, not here; see the class doc.)
    const bool neutralLevels = (cc.brightness == 0.0 && cc.contrast == 1.0);
    const bool neutralSat    = (cc.saturation == 1.0);
    if (neutralLevels && neutralSat && !Models::hasAnyCurve(cc.curves))
        return buffer;

    if (!buffer.isValid())
        throw std::runtime_error("ColourCorrector::apply — source buffer is invalid (null VipsImage)");

    VipsImage* const     in          = buffer.get(); // owned by `buffer`; never unref'd here
    const int            bands       = in->Bands;
    const bool           hasAlpha    = vips_image_hasalpha(in) != 0;
    const int            colourBands = hasAlpha ? bands - 1 : bands;
    const VipsBandFormat fmt         = in->BandFmt;
    const double         maxv        = formatMax(fmt);

    // Split the alpha off so the grade never touches transparency; rejoined untouched at the end.
    VipsImage* colour = nullptr;
    VipsImage* alpha  = nullptr;
    if (hasAlpha) {
        if (vips_extract_band(in, &colour, 0, "n", colourBands, nullptr) != 0)
            failVips(nullptr, nullptr, "extract colour bands");
        if (vips_extract_band(in, &alpha, colourBands, "n", 1, nullptr) != 0)
            failVips(colour, nullptr, "extract alpha band");
    } else {
        colour = in;
        g_object_ref(colour); // own a ref so unref bookkeeping is uniform with the split path
    }

    // Tone curves (8-bit, 3-band colour only): map each channel through channelCurve(masterCurve(v)) via
    // a 256-entry LUT. Applied first, on the raw uchar values, before the float scalar maths below.
    if (Models::hasAnyCurve(cc.curves) && fmt == VIPS_FORMAT_UCHAR && colourBands == 3) {
        auto mCurve = cc.curves.master, rCurve = cc.curves.r, gCurve = cc.curves.g, bCurve = cc.curves.b;
        const auto byX = [](const Models::CurvePoint& a, const Models::CurvePoint& b) { return a.x < b.x; };
        std::sort(mCurve.begin(), mCurve.end(), byX);
        std::sort(rCurve.begin(), rCurve.end(), byX);
        std::sort(gCurve.begin(), gCurve.end(), byX);
        std::sort(bCurve.begin(), bCurve.end(), byX);

        std::vector<std::uint8_t> lut(256 * 3);
        for (int i = 0; i < 256; ++i) {
            const double m = evalCurve(mCurve, i / 255.0);
            lut[static_cast<std::size_t>(i) * 3 + 0] = toByte(evalCurve(rCurve, m));
            lut[static_cast<std::size_t>(i) * 3 + 1] = toByte(evalCurve(gCurve, m));
            lut[static_cast<std::size_t>(i) * 3 + 2] = toByte(evalCurve(bCurve, m));
        }

        VipsImage* lutImg =
            vips_image_new_from_memory_copy(lut.data(), lut.size(), 256, 1, 3, VIPS_FORMAT_UCHAR);
        if (!lutImg)
            failVips(colour, alpha, "build curve LUT");
        VipsImage* mapped = nullptr;
        const int rc = vips_maplut(colour, &mapped, lutImg, nullptr);
        g_object_unref(lutImg);
        if (rc != 0)
            failVips(colour, alpha, "apply curves (vips_maplut)");
        g_object_unref(colour);
        colour = mapped;
    }

    // Brightness / contrast, per colour band: out = in*contrast + (mid*(1 - contrast) + brightness*max).
    if (!neutralLevels) {
        const double c   = cc.contrast;
        const double off = (maxv * 0.5) * (1.0 - c) + cc.brightness * maxv;
        std::vector<double> a(static_cast<std::size_t>(colourBands), c);
        std::vector<double> b(static_cast<std::size_t>(colourBands), off);
        VipsImage* lin = nullptr;
        if (vips_linear(colour, &lin, a.data(), b.data(), colourBands, nullptr) != 0)
            failVips(colour, alpha, "brightness/contrast (vips_linear)");
        g_object_unref(colour);
        colour = lin;
    }

    // Saturation — luminance-preserving (Rec.709) 3x3 recomb; only meaningful for 3-band colour.
    if (!neutralSat && colourBands == 3) {
        const double s  = cc.saturation;
        const double wr = 0.2126, wg = 0.7152, wb = 0.0722;
        VipsImage* m = vips_image_new_matrixv(3, 3,
            wr * (1 - s) + s, wg * (1 - s),     wb * (1 - s),
            wr * (1 - s),     wg * (1 - s) + s, wb * (1 - s),
            wr * (1 - s),     wg * (1 - s),     wb * (1 - s) + s);
        if (!m)
            failVips(colour, alpha, "build saturation matrix");
        VipsImage* rec = nullptr;
        const int rc = vips_recomb(colour, &rec, m, nullptr);
        g_object_unref(m);
        if (rc != 0)
            failVips(colour, alpha, "saturation (vips_recomb)");
        g_object_unref(colour);
        colour = rec;
    }

    // Back to the source band format — vips_linear/recomb promote to float; the cast clips to the
    // format's range so downstream sees the same uchar (etc.) an ungraded page would produce.
    {
        VipsImage* casted = nullptr;
        if (vips_cast(colour, &casted, fmt, nullptr) != 0)
            failVips(colour, alpha, "cast back to source format");
        g_object_unref(colour);
        colour = casted;
    }

    // Rejoin alpha if it was split off.
    VipsImage* result = colour;
    if (hasAlpha) {
        VipsImage* joined = nullptr;
        const int rc = vips_bandjoin2(colour, alpha, &joined, nullptr);
        if (rc != 0)
            failVips(colour, alpha, "rejoin alpha (vips_bandjoin2)");
        g_object_unref(colour);
        g_object_unref(alpha);
        result = joined;
    }

    PLATEMAKER_LOG(Log::ColourCorrector,
            "graded " + std::to_string(bands) + "-band image (b=" + std::to_string(cc.brightness)
            + " c=" + std::to_string(cc.contrast) + " s=" + std::to_string(cc.saturation) + ")");

    return PixelBuffer{result};
}

void ColourCorrector::applyToRgba(unsigned char* rgba, int width, int height,
                                  const Models::ColourCorrection& cc) const
{
    if (!rgba || width <= 0 || height <= 0)
        throw std::runtime_error("ColourCorrector::applyToRgba — invalid buffer or dimensions");

    // The lib does not own the vips lifecycle, and a GUI consumer cannot call VIPS_INIT itself (it has no
    // vips headers) — it may reach here (a live grade preview) before any render has initialised vips.
    // VIPS_INIT only does anything on its first successful call, so ensure it before touching vips.
    if (VIPS_INIT("platemaker"))
        vips_error_clear(); // a genuine init failure surfaces as a throw from the vips calls below

    // Neutral grade → leave the bytes untouched (matches apply()'s no-op fast path).
    if (cc.brightness == 0.0 && cc.contrast == 1.0 && cc.saturation == 1.0
        && !Models::hasAnyCurve(cc.curves))
        return;

    const std::size_t nbytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;

    // Wrap the caller's RGBA in a vips image — a *copy*, so the caller's buffer lifetime never matters —
    // then tag it sRGB so apply() classifies the 4th band as alpha and grades only the colour bands.
    VipsImage* mem = vips_image_new_from_memory_copy(rgba, nbytes, width, height, 4, VIPS_FORMAT_UCHAR);
    if (!mem) {
        const std::string err = vips_error_buffer();
        vips_error_clear();
        throw std::runtime_error("ColourCorrector::applyToRgba — wrap RGBA buffer: " + err);
    }
    VipsImage* srgb = nullptr;
    if (vips_copy(mem, &srgb, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr) != 0) {
        const std::string err = vips_error_buffer();
        vips_error_clear();
        g_object_unref(mem);
        throw std::runtime_error("ColourCorrector::applyToRgba — tag sRGB: " + err);
    }
    g_object_unref(mem);

    // Grade through the shared engine (takes ownership of srgb), then read the result back into rgba.
    PixelBuffer graded = apply(PixelBuffer{srgb}, cc);
    VipsImage*  out    = graded.get();
    if (!out || out->Xsize != width || out->Ysize != height
        || out->Bands != 4 || out->BandFmt != VIPS_FORMAT_UCHAR)
        throw std::runtime_error("ColourCorrector::applyToRgba — unexpected graded image layout");

    std::size_t outSize = 0;
    void* outData = vips_image_write_to_memory(out, &outSize);
    if (!outData) {
        const std::string err = vips_error_buffer();
        vips_error_clear();
        throw std::runtime_error("ColourCorrector::applyToRgba — read graded pixels: " + err);
    }
    std::memcpy(rgba, outData, std::min(outSize, nbytes));
    g_free(outData);
}

} // namespace Platemaker::Core
