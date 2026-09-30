/**
 * \file lib/include/platemaker/infrastructure/workspace_packager/workspace_packager.hpp
 * \brief WorkspacePackager — plans a workspace package: one root holding the workspace and every file it
 *        references, so it opens on another machine with no path fixing.
 *
 * A package is **the same workspace, copied, with its paths rewritten** — not a new one. Nothing is
 * re-imported or rebuilt: project, input and overlay uids stay (overlays are anchored to input uids, so a
 * page file renamed on a name clash moves no bubble), and an overlay's \c sha256 stays, because the bytes
 * are copied, not re-encoded. It is the shape of InDesign's *Package* and Blender Asset Tracer's pack: copy
 * everything into one root, repoint the copy at the copies, leave the original alone.
 *
 * **A package mirrors the workspace folder.** The library knows no subfolder of a workspace by name — how
 * a consumer lays its folder out is the consumer's business — so a file in the folder keeps its path there,
 * whatever it is. Only what the workspace names from **elsewhere** is given a place, and those are the only
 * names the library chooses:
 * \code
 *   <name>.platemaker.json   the copy; every path in it is rootless, read against the root on load
 *   package.json             the manifest: who exported it, with what, when, and what is missing
 *   …                        every file from the workspace folder, at its own path there
 *   inputs/                  pages from outside the folder, flat
 *   external/                anything else from outside the folder, flat
 * \endcode
 * A name brought in keeps its file name and gets " (2)" only where it is taken; the folder's own files are
 * placed first, so nothing from elsewhere ever displaces one. Because an unpacked package is just the folder
 * again, whatever reads a workspace folder reads a package — a change of layout is migrated in one place.
 *
 * **What a package does not carry: anything a render reproduces.** No output files, no output directory
 * (workspace or project), and none of the state that described the last render — the render baselines,
 * and each input's status, hash, outputs and cached thumbnail. (An input's \c sha256 is the hash its last
 * render saw, which \c sanitize() reads as "processed"; kept, a copy with no outputs would look rendered.)
 * A package opens as a workspace that has not been rendered, and rendering it reproduces the outputs.
 *
 * **Missing files do not stop a plan.** A file that is not on disk stays declared in the copy, pointing
 * at the place it would have had — so dropping the file there is enough to bring it back — and is listed
 * in \ref PackagePlan::missing.
 *
 * **Two phases, so a consumer can add what only it knows about.** \ref plan() lists what the model
 * references. A consumer then appends its own \ref PackageFile entries — the GUI adds the pictures behind
 * lettered pictures and the workspace's fonts — and \c write() archives the result. The library
 * never decides which of a consumer's files belong in a package.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-30
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#ifndef PLATEMAKER_INFRASTRUCTURE_WORKSPACE_PACKAGER_HPP
#define PLATEMAKER_INFRASTRUCTURE_WORKSPACE_PACKAGER_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/models/workspace.hpp>

namespace Platemaker::Infrastructure {

/// \brief One file to put in a package.
struct PackageFile {
    std::string source;   //!< Where it is now: an absolute path, UTF-8.
    std::string target;   //!< Where it goes: relative to the package root, '/'-separated.
};

/// \brief A file the workspace references that is not on disk. Declared in the copy, not packed.
struct PackageMissing {
    std::string project;  //!< The project that references it (its name); empty for a workspace-level file.
    std::string source;   //!< The path the workspace names, UTF-8.
    std::string target;   //!< Where the copy expects it, relative to the package root.
};

/// \brief Everything a package will hold, before any of it is written.
struct PackagePlan {
    std::string                 workspaceFileName; //!< The copy's file name at the package root.
    std::string                 workspaceJson;     //!< The copy itself, every path in it rootless.
    std::vector<PackageFile>    files;             //!< What gets copied; a consumer may append its own.
    std::vector<PackageMissing> missing;           //!< Referenced, not on disk, not packed.

    // --- The consumer's part of the manifest; filled in by the consumer before write(). ---------------
    std::string applicationName;     //!< Who exported it, e.g. "Platemaker" or "platemaker-cli".
    std::string applicationVersion;  //!< Its version.
    /// A JSON object of the consumer's own — what it packed beyond the model, what it could not — recorded
    /// as is. Empty for none. The library does not read it; it only checks that it is an object.
    std::string applicationManifest;
};

/// \brief The manifest's name at a package root.
///
/// Outside WorkspacePackager on purpose: a static data member of an exported class is imported from the
/// DLL wherever it is used by address, and MinGW then wants a symbol the DLL does not export.
inline constexpr const char* k_packageManifestFileName = "package.json";

/// \brief Reports how much of a package is written or read: bytes so far, and in all.
using PackageProgress = std::function<void(std::uint64_t done, std::uint64_t total)>;

/// \brief What a package's \c package.json says, as read back by \ref WorkspacePackager::unpack().
struct PackageManifest {
    int                         format = 0;          //!< The manifest's own format.
    std::string                 libraryVersion;      //!< The libplatemaker that wrote it.
    std::string                 applicationName;     //!< Who exported it.
    std::string                 applicationVersion;  //!< Its version.
    std::string                 applicationDetails;  //!< The application's own section, as JSON text ("{}" if none).
    std::string                 exported;            //!< When, UTC, ISO 8601.
    std::string                 workspaceFileName;   //!< The workspace file at the package root.
    std::vector<PackageMissing> missing;             //!< What was missing when it was exported.
};

/// \brief An unpacked package: where its workspace file is now, and what its manifest says.
struct UnpackedPackage {
    std::string     workspaceFile;   //!< Absolute path, UTF-8.
    PackageManifest manifest;
};

/**
 * \class WorkspacePackager
 * \brief Builds a \ref PackagePlan from a workspace. Stateless; reads the file system only to ask which
 *        referenced files exist.
 */
