/**
 * \file lib/src/core/scaler/scaler.cpp
 * \brief Scaler implementation — Lanczos3-equivalent scaling via vips_resize().
 *
 * \note Design decision — why we use vips_image_new_from_file (RANDOM) + vips_resize
 *   instead of vips_thumbnail:
 *
 *   vips_thumbnail() opens files in SEQUENTIAL access mode for efficiency.
 *   This means the libvips pipeline can only read pixels top-to-bottom, once.
 *   When ScaledStrip::buildSlice() later calls vips_extract_area() on a scaled
 *   image, and then ImageIO::encode() calls vips_pngsave() on that extract, the
 *   PNG encoder uses an internal tiled write path that may read rows out of
 *   order — triggering "vipspng: out of order read" errors on the second and
 *   later slices.  Additionally, calling vips_thumbnail() three times in
 *   sequence (once per source file) while also calling vips_image_wio_input()
 *   to force evaluation causes subtle state corruption that silently skips
 *   files 2 and 3.
 *
 *   Loading with VIPS_ACCESS_RANDOM forces the decoder to cache the full image
 *   in memory once.  vips_resize() on a random-access source produces a PARTIAL
 *   (lazy) image that can be read in any tile order.  All downstream operations
 *   (vips_extract_area, vips_arrayjoin, vips_pngsave, vips_jpegsave, etc.) can
 *   therefore compute any tile without sequential-access restrictions.
 *
 *   Trade-off: no JPEG DCT shrink-on-load (vips_thumbnail advantage).
 *   For typical Procreate exports at 800 px wide this is inconsequential; the
 *   correctness guarantee is worth the minor overhead.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/core/scaler/scaler.hpp>

#include <platemaker/infrastructure/log/log.hpp>

#include <vips/vips.h>

#include <stdexcept>
#include <string>

namespace { namespace Log = Platemaker::Infrastructure::Log; }

namespace Platemaker::Core {

// ---------------------------------------------------------------------------
// File-path overload
// ---------------------------------------------------------------------------

ScaledImage Scaler::scale(const std::string& filePath, int targetWidth) const
{
    if (targetWidth <= 0) {
        throw std::runtime_error(
            "Scaler::scale() — targetWidth must be > 0 (got " +
            std::to_string(targetWidth) + ")");
    }

    // Load with VIPS_ACCESS_RANDOM so the whole image is decoded into a flat in-memory buffer, so every
    // downstream op (resize, extract, join, save) can read tiles in any order without the "out of order
    // read" failures that vips_thumbnail's SEQUENTIAL mode triggers (see the file header).
    VipsImage* loaded = vips_image_new_from_file(filePath.c_str(),
        "access", VIPS_ACCESS_RANDOM,
        nullptr);
    if (!loaded) {
        throw std::runtime_error(
            "Scaler::scale() — cannot load '" + filePath + "': " +
            vips_error_buffer());
    }

    if (loaded->Xsize == 0) {
        g_object_unref(loaded);
        throw std::runtime_error(
            "Scaler::scale() — source image '" + filePath + "' has zero width");
    }

    // Normalise to display orientation: rotate the pixels per the EXIF Orientation tag and drop the tag.
    // Camera JPEGs store landscape pixels plus an Orientation tag; without this a rotated photo is built
    // into the strip sideways / upside-down and the saved slice inherits the tag so viewers re-rotate it.
    // vips_autorot is idempotent when Orientation is absent or 1 (Procreate exports carry no tag), so this
    // is a no-op there and only changes genuinely rotated inputs. It preserves random access, so the
    // "out of order read" guarantee above still holds.
    VipsImage* upright = nullptr;
    if (vips_autorot(loaded, &upright, nullptr) != 0) {
        g_object_unref(loaded);
        throw std::runtime_error(
            "Scaler::scale() — vips_autorot failed for '" + filePath + "': " +
            vips_error_buffer());
    }
    g_object_unref(loaded); // autorot holds its own reference
    loaded = upright;       // continue with the display-correct image

    const int    srcW   = loaded->Xsize;
    const int    srcH   = loaded->Ysize;
    const double hscale = static_cast<double>(targetWidth) /
                          static_cast<double>(srcW);

    // vips_resize() scales by a factor, preserving aspect ratio (vscale defaults
    // to hscale when omitted).  Unlike vips_thumbnail, it does not reorder access
    // patterns and is safe to use on already-loaded random-access images.
    VipsImage* out = nullptr;
    if (vips_resize(loaded, &out, hscale, nullptr) != 0) {
        g_object_unref(loaded);
        throw std::runtime_error(
            "Scaler::scale() — vips_resize failed for '" + filePath +
            "': " + vips_error_buffer());
    }
    g_object_unref(loaded); // resize holds its own reference; release ours

    PLATEMAKER_LOG(Log::Scaler,
            "scale(file) " + filePath + ": " + std::to_string(srcW) + "x"
            + std::to_string(srcH) + " -> " + std::to_string(out->Xsize) + "x"
            + std::to_string(out->Ysize));

    return ScaledImage{PixelBuffer{out}, filePath};
}

// ---------------------------------------------------------------------------
// In-memory buffer overload (margin-aware pipeline)
// ---------------------------------------------------------------------------

ScaledImage Scaler::scale(PixelBuffer buffer, std::string sourceFilePath, int targetWidth) const
{
    if (targetWidth <= 0) {
        throw std::runtime_error(
            "Scaler::scale() — targetWidth must be > 0 (got " +
            std::to_string(targetWidth) + ")");
    }
    if (!buffer.isValid()) {
        throw std::runtime_error(
            "Scaler::scale() — source buffer is invalid (null VipsImage)");
    }

    // Use vips_resize() for buffer-based scaling: explicit scale factor,
    // predictable aspect-ratio semantics, no auto-orientation ambiguity.
    // The caller (ImageIO::decode + MarginCropper) already holds a
    // VIPS_ACCESS_RANDOM-backed buffer, so the resize result is random-access.
    if (buffer.width() == 0) {
        throw std::runtime_error(
            "Scaler::scale() — source buffer has zero width");
    }
    const int    srcW   = buffer.width();
    const int    srcH   = buffer.height();
    const double hscale = static_cast<double>(targetWidth) /
                          static_cast<double>(srcW);

    VipsImage* out = nullptr;
    if (vips_resize(buffer.vipsImage(), &out, hscale, nullptr) != 0) {
        throw std::runtime_error(
            "Scaler::scale() — vips_resize failed for '" + sourceFilePath +
            "': " + vips_error_buffer());
    }

    PLATEMAKER_LOG(Log::Scaler,
            "scale(buffer) " + sourceFilePath + ": " + std::to_string(srcW) + "x"
            + std::to_string(srcH) + " -> " + std::to_string(out->Xsize) + "x"
            + std::to_string(out->Ysize));

    return ScaledImage{PixelBuffer{out}, std::move(sourceFilePath)};
}

} // namespace Platemaker::Core
