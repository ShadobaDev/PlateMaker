/**
 * \file lib/src/infrastructure/file/atomic_file.cpp
 * \brief A file written whole or not at all — see the header.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "infrastructure/file/atomic_file.hpp"

#include <platemaker/infrastructure/file/path_utf8.hpp>

#include <fstream>
#include <stdexcept>

namespace Platemaker::Infrastructure {

namespace fs = std::filesystem;

std::error_code publishTempFile(const fs::path& temp, const fs::path& target, bool copyFallback)
{
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (!ec)
        return ec;
    if (copyFallback)
        fs::copy_file(temp, target, fs::copy_options::overwrite_existing, ec);
    // A copy leaves the temporary file where it was, a failure too. Whether this removal works changes
    // nothing: the target either has the new bytes or keeps the old ones.
    std::error_code removed;
    fs::remove(temp, removed);
    return ec;
}

void writeTextFileAtomically(const fs::path& target, const std::string& text, const std::string& who)
{
    const fs::path temp = target.parent_path() / utf8ToPath(pathToUtf8(target.filename()) + ".tmp");
    bool written = false;
    {
        std::ofstream file(temp, std::ios::out | std::ios::trunc);
        if (!file.is_open())
            throw std::runtime_error(who + " — cannot open temp file '" + pathToUtf8(temp) + "' for writing");
        file << text;
        file.close();   // flushes: a full disk shows here, not in the destructor where nobody sees it
        written = !file.fail();
    }
    if (!written) {
        std::error_code removed;
        fs::remove(temp, removed);
        throw std::runtime_error(who + " — write error for '" + pathToUtf8(temp) + "'");
    }
    if (const std::error_code ec = publishTempFile(temp, target, /*copyFallback=*/true))
        throw std::runtime_error(who + " — cannot move temp file to '" + pathToUtf8(target) + "': " + ec.message());
}

} // namespace Platemaker::Infrastructure
