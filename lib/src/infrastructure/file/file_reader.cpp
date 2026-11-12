/**
 * \file lib/src/infrastructure/file/file_reader.cpp
 * \brief FileReader implementation — the sized stream behind the library's file reads.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "infrastructure/file/file_reader.hpp"

namespace Platemaker::Infrastructure {

namespace {

//! The stream's buffer: MSVC reads the file in requests of this size, where the default buffer makes
//! them 4 KiB each (measured on Google Drive: 25 MB/s with the default, about 900 MB/s with this).
constexpr std::size_t k_bufferBytes = std::size_t{1} << 20;

} // namespace

FileReader::FileReader(const std::filesystem::path& path)
    : m_buffer(std::make_unique_for_overwrite<char[]>(k_bufferBytes))
{
    m_stream.open(path, std::ios::binary);
    // After open(), never before: MSVC's filebuf has no C stream until it is open and ignores the buffer.
    if (m_stream.is_open())
        m_stream.rdbuf()->pubsetbuf(m_buffer.get(), static_cast<std::streamsize>(k_bufferBytes));
}

std::size_t FileReader::read(char* into, std::size_t count)
{
    m_stream.read(into, static_cast<std::streamsize>(count));
    return static_cast<std::size_t>(m_stream.gcount());
}

} // namespace Platemaker::Infrastructure
