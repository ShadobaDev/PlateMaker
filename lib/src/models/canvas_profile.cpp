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

#include <cctype>
#include <iterator>

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

const CanvasTemplateInfo* CanvasProfile::templateIn(TemplateFormat format) const noexcept
{
    for (const auto& t : templates)
        if (t.format == format) return &t;
    return nullptr;
}

namespace {

bool sameRgba(const RGBA& a, const RGBA& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool sameField(const CanvasProfile& a, const CanvasProfile& b, ProfileField f)
{
    switch (f) {
    case ProfileField::CanvasSize:
        return a.canvasSize.width == b.canvasSize.width && a.canvasSize.height == b.canvasSize.height;
    case ProfileField::Margins:
        return a.margins.top == b.margins.top && a.margins.right == b.margins.right
            && a.margins.bottom == b.margins.bottom && a.margins.left == b.margins.left;
    case ProfileField::Dpi:
        return a.dpi == b.dpi;
    case ProfileField::TemplateSettings:
        return a.templateSettings.format == b.templateSettings.format
            && a.templateSettings.emptyLayer == b.templateSettings.emptyLayer;
    case ProfileField::Colours:
        return sameRgba(a.visualColour, b.visualColour) && sameRgba(a.backgroundColour, b.backgroundColour);
    }
    return true;
}

constexpr ProfileField k_fields[] = {ProfileField::CanvasSize, ProfileField::Margins, ProfileField::Dpi,
                                     ProfileField::TemplateSettings, ProfileField::Colours};

bool sameNameNoCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

} // namespace

bool sameProfileContent(const CanvasProfile& a, const CanvasProfile& b)
{
    for (const ProfileField f : k_fields)
        if (!sameField(a, b, f)) return false;
    return true;
}

ProfileDistinction distinguishProfile(const std::vector<CanvasProfile>& palette, const CanvasProfile& cp)
{
    ProfileDistinction d;
    bool differs[std::size(k_fields)] = {};
    for (const auto& other : palette) {
        if ((!cp.id.empty() && other.id == cp.id) || !sameNameNoCase(other.name, cp.name))
            continue;
        if (sameProfileContent(other, cp)) {
            d.duplicate = true;
            continue;
        }
        for (std::size_t i = 0; i < std::size(k_fields); ++i)
            if (!sameField(other, cp, k_fields[i])) differs[i] = true;
    }
    for (std::size_t i = 0; i < std::size(k_fields); ++i)
        if (differs[i]) d.differs.push_back(k_fields[i]);
    return d;
}

bool canvasSizeMatches(const CanvasProfile& cp, int w, int h) noexcept
{
    return cp.canvasSize.width == w && cp.canvasSize.height == h;
}

} // namespace Platemaker::Models
