/**
 * \file lib/src/infrastructure/file/file_meta_data.cpp
 * \brief file information helper.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <fstream>
#include <vips/vips.h> //glib dependency for GChecksum API

namespace Platemaker::Infrastructure {

std::string FileMetaData::computeFileSha256(const std::string& filePath)
{
    // Through utf8ToPath(): opening from the narrow string sent the bytes to fopen(), which
    // reads them in the ANSI code page, so any path with a non-ASCII character (a Polish
    // "Mój dysk", an accented user name) failed to open and hashed to nothing.
    std::ifstream file(utf8ToPath(filePath), std::ios::binary);
    if (!file) return {};

    GChecksum* cs = g_checksum_new(G_CHECKSUM_SHA256);
    char buf[65536];
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
        g_checksum_update(cs,
            reinterpret_cast<const guchar*>(buf),
            static_cast<gssize>(file.gcount()));
        if (!file && !file.eof()) break; // IO error mid-file
    }
    const std::string result = g_checksum_get_string(cs); // GLib owns the string
    g_checksum_free(cs);
    return result;
}

} // namespace Platemaker::Infrastructure

