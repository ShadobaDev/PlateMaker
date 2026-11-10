/**
 * \file lib/src/infrastructure/file/file_reader.hpp
 * \brief A file read whole, in blocks — internal, shared by every reader of a file's bytes.
 *
 * Hashing a page and packing a workspace read every byte of large files, often on a cloud drive (Google
 * Drive's "G:"). std::ifstream does that slowly there: with MSVC it read such a drive at 25 MB/s whatever
 * its buffer, where fread() with a 1 MiB buffer read the same files at about 1 GB/s. Hashing a chapter
 * took ten seconds instead of a quarter of one, and held the drive for every other read meanwhile. The
 * library reads through this class so that it reads one way, and fast, everywhere.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_FILE_FILE_READER_HPP
#define PLATEMAKER_INFRASTRUCTURE_FILE_FILE_READER_HPP

#include <cstddef>
#include <cstdio>
#include <filesystem>

namespace Platemaker::Infrastructure {

/**
 * \brief Reads a file from its start, in the blocks the caller asks for.
 *
 * A file that cannot be opened (missing, a folder on Windows, no permission) is not open, and reads
 * nothing. A short read means the end of the file or a failure: a caller that knows the size it expects
 * compares the count.
 */
class FileReader {
public:
    //! Opens \p path for reading; isOpen() tells whether it did.
    explicit FileReader(const std::filesystem::path& path);
    ~FileReader();

    FileReader(const FileReader&)            = delete;
    FileReader& operator=(const FileReader&) = delete;

    [[nodiscard]] bool isOpen() const { return m_file != nullptr; }

    /**
     * \brief Reads up to \p count bytes into \p into.
     *
     * \return The bytes read: \p count, or fewer at the end of the file or on a failure; 0 when not open.
     */
    [[nodiscard]] std::size_t read(char* into, std::size_t count);

private:
    std::FILE* m_file = nullptr;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_FILE_FILE_READER_HPP