class PLATEMAKER_EXPORT WorkspacePackager {
public:
    /**
     * \brief Plans a package of \p workspace, as saved at \p workspaceFilePath.
     *
     * Every file the model references — each input page, each overlay asset, each canvas profile's
     * template — gets one place in the package, and the copy is rewritten to name those places. One file
     * referenced twice (a page two projects share, an overlay two bubbles share) is packed once. The
     * original workspace is not touched.
     *
     * \param workspace          The workspace to package — the one in memory, not re-read from disk.
     * \param workspaceFilePath  Where it is saved (UTF-8): names the copy, and is the folder a relative
     *                           template path is read against.
     */
    [[nodiscard]] static PackagePlan plan(const Models::Workspace& workspace,
                                          const std::string&       workspaceFilePath);

    /**
     * \brief Writes \p plan as a zip at \p zipPath: every file, then the workspace copy, then the manifest.
     *
     * - **Checked before a byte is written:** every target is a relative path that stays inside the
     *   package (no root, no `..`), no two targets collide (compared without case), and
     *   \c applicationManifest is a JSON object or empty.
     * - **UTF-8 names**, flagged as such; ZIP64 where an entry or the archive needs it. Formats that are
     *   compressed already (PNG, JPEG, WebP) are stored, everything else deflated.
     * - **Whole or not at all:** written to \c zipPath + ".tmp" and renamed over \p zipPath at the end, so
     *   a failure or a cancel leaves no half a package — and no partial file where an old package was.
     * - The manifest, \c package.json, is written last: the library's name and version, the export
     *   time (UTC), the workspace file's name, the missing files, and the consumer's part under
     *   \c "application".
     *
     * \param progress Called after each chunk with the bytes written so far and in all; may be empty.
     * \param cancel   Checked between chunks; may be null.
     * 
     * \return \c true when the package was written, \c false when \p cancel stopped it (nothing is left).
     * \throws std::runtime_error on an invalid plan, a file that cannot be read, or an archive error — in
     *         each case naming the file.
     */
    static bool write(const PackagePlan&       plan,
                      const std::string&       zipPath,
                      const PackageProgress&   progress = {},
                      const CancellationToken* cancel   = nullptr);

    /**
     * \brief Unpacks the package at \p zipPath into \p folder, which it creates.
     *
     * - **A new folder only:** refused if \p folder exists, so nothing is overwritten and the folder holds
     *   this one workspace and nothing else.
     * - **Whole or not at all:** unpacked into \p folder + ".partial" and renamed to \p folder at the end;
     *   a failure or a cancel removes it. A ".partial" folder left by a crash is refused, not reused.
     * - **Nothing lands outside \p folder:** an entry with a root or a `..` is refused, as is an entry that
     *   is not an ordinary file, and two entries for one file (compared without case).
     * - **It must be a package:** \c package.json has to be there and name a workspace file at the root that
     *   was unpacked too. The workspace itself is not loaded: that is the caller's next step.
     *
     * \param progress Called as the zip is read: its bytes read so far, and its size.
     * \param cancel   Checked between chunks; may be null.
     * \return The workspace file and the manifest, or \c std::nullopt when \p cancel stopped it (nothing
     *         is left).
     * \throws std::runtime_error when \p folder exists, the zip cannot be read or is not a package, or an
     *         entry is refused — naming it.
     */
    [[nodiscard]] static std::optional<UnpackedPackage> unpack(const std::string&       zipPath,
                                                               const std::string&       folder,
                                                               const PackageProgress&   progress = {},
                                                               const CancellationToken* cancel   = nullptr);
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_WORKSPACE_PACKAGER_HPP
