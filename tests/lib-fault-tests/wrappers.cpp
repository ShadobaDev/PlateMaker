/**
 * \file tests/lib-fault-tests/wrappers.cpp
 * \brief The functions a test can make fail — one line each, by kind (fault.hpp).
 *
 * A function the library imports (`nm -D --undefined-only libplatemaker.so`) gets a line here when a
 * test needs it to fail. Its parameter list is the one in the library's header. The few whose `...`
 * carries more than options are written out, each as libvips itself implements it.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "fault.hpp"

#include <archive.h>
#include <vips/vips.h>

#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <vector>

// --- libvips operations ---------------------------------------------------------------------------
FAULT_VIPS_OPERATION(autorot, (VipsImage* in, VipsImage** out, ...), out, out, in, out)
FAULT_VIPS_OPERATION(cast, (VipsImage* in, VipsImage** out, VipsBandFormat format, ...), format, out, in, out, format)
FAULT_VIPS_OPERATION(colourspace, (VipsImage* in, VipsImage** out, VipsInterpretation space, ...), space, out,
                     in, out, space)
FAULT_VIPS_OPERATION(copy, (VipsImage* in, VipsImage** out, ...), out, out, in, out)
FAULT_VIPS_OPERATION(embed, (VipsImage* in, VipsImage** out, int x, int y, int width, int height, ...), height,
                     out, in, out, x, y, width, height)
FAULT_VIPS_OPERATION(extract_area, (VipsImage* in, VipsImage** out, int left, int top, int width, int height, ...),
                     height, out, in, out, left, top, width, height)
FAULT_VIPS_OPERATION(extract_band, (VipsImage* in, VipsImage** out, int band, ...), band, out, in, out, band)
FAULT_VIPS_OPERATION(icc_transform, (VipsImage* in, VipsImage** out, const char* output_profile, ...),
                     output_profile, out, in, out, output_profile)
FAULT_VIPS_OPERATION(maplut, (VipsImage* in, VipsImage** out, VipsImage* lut, ...), lut, out, in, out, lut)
FAULT_VIPS_OPERATION(resize, (VipsImage* in, VipsImage** out, double scale, ...), scale, out, in, out, scale)

// --- libvips functions that are not operations ----------------------------------------------------
FAULT_VIPS_IN_OUT(addalpha)

FAULT_C_FUNCTION(VipsImage*, vips_image_new_from_memory,
                 (const void* data, size_t size, int width, int height, int bands, VipsBandFormat format),
                 (data, size, width, height, bands, format), (Fault::vipsFailure("vips_image_new_from_memory"), nullptr))
FAULT_C_FUNCTION(VipsImage*, vips_image_new_from_memory_copy,
                 (const void* data, size_t size, int width, int height, int bands, VipsBandFormat format),
                 (data, size, width, height, bands, format),
                 (Fault::vipsFailure("vips_image_new_from_memory_copy"), nullptr))
FAULT_C_FUNCTION(VipsImage*, vips_image_new_memory, (void), (), (Fault::vipsFailure("vips_image_new_memory"), nullptr))
FAULT_C_FUNCTION(int, vips_image_write, (VipsImage* image, VipsImage* out), (image, out),
                 Fault::vipsFailure("vips_image_write"))
FAULT_C_FUNCTION(int, vips_image_write_prepare, (VipsImage* image), (image), Fault::vipsFailure("vips_image_write_prepare"))
FAULT_C_FUNCTION(void*, vips_image_write_to_memory, (VipsImage* in, size_t* size), (in, size),
                 (Fault::vipsFailure("vips_image_write_to_memory"), nullptr))
// The minipsd plugin's buffer save makes its target with this, inside the library.
FAULT_C_FUNCTION(VipsTarget*, vips_target_new_to_memory, (void), (),
                 (Fault::vipsFailure("vips_target_new_to_memory"), nullptr))

// The loader libvips would pick, called with the options given — vips_image_new_from_file() without its
// "name[options]" form, which the library does not use.
static const bool fault_declared_image_new_from_file = Fault::declare("vips_image_new_from_file");
extern "C" VipsImage* vips_image_new_from_file(const char* name, ...)
{
    VipsImage* out = nullptr;
    switch (Fault::shoot("vips_image_new_from_file", &out)) {
    case Fault::Shot::Fail:  Fault::vipsFailure("vips_image_new_from_file"); return nullptr;
    case Fault::Shot::Image: return out;
    case Fault::Shot::Pass:  break;
    }
    const char* loader = vips_foreign_find_load(name);
    if (!loader)
        return nullptr;
    va_list ap;
    va_start(ap, name);
    const int r = vips_call_split(loader, ap, name, &out);
    va_end(ap);
    return r == 0 ? out : nullptr;
}

// The SVG in a blob, as libvips wraps it, so the options pass through.
static const bool fault_declared_svgload_buffer = Fault::declare("vips_svgload_buffer");
extern "C" int vips_svgload_buffer(void* buf, size_t len, VipsImage** out, ...)
{
    switch (Fault::shoot("vips_svgload_buffer", out)) {
    case Fault::Shot::Fail:  return Fault::vipsFailure("vips_svgload_buffer");
    case Fault::Shot::Image: return 0;
    case Fault::Shot::Pass:  break;
    }
    VipsBlob* blob = vips_blob_new(nullptr, buf, len);
    va_list   ap;
    va_start(ap, out);
    const int r = vips_call_split("svgload_buffer", ap, blob, out);
    va_end(ap);
    vips_area_unref(VIPS_AREA(blob));
    return r;
}

// The ink as an array, as libvips passes it, so "fill" passes through.
static const bool fault_declared_draw_rect = Fault::declare("vips_draw_rect");
extern "C" int vips_draw_rect(VipsImage* image, double* ink, int n, int left, int top, int width, int height, ...)
{
    if (Fault::shoot("vips_draw_rect") == Fault::Shot::Fail)
        return Fault::vipsFailure("vips_draw_rect");
    VipsArrayDouble* array = vips_array_double_new(ink, n);
    va_list          ap;
    va_start(ap, height);
    const int r = vips_call_split("draw_rect", ap, image, array, left, top, width, height);
    va_end(ap);
    vips_area_unref(VIPS_AREA(array));
    return r;
}

// Two inputs: not the in/out shape. The library passes no options.
static const bool fault_declared_bandjoin2 = Fault::declare("vips_bandjoin2");
extern "C" int vips_bandjoin2(VipsImage* in1, VipsImage* in2, VipsImage** out, ...)
{
    switch (Fault::shoot("vips_bandjoin2", out)) {
    case Fault::Shot::Fail:  return Fault::vipsFailure("vips_bandjoin2");
    case Fault::Shot::Image: return 0;
    case Fault::Shot::Pass:  break;
    }
    using Real = int (*)(VipsImage*, VipsImage*, VipsImage**, ...);
    return reinterpret_cast<Real>(Fault::real("vips_bandjoin2"))(in1, in2, out, nullptr);
}

// The matrix's values are the `...`: read them, then build it from an array.
static const bool fault_declared_image_new_matrixv = Fault::declare("vips_image_new_matrixv");
extern "C" VipsImage* vips_image_new_matrixv(int width, int height, ...)
{
    if (Fault::shoot("vips_image_new_matrixv") == Fault::Shot::Fail) {
        Fault::vipsFailure("vips_image_new_matrixv");
        return nullptr;
    }
    std::vector<double> values(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    va_list             ap;
    va_start(ap, height);
    for (double& v : values)
        v = va_arg(ap, double);
    va_end(ap);
    return vips_image_new_matrix_from_array(width, height, values.data(), static_cast<int>(values.size()));
}

// No options, a buffer out: the real one, with none.
static const bool fault_declared_pngsave_buffer = Fault::declare("vips_pngsave_buffer");
extern "C" int vips_pngsave_buffer(VipsImage* in, void** buf, size_t* len, ...)
{
    if (Fault::shoot("vips_pngsave_buffer") == Fault::Shot::Fail)
        return Fault::vipsFailure("vips_pngsave_buffer");
    using Real = int (*)(VipsImage*, void**, size_t*, ...);
    return reinterpret_cast<Real>(Fault::real("vips_pngsave_buffer"))(in, buf, len, nullptr);
}

// --- libarchive -----------------------------------------------------------------------------------
FAULT_C_FUNCTION(struct archive*, archive_write_new, (void), (), nullptr)
FAULT_C_FUNCTION(int, archive_write_header, (struct archive* a, struct archive_entry* entry), (a, entry), ARCHIVE_FATAL)
FAULT_C_FUNCTION(la_ssize_t, archive_write_data, (struct archive* a, const void* buffer, size_t size), (a, buffer, size),
                 ARCHIVE_FATAL)

// --- libc: what std::ifstream and std::ofstream read and write with -------------------------------------
FAULT_C_FUNCTION(ssize_t, read, (int fd, void* buffer, size_t size), (fd, buffer, size), (errno = EIO, -1))
FAULT_C_FUNCTION(ssize_t, write, (int fd, const void* buffer, size_t size), (fd, buffer, size), (errno = ENOSPC, -1))
