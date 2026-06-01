/**
 * \file
 * \brief OutputProfile and JpegOptions data models — describe how processed output is generated.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#pragma once

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
 * \class OutputProfile
 * \brief A named output configuration that controls how the virtual strip is
 *        scaled, sliced, and saved.
 *
 * Multiple profiles can coexist inside a workspace (e.g. "Webtoon Standard",
 * "Webtoon HD", "Instagram Square").  The active profile is referenced by name
 * via \c Workspace::activeOutputProfileName.
 */
class OutputProfile {
public:
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
    JpegOptions      jpegOptions;                                 //!< JPEG-specific encoding parameters (ignored for PNG/WebP output).
    int              startIndex      = 1;                         //!< First output file number, e.g. 1 → output_001.png, 5 → output_005.png.
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_OUTPUT_PROFILE_HPP
