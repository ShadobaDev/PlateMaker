/**
 * \file lib/src/core/margin_cropper/margin_cropper.cpp
 * \brief MarginCropper implementation — pure-crop via vips_extract_area().
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
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

    // Safe-area rectangle derived from the margin offsets.
    const int x = margins.left;
    const int y = margins.top;
    const int w = srcW - margins.left - margins.right;
    const int h = srcH - margins.top  - margins.bottom;

    if (w <= 0 || h <= 0) {
        throw std::invalid_argument(
            "MarginCropper::crop() — margins produce a zero or negative safe area "
            "(source: " + std::to_string(srcW) + "x" + std::to_string(srcH) +
            ", margins: top="    + std::to_string(margins.top) +
            " right="  + std::to_string(margins.right) +
            " bottom=" + std::to_string(margins.bottom) +
            " left="   + std::to_string(margins.left) +
            ", result: " + std::to_string(w) + "x" + std::to_string(h) + ")");
    }

    VipsImage* out = nullptr;
    if (vips_extract_area(source.vipsImage(), &out, x, y, w, h, nullptr) != 0) {
        throw std::runtime_error(
            "MarginCropper::crop() — vips_extract_area failed: " +
            std::string(vips_error_buffer()));
    }

    return PixelBuffer{out};
}

} // namespace Platemaker::Core
