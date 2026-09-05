/**
 * \file lib/src/core/pixel_buffer/pixel_buffer.cpp
 * \brief PixelBuffer implementation — RAII lifecycle for VipsImage*.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/core/pixel_buffer/pixel_buffer.hpp>

#include <vips/vips.h>

#include <stdexcept>
#include <string>

namespace Platemaker::Core {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PixelBuffer::PixelBuffer() noexcept
    : m_image(nullptr)
{}

PixelBuffer::PixelBuffer(VipsImage* image) noexcept
    : m_image(image)
{}

PixelBuffer::~PixelBuffer()
{
    if (m_image) {
        g_object_unref(m_image);
        m_image = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

PixelBuffer::PixelBuffer(PixelBuffer&& other) noexcept
    : m_image(other.m_image)
{
    other.m_image = nullptr;
}

PixelBuffer& PixelBuffer::operator=(PixelBuffer&& other) noexcept
{
    if (this != &other) {
        if (m_image) g_object_unref(m_image);
        m_image       = other.m_image;
        other.m_image = nullptr;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

VipsImage* PixelBuffer::vipsImage() const noexcept { return m_image; }

bool PixelBuffer::isValid() const noexcept { return m_image != nullptr; }

int PixelBuffer::width() const noexcept
{
    return m_image ? vips_image_get_width(m_image) : 0;
}

int PixelBuffer::height() const noexcept
{
    return m_image ? vips_image_get_height(m_image) : 0;
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

VipsImage* PixelBuffer::release() noexcept
{
    VipsImage* img = m_image;
    m_image        = nullptr;
    return img;
}

PixelBuffer PixelBuffer::clone() const
{
    if (!m_image) return PixelBuffer{};

    VipsImage* out = nullptr;
    if (vips_copy(m_image, &out, nullptr) != 0) {
        throw std::runtime_error(
            "PixelBuffer::clone() — vips_copy failed: " +
            std::string(vips_error_buffer()));
    }
    return PixelBuffer{out};
}

} // namespace Platemaker::Core
