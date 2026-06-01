/**
 * \file
 * \brief ThumbnailCache implementation.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/thumbnail_cache/thumbnail_cache.hpp>

#include <vips/vips.h>

#ifndef PLATEMAKER_NO_QT
#   include <QtConcurrent/QtConcurrent>
#endif

namespace Platemaker::Infrastructure {

// TODO: Stage 1 implementation — SHA-256 path hashing, vips_thumbnail() for 200 px
//       preview generation, optional QtConcurrent::run dispatch.

} // namespace Platemaker::Infrastructure
