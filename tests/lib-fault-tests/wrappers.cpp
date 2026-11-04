/**
 * \file tests/lib-fault-tests/wrappers.cpp
 * \brief The functions a test can make fail — one line each, by kind (fault.hpp).
 *
 * A function the library imports (`nm -D --undefined-only libplatemaker.so`) gets a line here when a
 * test needs it to fail. Its parameter list is the one in the library's header.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "fault.hpp"

#include <archive.h>
#include <vips/vips.h>

#include <cstdarg>

// --- libvips operations ---------------------------------------------------------------------------
FAULT_VIPS_OPERATION(extract_area, (VipsImage* in, VipsImage** out, int left, int top, int width, int height, ...),
                     height, in, out, left, top, width, height)

// --- libvips functions that are not operations ----------------------------------------------------
FAULT_VIPS_IN_OUT(addalpha)

// --- libarchive -----------------------------------------------------------------------------------
FAULT_C_FUNCTION(struct archive*, archive_write_new, (void), (), nullptr)
