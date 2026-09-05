/**
 * \file lib/include/platemaker/infrastructure/image_io/output_locked_error.hpp
 * \brief OutputLockedError - the one failure a caller is expected to catch by type.
 *
 * Split out of \c image_io.hpp: an exception type and an I/O service are different kinds of thing,
 * and this one is caught in places that have no interest in \c ImageIO itself.  It stays in the
 * image_io directory because that is the class it belongs to (see \c docs/CODING_STYLE.md).
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_OUTPUT_LOCKED_ERROR_HPP
#define PLATEMAKER_INFRASTRUCTURE_OUTPUT_LOCKED_ERROR_HPP

#include <stdexcept>

#include "platemaker/platemaker_export.h"

namespace Platemaker::Infrastructure {

// Cross-module visibility for a thrown exception type. The two toolchains match exceptions across a
// shared-library boundary differently, so the correct annotation differs:
//   * MSVC matches by the type's decorated *name*, which every module that includes this header already
//     shares — so no __declspec(dllexport) is needed. Exporting one would only trigger C4275 (a
//     non-dll-interface std base) and LNK4197 (the vtable exported from every TU), for no benefit.
//   * GCC/Clang build the lib with hidden visibility, where the type_info must have *default* visibility
//     for a consumer in another shared object to catch it by type — so it keeps PLATEMAKER_EXPORT.
#if defined(_MSC_VER)
#  define PLATEMAKER_EXCEPTION_EXPORT
#else
#  define PLATEMAKER_EXCEPTION_EXPORT PLATEMAKER_EXPORT
#endif

/**
 * \brief Thrown by \c ImageIO::encode() when the destination cannot be published because another process
 *        holds it open (Explorer's preview, antivirus, an image viewer).
 *
 * Distinct from a generic write failure so a caller (the pipeline) can report it as the typed
 * \c Models::ProcessingErrorCode::OutputLocked and leave any retry policy to the consumer — the lib
 * itself never polls. The processing pipeline catches it internally, so higher-level consumers see the
 * ProcessingErrorCode rather than this type; a direct \c ImageIO::encode() caller can still catch it by
 * type across the library boundary (the unit tests do) — see \c PLATEMAKER_EXCEPTION_EXPORT above.
 */
class PLATEMAKER_EXCEPTION_EXPORT OutputLockedError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_OUTPUT_LOCKED_ERROR_HPP
