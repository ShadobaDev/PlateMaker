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
#include <platemaker/infrastructure/file/path_utf8.hpp>

#include <vips/vips.h>

#include <filesystem>
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

    // Normalise to display orientation first: MarginCropper crops in display coordinates and the scaler
    // builds the strip from these pixels, so both must see the upright image. vips_autorot rotates per the
    // EXIF Orientation and drops the tag; a no-op when Orientation is absent or 1 (see Scaler::scale).
    VipsImage* upright = nullptr;
    if (vips_autorot(image, &upright, nullptr) != 0) {
        g_object_unref(image);
        throw std::runtime_error(
            "ImageIO::load() — vips_autorot failed for '" + filePath + "': " +
            vips_error_buffer());
    }
    g_object_unref(image);
    image = upright;

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

    namespace fs = std::filesystem;

    // Encode to a temporary sibling first, then atomically rename it over the destination. Rationale:
    //
    //  1. A reader can hold the destination open the instant we go to overwrite it — the GUI
    //     regenerates each slice's thumbnail right after it is saved, and antivirus / Explorer's
    //     preview / an open image viewer do the same. On Windows that read handle makes an in-place
    //     open-for-write fail hard ("unable to open for write, Invalid argument"), aborting the whole
    //     render. Encoding into an unshared temp name never collides; only the quick rename touches
    //     the destination, and it is retried past a transient reader.
    //  2. The published file is therefore never a half-written slice — a viewer sees either the old
    //     file or the whole new one, never a truncated frame.
    //
    // The *save calls are format-explicit, so the ".pmtmp" extension does not affect the encoder.
    const std::string tmpPath = outputPath + ".pmtmp";

    int result = -1;

    switch (profile.outputFormat) {

        // --- PNG (lossless) ---
        case Models::OutputFormat::PNG:
            result = vips_pngsave(buffer.get(), tmpPath.c_str(),
                "compression", profile.pngOptions.compression,
                "interlace",   profile.pngOptions.interlaced ? 1 : 0,
                "keep",        VIPS_FOREIGN_KEEP_ICC, // rendered slice: drop source EXIF/XMP/IPTC, keep colour
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

            // JPEG cannot carry an alpha channel. The strip may be RGBA when any source had
            // transparency (ScaledStrip promotes to the widest band layout so mixed RGB/RGBA inputs
            // join cleanly). Flatten over white here — the one place dropping alpha is unavoidable,
            // dictated by the format, not our assembly. White matches the PadWhite/EXTEND_WHITE tail
            // convention; for the promoted, fully-opaque regions the composite is pixel-identical
            // regardless of background, so only genuinely-transparent pixels are affected.
            VipsImage* encodeSrc = buffer.get();
            VipsImage* flattened = nullptr; // owned only if we flatten
            if (vips_image_hasalpha(encodeSrc)) {
                double bg[3] = { 255.0, 255.0, 255.0 };
                VipsArrayDouble* white = vips_array_double_new(bg, 3);
                const int frc = vips_flatten(encodeSrc, &flattened, "background", white, nullptr);
                vips_area_unref(VIPS_AREA(white));
                if (frc != 0) {
                    const std::string err = vips_error_buffer();
                    std::error_code rmEc;
                    fs::remove(utf8ToPath(tmpPath), rmEc);
                    throw std::runtime_error(
                        "ImageIO::save() — failed to flatten alpha for JPEG '" + outputPath +
                        "': " + err);
                }
                encodeSrc = flattened;
            }

            result = vips_jpegsave(encodeSrc, tmpPath.c_str(),
                "Q",               profile.jpegOptions.quality,
                "optimize_coding", profile.jpegOptions.optimize   ? 1 : 0,
                "interlace",       profile.jpegOptions.progressive ? 1 : 0,
                "subsample_mode",  subsampleMode,
                "keep",            VIPS_FOREIGN_KEEP_ICC, // drop source EXIF (orientation/camera/thumbnail), keep colour
                nullptr);

            if (flattened) g_object_unref(flattened);
            break;
        }

        // --- WebP ---
        case Models::OutputFormat::WebP:
            result = vips_webpsave(buffer.get(), tmpPath.c_str(),
                "Q",        profile.webpOptions.quality,
                "lossless", profile.webpOptions.lossless ? 1 : 0,
                "effort",   profile.webpOptions.effort,
                "keep",     VIPS_FOREIGN_KEEP_ICC, // rendered slice: drop source EXIF/XMP/IPTC, keep colour
                nullptr);
            break;
    }

    if (result != 0) {
        const std::string err = vips_error_buffer();
        std::error_code rmEc;
        fs::remove(utf8ToPath(tmpPath), rmEc); // don't leave a partial temp behind
        throw std::runtime_error(
            "ImageIO::save() — failed to write '" + outputPath + "': " + err);
    }

    // Publish: rename the temp over the destination. std::filesystem::rename replaces an existing target
    // atomically. It fails while another process holds the destination (Explorer preview / antivirus /
    // an open viewer). The lib does **not** poll or retry — that policy belongs to the consumer — so a
    // locked destination surfaces immediately as a typed OutputLockedError, which the pipeline maps to
    // ProcessingErrorCode::OutputLocked.
    std::error_code ec;
    fs::rename(utf8ToPath(tmpPath), utf8ToPath(outputPath), ec);
    if (ec) {
        std::error_code rmEc;
        fs::remove(utf8ToPath(tmpPath), rmEc);
        throw OutputLockedError(
            "ImageIO::save() — could not replace '" + outputPath +
            "' (is it open in another program?): " + ec.message());
    }
}

} // namespace Platemaker::Infrastructure
