/**
 * \file
 * \brief WorkspaceSerializer — reads and writes Workspace objects as .platemaker.json files.
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-01
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */


#ifndef PLATEMAKER_INFRASTRUCTURE_WORKSPACE_SERIALIZER_HPP
#define PLATEMAKER_INFRASTRUCTURE_WORKSPACE_SERIALIZER_HPP

#include <string>

#include <platemaker/models/workspace.hpp>

namespace Platemaker::Infrastructure {

/**
 * \class WorkspaceSerializer
 * \brief Serialises and deserialises a Workspace to and from a UTF-8 JSON file.
 *
 * The on-disk format is a single JSON object with a top-level \c "version" integer
 * field.  When loading a file whose version is lower than the current schema version,
 * the serialiser runs the migration chain automatically before returning the object.
 *
 * WorkspaceSerializer is stateless and thread-safe for concurrent reads.
 * Concurrent writes to the same file are not supported — serialise write calls
 * externally if needed.
 *
 * \note The JSON library used internally is nlohmann/json (MIT licence).
 */
class WorkspaceSerializer {
public:
    WorkspaceSerializer() = default;

    /**
     * \brief Loads a Workspace from a .platemaker.json file.
     *
     * If the file's schema version is older than the current version, each
     * intermediate migration step is applied in sequence before returning.
     *
     * \param filePath Absolute path to the .platemaker.json file.
     * \return A fully populated Workspace object.
     *
     * \throws std::runtime_error if the file cannot be opened, is not valid JSON,
     *                            or fails a required schema field validation.
     */
    [[nodiscard]] Models::Workspace load(const std::string& filePath) const;

    /**
     * \brief Saves a Workspace to a .platemaker.json file.
     *
     * The file is written atomically (written to a temporary file then renamed)
     * to avoid corruption on unexpected termination.
     *
     * \param workspace The Workspace object to serialise.
     * \param filePath  Absolute path of the destination file.  Created if it does
     *                  not exist; overwritten if it does.
     *
     * \throws std::runtime_error if the file cannot be written.
     */
    void save(
        const Models::Workspace& workspace,
        const std::string&       filePath) const;

private:
    /**
     * \brief Runs the migration chain to bring an old workspace up to the current version.
     *
     * Called automatically by \c load() when the file version is below the current
     * schema version.  Each migration step is a small function that transforms the
     * JSON object in-place from version N to N+1.
     *
     * \param workspace   The workspace loaded from disk (potentially old schema).
     * \param fromVersion The schema version read from the file.
     */
    void migrate(Models::Workspace& workspace, int fromVersion) const;
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_WORKSPACE_SERIALIZER_HPP
