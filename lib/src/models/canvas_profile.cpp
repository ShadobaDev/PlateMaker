/**
 * \file lib/src/models/canvas_profile.cpp
 * \brief CanvasProfile implementation — computed properties.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/models/canvas_profile.hpp>

namespace Platemaker::Models {

Size CanvasProfile::safeArea() const noexcept
{
    return Size{
        canvasSize.width  - margins.left - margins.right,
        canvasSize.height - margins.top  - margins.bottom
    };
}

std::string canvasRenderFingerprint(const CanvasProfile& cp)
{
    // Field tags guard against ambiguous concatenations (e.g. 1,60 vs 16,0).
    // Only fields that reach the render: canvasSize decides which pages this profile
    // matches, margins decide what MarginCropper removes. visualColour /
    // backgroundColour are template-only and must stay out — see the header.
    std::string s;
    s.reserve(48);
    s += "cw=" + std::to_string(cp.canvasSize.width);
    s += ";ch=" + std::to_string(cp.canvasSize.height);
    s += ";mt=" + std::to_string(cp.margins.top);
    s += ";mr=" + std::to_string(cp.margins.right);
    s += ";mb=" + std::to_string(cp.margins.bottom);
    s += ";ml=" + std::to_string(cp.margins.left);
    return s;
}

} // namespace Platemaker::Models
