/**
 * \file lib/src/infrastructure/image_io/image_io.cpp
 * \brief ImageIO implementation — libvips-backed load and save with format dispatch.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/image_io/image_io.hpp>

#include <vips/vips.h>

#include <stdexcept>
#include <string>

namespace Platemaker::Infrastructure {

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

Core::PixelBuffer ImageIO::load(const std::string& filePath) const
{
    // VIPS_ACCESS_RANDOM allows any downstream operation (e.g. crop, extract)
    // to access arbitrary rows without a second file read.  For the standard
    // pipeline Scaler::scale() loads via vips_thumbnail() directly; ImageIO::load
    // is used for the margin-aware pipeline where MarginCropper needs random access.
    VipsImage* image = vips_image_new_from_file(filePath.c_str(),
        "access", VIPS_ACCESS_RANDOM,
        nullptr);
    if (!image) {
        throw std::runtime_error(
            "ImageIO::load() — cannot load '" + filePath + "': " +
            vips_error_buffer());
    }

    // Attempt to normalise the colour profile to sRGB using any embedded ICC
    // profile.  If none is present or the transform fails, continue with the
    // original image (most Procreate exports are already sRGB).
    VipsImage* srgb = nullptr;
    if (vips_icc_transform(image, &srgb, "srgb",
            "embedded", TRUE,
            nullptr) == 0)
    {
        g_object_unref(image);
        return Core::PixelBuffer{srgb};
    }

    // icc_transform failure is non-fatal — clear the error and keep original.
    vips_error_clear();
    return Core::PixelBuffer{image};
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

void ImageIO::save(
    const Core::PixelBuffer&     buffer,
    const std::string&           outputPath,
    const Models::OutputProfile& profile) const
{
    if (!buffer.isValid()) {
        throw std::runtime_error("ImageIO::save() — pixel buffer is empty");
    }

    int result = -1;

    switch (profile.outputFormat) {

        // --- PNG (lossless) ---
        case Models::OutputFormat::PNG:
            result = vips_pngsave(buffer.get(), outputPath.c_str(),
                "compression", profile.pngOptions.compression,
                "interlace",   profile.pngOptions.interlaced ? 1 : 0,
                nullptr);
            break;

        // --- JPEG ---
        case Models::OutputFormat::JPEG: {
            // Map our JpegSubsampling enum to libvips VipsForeignSubsample integers:
            //   VIPS_FOREIGN_SUBSAMPLE_AUTO (0) — libjpeg decides (best effort 4:2:2)
            //   VIPS_FOREIGN_SUBSAMPLE_ON   (1) — always subsample (4:2:0)
            //   VIPS_FOREIGN_SUBSAMPLE_OFF  (2) — no subsampling (4:4:4)
            int subsampleMode = 0;
            switch (profile.jpegOptions.subsampling) {
                case Models::JpegSubsampling::YUV_444: subsampleMode = 2; break;
                case Models::JpegSubsampling::YUV_422: subsampleMode = 0; break; // best-effort
                case Models::JpegSubsampling::YUV_420: subsampleMode = 1; break;
            }

            result = vips_jpegsave(buffer.get(), outputPath.c_str(),
                "Q",               profile.jpegOptions.quality,
                "optimize_coding", profile.jpegOptions.optimize   ? 1 : 0,
                "interlace",       profile.jpegOptions.progressive ? 1 : 0,
                "subsample_mode",  subsampleMode,
                nullptr);
            break;
        }

        // --- WebP ---
        case Models::OutputFormat::WebP:
            result = vips_webpsave(buffer.get(), outputPath.c_str(),
                "Q",        profile.webpOptions.quality,
                "lossless", profile.webpOptions.lossless ? 1 : 0,
                "effort",   profile.webpOptions.effort,
                nullptr);
            break;
    }

    if (result != 0) {
        throw std::runtime_error(
            "ImageIO::save() — failed to write '" + outputPath + "': " +
            vips_error_buffer());
    }
}

} // namespace Platemaker::Infrastructure
