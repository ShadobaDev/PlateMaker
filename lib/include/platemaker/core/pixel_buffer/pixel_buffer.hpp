/**
 * \file lib/include/platemaker/core/pixel_buffer/pixel_buffer.hpp
 * \brief PixelBuffer — RAII wrapper around a libvips VipsImage pointer.
 *
 * PixelBuffer is the fundamental pixel-data container used throughout libplatemaker.
 * It owns the VipsImage* lifetime (via g_object_ref/unref) and provides value
 * semantics through move operations.  Copy construction is disabled — use
 * explicit clone() if a deep copy is required.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_CORE_PIXEL_BUFFER_HPP
#define PLATEMAKER_CORE_PIXEL_BUFFER_HPP

#include "platemaker/platemaker_export.h"

// Forward-declare VipsImage to avoid pulling in all of <vips/vips.h> in every
// translation unit that uses PixelBuffer.  The actual VipsImage operations are
// confined to pixel_buffer.cpp and other implementation files.
struct _VipsImage;
typedef struct _VipsImage VipsImage; //!< Opaque libvips image handle.

/**
 * \namespace Platemaker::Core
 * \brief Core image-processing components of libplatemaker.
 *
 * The Core namespace contains the main processing pipeline: PixelBuffer, Scaler,
 * ScaledStrip, MarginCropper, and TemplateGenerator.  All components in
 * this namespace are pure C++ and have zero dependency on Qt.
 */
namespace Platemaker::Core {

/**
 * \class PixelBuffer
 * \brief RAII owner of a libvips VipsImage*.
 *
 * All pixel data flowing through the libplatemaker pipeline is carried in a
 * PixelBuffer.  The class manages the VipsImage reference count so that callers
 * never need to call \c g_object_ref / \c g_object_unref directly.
 *
 * PixelBuffer is **move-only** — copying is disabled to prevent accidental
 * duplication of large in-memory images.  When you genuinely need a copy, call
 * \c clone() which performs an explicit, documented deep copy.
 *
 * \note An empty (default-constructed) PixelBuffer represents the absence of an
 *       image.  Always test with \c isValid() before accessing pixel data.
 */
class PLATEMAKER_EXPORT PixelBuffer {
public:
    // ---------------------------------------------------------------------------
    // Construction and destruction
    // ---------------------------------------------------------------------------

    /**
     * \brief Default constructor — creates an empty, invalid PixelBuffer.
     */
    PixelBuffer() noexcept;

    /**
     * \brief Takes ownership of an existing VipsImage pointer.
     *
     * The PixelBuffer assumes ownership of \p image.  The caller must not
     * call \c g_object_unref on \p image after this call.
     *
     * \param image A valid, non-null VipsImage pointer to take ownership of.
     *              Passing \c nullptr is allowed and produces an invalid PixelBuffer.
     */
    explicit PixelBuffer(VipsImage* image) noexcept;

    ~PixelBuffer();

    // Copying is disabled — use clone() for an explicit deep copy.
    PixelBuffer(const PixelBuffer&)            = delete;
    PixelBuffer& operator=(const PixelBuffer&) = delete;

    /**
     * \brief Move constructor — transfers ownership from \p other.
     *
     * \p other is left in a valid but empty state after the move.
     */
    PixelBuffer(PixelBuffer&& other) noexcept;

    /**
     * \brief Move assignment operator — transfers ownership from \p other.
     *
     * Any previously owned image is released.  \p other is left empty.
     */
    PixelBuffer& operator=(PixelBuffer&& other) noexcept;

    // ---------------------------------------------------------------------------
    // Accessors
    // ---------------------------------------------------------------------------

    /**
     * \brief Returns the underlying VipsImage* without transferring ownership.
     *
     * \return A non-owning pointer to the VipsImage, or \c nullptr if empty.
     */
    [[nodiscard]] VipsImage* vipsImage() const noexcept;

    /**
     * \brief Returns \c true if this PixelBuffer holds a valid image.
     */
    [[nodiscard]] bool isValid() const noexcept;

    /**
     * \brief Image width in pixels.
     *
     * \return Width, or 0 if the buffer is empty.
     */
    [[nodiscard]] int width() const noexcept;

    /**
     * \brief Image height in pixels.
     *
     * \return Height, or 0 if the buffer is empty.
     */
    [[nodiscard]] int height() const noexcept;

    // ---------------------------------------------------------------------------
    // Operations
    // ---------------------------------------------------------------------------

    /**
     * \brief Releases the owned image and returns the raw pointer to the caller.
     *
     * After this call the PixelBuffer is left empty.  The caller takes full
     * ownership of the returned pointer and is responsible for calling
     * \c g_object_unref on it.
     *
     * \return The previously owned VipsImage*, or \c nullptr if already empty.
     */
    [[nodiscard]] VipsImage* release() noexcept;

    /**
     * \brief Creates a deep copy of this PixelBuffer.
     *
     * An explicit operation to make duplication of pixel data visible at the
     * call site.  Internally calls \c vips_copy().
     *
     * \return A new PixelBuffer containing a copy of the current image data.
     * \retval PixelBuffer{} (empty) if the current buffer is empty.
     */
    [[nodiscard]] PixelBuffer clone() const;

private:
    VipsImage* m_image = nullptr; //!< Owned VipsImage pointer, or nullptr when empty.
};

} // namespace Platemaker::Core

#endif // PLATEMAKER_CORE_PIXEL_BUFFER_HPP
