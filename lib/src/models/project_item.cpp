/**
 * \file lib/src/infrastructure/workspace_serializer/project_item.cpp
 * \brief ProjectItem implementation.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-04
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/models/project_item.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>

#include <filesystem>

namespace Platemaker::Models {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ProjectItem::ProjectItem() noexcept
    : m_isUpToDate(false)
{}

ProjectItem::ProjectItem(ProjectItem&& other) noexcept
    : name(std::move(other.name))
    , uuid(std::move(other.uuid))
    , inputDirectory(std::move(other.inputDirectory))
    , m_input_images(std::move(other.m_input_images))
    , m_output_images(std::move(other.m_output_images))
    , m_output_directory(std::move(other.m_output_directory))
    , m_isUpToDate(other.m_isUpToDate)
{}

ProjectItem& ProjectItem::operator=(ProjectItem&& other) noexcept
{
    if (this != &other) {
        name             = std::move(other.name);
        uuid             = std::move(other.uuid);
        inputDirectory   = std::move(other.inputDirectory);
        m_input_images   = std::move(other.m_input_images);
        m_output_images  = std::move(other.m_output_images);
        m_output_directory = std::move(other.m_output_directory);
        m_isUpToDate     = other.m_isUpToDate;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

std::vector<InputFile>& ProjectItem::getInputImages() noexcept
{
    return m_input_images;
}

const std::vector<InputFile>& ProjectItem::getInputImages() const noexcept
{
    return m_input_images;
}

std::vector<OutputFile>& ProjectItem::getOutputImages() noexcept
{
    return m_output_images;
}

const std::vector<OutputFile>& ProjectItem::getOutputImages() const noexcept
{
    return m_output_images;
}

std::string& ProjectItem::getOutputDirectory() noexcept
{
    return m_output_directory;
}

const std::string& ProjectItem::getOutputDirectory() const noexcept
{
    return m_output_directory;
}

bool ProjectItem::isUpToDate() const noexcept
{
    return m_isUpToDate;
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

bool ProjectItem::sanitize()
{
    namespace fs = std::filesystem;

    m_isUpToDate = true;

    for (auto& file : m_input_images) {
        if (!fs::exists(file.filePath)) {
            file.status  = FileStatus::Missing;
            m_isUpToDate = false;
            continue;
        }

        if (file.sha256.empty()) {
            // Never processed — just mark as pending so the pipeline knows to run.
            file.status  = FileStatus::Pending;
            m_isUpToDate = false;
            continue;
        }

        const std::string currentHash =
            Infrastructure::FileMetaData::computeFileSha256(file.filePath);

        if (currentHash != file.sha256) {
            file.status  = FileStatus::Modified;
            m_isUpToDate = false;
        } else {
            file.status = FileStatus::Processed;
        }
    }

    return m_isUpToDate;
}

} // namespace Platemaker::Models
