/**
 * \file lib/include/platemaker/infrastructure/workspace_packager/workspace_packager.hpp
 * \brief WorkspacePackager — plans a workspace package: one root holding the workspace and every file it
 *        references, so it opens on another machine with no path fixing.
 *
 * A package is **the same workspace, copied, with its paths rewritten** — not a new one. Nothing is
 * re-imported or rebuilt: project, input and overlay uids stay (overlays are anchored to input uids, so a
 * page file renamed on a name clash moves no bubble), and every \c sha256 stays, because the bytes are
 * copied, not re-encoded. It is the shape of InDesign's *Package* and Blender Asset Tracer's pack: copy
 * everything into one root, repoint the copy at the copies, leave the original alone.
 *
 * Layout of a package root:
 * \code
 *   <name>.platemaker.json   the copy; every path in it is rootless, read against the root on load
 *   inputs/                  the pages, flat; a name is kept and gets " (2)" only on a clash
 *   overlays/                every overlay asset
 *   templates/               every canvas profile's template
 * \endcode
 *
 * **What a package does not carry: anything a render reproduces.** No output files, no output directory
 * (workspace or project), and none of the state that described the last render — the render baselines,
 * each input's status, its outputs and its cached thumbnail. A package opens as a workspace that has not
 * been rendered, and rendering it reproduces the outputs.
 *
 * **Missing files do not stop a plan.** A page that is not on disk stays declared in the copy, pointing
 * at the place in \c inputs/ it would have had — so dropping the file there is enough to bring it back —
 * and is listed in \ref PackagePlan::missing. The same goes for an overlay asset or a template.
 *
 * **Two phases, so a consumer can add what only it knows about.** \ref plan() lists what the model
 * references. A consumer then appends its own \ref PackageFile entries — the GUI adds the pictures behind
 * lettered pictures and the workspace's fonts — and \c write() (next) archives the result. The library
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

#include <string>
#include <vector>

#include "platemaker/platemaker_export.h"

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
};

} // namespace Platemaker::Infrastructure

#endif // PLATEMAKER_INFRASTRUCTURE_WORKSPACE_PACKAGER_HPP
