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
    // A neutral grade touches no pixels — enabling the step with default scalars is a true no-op.
    // (ICC → sRGB is handled at load, not here; see the class doc.)
    const bool neutralLevels = (cc.brightness == 0.0 && cc.contrast == 1.0);
    const bool neutralSat    = (cc.saturation == 1.0);
    if (neutralLevels && neutralSat)
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

} // namespace Platemaker::Core
