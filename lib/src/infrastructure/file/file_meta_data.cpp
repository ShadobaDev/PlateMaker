/**
 * \file lib/src/infrastructure/file/file_meta_data.cpp
 * \brief file information helper.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-05
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */


#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/infrastructure/file/path_utf8.hpp>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vips/vips.h> //glib dependency for GChecksum API

namespace Platemaker::Infrastructure {

std::string FileMetaData::computeFileSha256(const std::string& filePath)
{
    // Through utf8ToPath(): opening from the narrow string sent the bytes to fopen(), which
    // reads them in the ANSI code page, so any path with a non-ASCII character (a Polish
    // "Mój dysk") failed to open and hashed to nothing.
    const std::filesystem::path path = utf8ToPath(filePath);
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);   // fails for a folder too
    if (ec) return {};
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    GChecksum* cs = g_checksum_new(G_CHECKSUM_SHA256);
    char buf[65536];
    std::uintmax_t hashed = 0;
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
        g_checksum_update(cs,
            reinterpret_cast<const guchar*>(buf),
            static_cast<gssize>(file.gcount()));
        hashed += static_cast<std::uintmax_t>(file.gcount());
    }   // a read error reads nothing more, so it ends the loop as the end of the file does
    // A read error can end the stream as its end does, so only the count tells them apart: the hash of
    // part of a file (or of one that changed size meanwhile) would pass for the whole, and say a changed
    // file is unchanged. Empty is "cannot be read", as callers already take it.
    const std::string result = hashed == size ? g_checksum_get_string(cs) : std::string{};   // GLib owns the string
    g_checksum_free(cs);
    return result;
}

std::string FileMetaData::computeSha256(const void* data, std::size_t size)
{
    GChecksum* cs = g_checksum_new(G_CHECKSUM_SHA256);
    g_checksum_update(cs, static_cast<const guchar*>(data), static_cast<gssize>(size));
    const std::string result = g_checksum_get_string(cs); // GLib owns the string
    g_checksum_free(cs);
    return result;
}

} // namespace Platemaker::Infrastructure

