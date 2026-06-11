/**
 * \file lib/include/platemaker/infrastructure/file/file_meta_data.hpp
 * \brief file information helper.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-05
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_INFRASTRUCTURE_FILE_META_DATA_HPP
#define PLATEMAKER_INFRASTRUCTURE_FILE_META_DATA_HPP

#include "platemaker/platemaker_export.h"

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

public:
    FileMetaData() = delete;

};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_FILE_META_DATA_HPP
