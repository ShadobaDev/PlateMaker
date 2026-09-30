/**
 * \file lib/src/infrastructure/workspace_packager/workspace_packager.cpp
 * \brief WorkspacePackager implementation — see the header for what a package is and is not.
 *
 * The copy is made on the workspace's JSON rather than on the model: a Workspace cannot be copied, and
 * the file form is what the package holds anyway. Every rewritten path is rootless, which
 * WorkspaceSerializer::load() reads against the package root — so a package needs no loader of its own.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-09-30
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <platemaker/infrastructure/workspace_packager/workspace_packager.hpp>

#include "infrastructure/model_json/model_json.hpp"   // the shared Models JSON codec

#include <platemaker/infrastructure/file/path_utf8.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace Platemaker::Infrastructure {

namespace {

namespace fs = std::filesystem;

/// The package's folders. One per kind of file, flat, so a person opening the zip can find a page.
constexpr const char* k_inputsDir    = "inputs";
constexpr const char* k_overlaysDir  = "overlays";
constexpr const char* k_templatesDir = "templates";

/// UTF-8 with '/' separators — the form consumers write paths in.
std::string genericUtf8(const fs::path& p)
{
    const std::u8string s = p.lexically_normal().generic_u8string();
    return std::string(s.begin(), s.end());
}

/// Names in a package are compared the way the file systems it lands on compare them: without case.
/// ponytail: ASCII case only — two names differing only in the case of a non-ASCII letter are kept apart.
std::string foldCase(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

class Planner {
public:
    Planner(PackagePlan& plan, fs::path folder) : m_plan(plan), m_folder(std::move(folder)) {}

    /**
     * Gives the file at \p modelPath (as the workspace names it) a place under \p dir, and returns that
     * place. A file already placed keeps its place; a file that is not on disk gets one too — so the copy
     * can name it — but is listed as missing instead of packed.
     */
    std::string place(const std::string& modelPath, const char* dir, const std::string& project)
    {
        const fs::path  source = absoluteOf(modelPath);
        std::error_code ec;
        const bool      exists = fs::is_regular_file(source, ec);
        // The same file, however it was spelled: canonical where it exists, normalised where it does not.
        const fs::path  identity = exists ? fs::weakly_canonical(source, ec) : source.lexically_normal();
        const std::string key    = foldCase(pathToUtf8(identity));

        if (const auto it = m_placed.find(key); it != m_placed.end())
            return it->second;

        const std::string target = claim(dir, source.filename());
        m_placed.emplace(key, target);
        if (exists)
            m_plan.files.push_back({genericUtf8(source), target});
        else
            m_plan.missing.push_back({project, modelPath, target});
        return target;
    }

private:
    /// A path from the model, made absolute: a rootless one is in the workspace folder, as load() reads it.
    fs::path absoluteOf(const std::string& modelPath) const
    {
        const fs::path p = utf8ToPath(modelPath);
        return p.has_root_path() ? p : m_folder / p;
    }

    /// The first free name under \p dir: \p name itself, then "stem (2).ext", "stem (3).ext", …
    std::string claim(const char* dir, const fs::path& name)
    {
        const std::string stem = pathToUtf8(name.stem());
        const std::string ext  = pathToUtf8(name.extension());
        for (int n = 1;; ++n) {
            const std::string candidate = std::string(dir) + '/'
                                        + (n == 1 ? stem + ext : stem + " (" + std::to_string(n) + ")" + ext);
            if (m_taken.insert(foldCase(candidate)).second)
                return candidate;
        }
    }

    PackagePlan&                                 m_plan;
    fs::path                                     m_folder;
    std::unordered_map<std::string, std::string> m_placed;   //!< source identity → its place
    std::unordered_set<std::string>              m_taken;    //!< places given out, case-folded
};

/// Resets \p key to the empty value of its own type — "", [] or {} — so the copy stays well-formed.
void clear(nlohmann::json& o, const char* key)
{
    if (o.contains(key))
        o[key] = nlohmann::json(o[key].type());
}

} // anonymous namespace

PackagePlan WorkspacePackager::plan(const Models::Workspace& workspace, const std::string& workspaceFilePath)
{
    const fs::path file = fs::absolute(utf8ToPath(workspaceFilePath));

    PackagePlan plan;
    plan.workspaceFileName = pathToUtf8(file.filename());
    Planner planner(plan, file.parent_path());

    nlohmann::json j = workspace;

    // --- What a render reproduces does not travel ---------------------------------------------------------
    clear(j, "outputDirectory");

    for (auto& project : j["projectItems"]) {
        const std::string name = project.value("name", std::string{});

        clear(project, "outputDirectory");
        clear(project, "outputFiles");
        clear(project, "inputDirectory");   // the folder the pages were scanned from, on the machine they left
        // The baselines of a render that is not in the package: with them, the copy would compare itself
        // against outputs it does not have.
        clear(project, "outputSignature");
        clear(project, "processingSignature");
        clear(project, "canvasProfileIdsAtRender");
        clear(project, "inputOrderAtRender");

        for (auto& input : project["inputFiles"]) {
            input["filePath"] = planner.place(input.value("filePath", std::string{}), k_inputsDir, name);
            input["status"]   = Models::FileStatus::Pending;   // not rendered, in this copy
            clear(input, "lastProcessed");
            clear(input, "contributesTo");
            clear(input, "thumbnailPath");                    // a cache on the machine it came from
        }

        for (auto& overlay : project["stripOverlays"])
            if (const std::string asset = overlay.value("assetPath", std::string{}); !asset.empty())
                overlay["assetPath"] = planner.place(asset, k_overlaysDir, name);
    }

    // --- Templates: relative to the workspace folder already, placed like everything else ----------------
    for (auto& profile : j["canvasProfiles"])
        if (auto& info = profile["templateInfo"]; info.is_object())
            if (const std::string path = info.value("path", std::string{}); !path.empty())
                info["path"] = planner.place(path, k_templatesDir, {});

    plan.workspaceJson = j.dump(4);
    return plan;
}

} // namespace Platemaker::Infrastructure
