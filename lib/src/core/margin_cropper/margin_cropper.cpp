/**
 * \file lib/src/core/margin_cropper/margin_cropper.cpp
 * \brief MarginCropper implementation — pure-crop via vips_extract_area().
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */

#include <platemaker/core/margin_cropper/margin_cropper.hpp>

#include <vips/vips.h>

#include <stdexcept>
#include <string>

namespace Platemaker::Core {

PixelBuffer MarginCropper::crop(
    const PixelBuffer&     source,
    const Models::Margins& margins) const
{
    if (!source.isValid()) {
        throw std::invalid_argument(
            "MarginCropper::crop() — source buffer is empty");
    }

    const int srcW = source.width();
    const int srcH = source.height();

    // Safe-area rectangle derived from the margin offsets, in 64 bits: two margins near INT_MAX would
    // overflow an int and could come back positive.
    const int x = margins.left;
    const int y = margins.top;
    const long long w = static_cast<long long>(srcW) - margins.left - margins.right;
    const long long h = static_cast<long long>(srcH) - margins.top  - margins.bottom;

    // A negative margin would start the area outside the image, which libvips refuses with a message
    // about extract_area rather than about margins.
    const bool negative = margins.top < 0 || margins.right < 0 || margins.bottom < 0 || margins.left < 0;
    if (negative || w <= 0 || h <= 0) {
        throw std::invalid_argument(
            "MarginCropper::crop() — margins must be non-negative and leave a safe area "
            "(source: " + std::to_string(srcW) + "x" + std::to_string(srcH) +
            ", margins: top="    + std::to_string(margins.top) +
            " right="  + std::to_string(margins.right) +
            " bottom=" + std::to_string(margins.bottom) +
            " left="   + std::to_string(margins.left) +
            ", result: " + std::to_string(w) + "x" + std::to_string(h) + ")");
    }

    VipsImage* out = nullptr;
    if (vips_extract_area(source.vipsImage(), &out, x, y, static_cast<int>(w), static_cast<int>(h), nullptr) != 0) {
        throw std::runtime_error(
            "MarginCropper::crop() — vips_extract_area failed: " +
            std::string(vips_error_buffer()));
    }

    return PixelBuffer{out};
}

} // namespace Platemaker::Core
