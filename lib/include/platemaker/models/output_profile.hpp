/**
 * \file lib/include/platemaker/models/output_profile.hpp
 * \brief OutputProfile and JpegOptions data models — describe how processed output is generated.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_MODELS_OUTPUT_PROFILE_HPP
#define PLATEMAKER_MODELS_OUTPUT_PROFILE_HPP

#include <string>
#include <platemaker/models/common_types.hpp>

namespace Platemaker::Models {

/**
 * \brief JPEG encoding parameters used when OutputProfile::outputFormat is OutputFormat::JPEG.
 *
 * All fields carry sensible defaults that produce high-quality, well-optimised
 * JPEG files suitable for online publication.
 */
struct JpegOptions {
    int             quality     = 90;                     //!< Encoding quality, 1–95 (95 = best quality, largest file).
    JpegSubsampling subsampling = JpegSubsampling::YUV_444; //!< Chroma subsampling mode.
    bool            optimize    = true;                   //!< Enable Huffman table optimisation (slightly slower, smaller files).
    bool            progressive = false;                  //!< Write progressive (multi-scan) JPEG instead of baseline.
};

/**
 * \brief PNG encoding parameters used when OutputProfile::outputFormat is OutputFormat::PNG.
 */
struct PngOptions {
    int  compression = 6;     //!< zlib compression level, 0–9 (higher = smaller, slower).
    bool interlaced  = false; //!< Write an Adam7-interlaced (progressive) PNG.
};

/**
 * \brief WebP encoding parameters used when OutputProfile::outputFormat is OutputFormat::WebP.
 */
struct WebpOptions {
    int  quality  = 80;    //!< Encoding quality, 0–100 (ignored when \c lossless).
    bool lossless = false; //!< Encode losslessly instead of lossy.
    int  effort   = 4;     //!< Compression effort, 0–6 (higher = smaller, slower).
};

/**
 * \class OutputProfile
 * \brief A named output configuration that controls how the virtual strip is
 *        scaled, sliced, and saved.
 *
 * Multiple profiles can coexist inside a workspace (e.g. "Webtoon Standard",
 * "Webtoon HD", "Instagram Square").  Per-project assignment is done via
 * \c ProjectItem::outputProfileId.
 */
class OutputProfile {
public:
    std::string id;   //!< Stable unique identifier — never changes after creation.

    /**
     * \brief Human-readable profile name shown in the GUI and used for workspace lookup.
     *
     * Example: "Webtoon Standard".
     */
    std::string name;

    int              targetWidth     = 800;                       //!< Target width in pixels after scaling each source image (Webtoon default: 800).
    int              sliceHeight     = 1280;                      //!< Output slice height in pixels (Webtoon default: 1280).
    LastSlicePolicy  lastSlicePolicy = LastSlicePolicy::KeepAsIs; //!< How the final (potentially shorter) tail slice is handled.
    OutputFormat     outputFormat    = OutputFormat::PNG;         //!< Container format for all output slice files.
    JpegOptions      jpegOptions;                                 //!< JPEG-specific encoding parameters (used when outputFormat == JPEG).
    PngOptions       pngOptions;                                  //!< PNG-specific encoding parameters (used when outputFormat == PNG).
    WebpOptions      webpOptions;                                 //!< WebP-specific encoding parameters (used when outputFormat == WebP).
    int              startIndex      = 1;                         //!< First output file number, e.g. 1 → output_001.png, 5 → output_005.png.
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_OUTPUT_PROFILE_HPP
