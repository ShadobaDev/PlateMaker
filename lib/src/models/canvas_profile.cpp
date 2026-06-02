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

} // namespace Platemaker::Models
