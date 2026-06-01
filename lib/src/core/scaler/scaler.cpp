/**
 * \file
 * \brief Scaler implementation — Lanczos3 scaling via vips_thumbnail().
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/core/scaler/scaler.hpp>

#include <vips/vips.h>

#include <stdexcept>
#include <string>

namespace Platemaker::Core {

ScaledImage Scaler::scale(const std::string& filePath, int targetWidth) const
{
    if (targetWidth <= 0) {
        throw std::runtime_error(
            "Scaler::scale() — targetWidth must be > 0 (got " +
            std::to_string(targetWidth) + ")");
    }

    VipsImage* out = nullptr;

    // vips_thumbnail() performs shrink-on-load where the file format allows it
    // (e.g. JPEG DCT down-sampling, pyramid TIFF), then applies Lanczos3 for
    // the residual resampling step.  This is the most RAM-efficient and
    // highest-quality path for large-to-small scaling.
    //
    // VIPS_SIZE_BOTH: allow both up- and down-scaling so the function behaves
    // correctly if a source image is narrower than targetWidth.
    //
    // no_rotate=TRUE: disable EXIF auto-rotation so the artist's intended
    // canvas orientation is preserved (rotation was already baked in by the
    // export from Procreate).
    if (vips_thumbnail(filePath.c_str(), &out, targetWidth,
            "kernel",    VIPS_KERNEL_LANCZOS3,
            "no_rotate", TRUE,
            "size",      VIPS_SIZE_BOTH,
            nullptr) != 0)
    {
        throw std::runtime_error(
            "Scaler::scale() — vips_thumbnail failed for '" + filePath +
            "': " + vips_error_buffer());
    }

    return ScaledImage{PixelBuffer{out}, filePath};
}

} // namespace Platemaker::Core
