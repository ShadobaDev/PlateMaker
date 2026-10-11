/**
 * \file lib/include/platemaker/infrastructure/file/file_meta_data.hpp
 * \brief file information helper.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-05
 *
 * \copyright Copyright (c) 2026 Bartlomiej Mucha <shadobadev@gmail.com>
 */


#ifndef PLATEMAKER_INFRASTRUCTURE_FILE_META_DATA_HPP
#define PLATEMAKER_INFRASTRUCTURE_FILE_META_DATA_HPP

#include "platemaker/platemaker_export.h"

#include <cstddef>
#include <string>

namespace Platemaker::Infrastructure {

/**
 * \class FileMetaData
 * \brief Represents a single file in the document.
 *
 * FileMetaData is a collection of static helper functions for computing and managing file metadata such as content hashes.
 *
 * FileMetaData is stateless and thread-safe.
 *
 */
class PLATEMAKER_EXPORT FileMetaData {
public:
    static std::string computeFileSha256(const std::string& filePath);

    /// SHA-256 of \p size bytes at \p data, as lowercase hex — for bytes about to be written, where
    /// reading the file back could see a stale copy (a synced folder).
    static std::string computeSha256(const void* data, std::size_t size);

public:
    FileMetaData() = delete;

};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_FILE_META_DATA_HPP
