/**
 * \file lib/src/infrastructure/file/file_reader.cpp
 * \brief FileReader implementation — the C stream behind the library's file reads.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "infrastructure/file/file_reader.hpp"

namespace Platemaker::Infrastructure {

namespace {

//! The stream's buffer: measured on Google Drive, 64 KiB reads through it ran three times faster than
//! with the C library's default buffer.
constexpr std::size_t k_bufferBytes = std::size_t{1} << 20;

} // namespace

FileReader::FileReader(const std::filesystem::path& path)
{
#ifdef _WIN32
    // The wide path: a narrow one would be read in the ANSI code page ("Mój dysk" would not open).
    if (_wfopen_s(&m_file, path.c_str(), L"rb") != 0)
        m_file = nullptr;
#else
    m_file = std::fopen(path.c_str(), "rb");
#endif
    if (m_file)
        std::setvbuf(m_file, nullptr, _IOFBF, k_bufferBytes);
}

FileReader::~FileReader()
{
    if (m_file)
        std::fclose(m_file);
}

std::size_t FileReader::read(char* into, std::size_t count)
{
    return m_file ? std::fread(into, 1, count, m_file) : 0;
}

} // namespace Platemaker::Infrastructure
