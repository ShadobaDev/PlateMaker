/**
 * \file
 * \brief Common primitive types, enumerations and simple structs shared across the entire library.
 *
 * This header is included by virtually every other component in libplatemaker.
 * Keep it minimal: only truly shared, dependency-free types belong here.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#pragma once

#ifndef PLATEMAKER_MODELS_COMMON_TYPES_HPP
#define PLATEMAKER_MODELS_COMMON_TYPES_HPP

#include <cstdint>

/**
 * \namespace Platemaker::Models
 * \brief Data model types shared across Core, Infrastructure, CLI and GUI layers.
 *
 * All plain data structures (POD-like structs, enumerations, and simple value types)
 * that cross component boundaries live in this namespace.  They carry no behaviour —
 * only data and, where appropriate, simple computed properties.
 */
namespace Platemaker::Models {

// ---------------------------------------------------------------------------
// Primitive geometry types
// ---------------------------------------------------------------------------

/**
 * \brief Two-dimensional integer size (width × height).
 */
struct Size {
    int width  = 0; //!< Width in pixels.
    int height = 0; //!< Height in pixels.
};

/**
 * \brief Margin offsets on all four sides of a canvas, in pixels.
 */
struct Margins {
    int top    = 0; //!< Top margin in pixels.
    int right  = 0; //!< Right margin in pixels.
    int bottom = 0; //!< Bottom margin in pixels.
    int left   = 0; //!< Left margin in pixels.
};

/**
 * \brief 32-bit RGBA colour value.
 */
struct RGBA {
    std::uint8_t r = 0; //!< Red channel (0–255).
    std::uint8_t g = 0; //!< Green channel (0–255).
    std::uint8_t b = 0; //!< Blue channel (0–255).
    std::uint8_t a = 255; //!< Alpha channel (0 = fully transparent, 255 = fully opaque).
};

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/**
 * \enum LastSlicePolicy
 * \brief Defines how the tail slice (shorter than sliceHeight) is handled.
 *
 * The tail is the leftover strip segment after all full-height slices have been
 * extracted.  It is only absent when totalHeight is an exact multiple of sliceHeight.
 */
enum class LastSlicePolicy {
    Crop,      //!< Discard the tail slice entirely.
    PadWhite,  //!< Extend the tail to sliceHeight by filling with white pixels.
    KeepAsIs   //!< Save the tail at its natural (shorter) height.
};

/**
 * \enum OutputFormat
 * \brief Image format used for output slice files.
 */
enum class OutputFormat {
    PNG,  //!< Lossless PNG (default).
    JPEG, //!< Lossy JPEG — quality and subsampling controlled via JpegOptions.
    WebP  //!< WebP (lossy or lossless depending on quality setting).
};

/**
 * \enum PageStatus
 * \brief Processing state of a single PageItem in the workspace.
 */
enum class PageStatus {
    Pending,   //!< Not yet processed in the current session.
    Processed, //!< Successfully processed and output written.
    Skipped,   //!< Skipped due to incremental-processing logic (hash unchanged).
    Error      //!< Failed to load or process; see PageItem::errorMessage for details.
};

/**
 * \enum JpegSubsampling
 * \brief Chroma subsampling mode for JPEG output.
 *
 * Higher subsampling (4:2:0) reduces file size at the cost of colour fidelity.
 * 4:4:4 preserves full colour detail.
 */
enum class JpegSubsampling {
    YUV_444, //!< No chroma subsampling — best quality, largest file.
    YUV_422, //!< Horizontal 2× chroma subsampling.
    YUV_420  //!< Horizontal and vertical 2× chroma subsampling — smallest file.
};

} // namespace Platemaker::Models

#endif // PLATEMAKER_MODELS_COMMON_TYPES_HPP
