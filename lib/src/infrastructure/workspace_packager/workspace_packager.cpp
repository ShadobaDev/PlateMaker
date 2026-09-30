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

#include <platemaker/version.hpp>

#include <nlohmann/json.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

// ---------------------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------------------

/// How much of a file is read, written and reported at a time.
constexpr std::size_t k_chunkBytes = std::size_t{1} << 20;

/// Every entry is an ordinary file anyone may read; nothing in a package is executable.
constexpr int k_entryPermissions = 0644;

/// The manifest's own format; a reader that knows an older one can say so.
constexpr int k_manifestFormat = 1;

/// Formats whose bytes are compressed already: deflating them again costs time and saves nothing.
bool compressedAlready(const std::string& target)
{
    const std::string ext = foldCase(pathToUtf8(utf8ToPath(target).extension()));
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp";
}

/// A target that could land outside the package when unpacked, or that no zip should carry.
bool unsafeTarget(const std::string& target)
{
    if (target.empty() || target.find('\\') != std::string::npos)
        return true;
    const fs::path p = utf8ToPath(target);
    if (p.has_root_name() || p.has_root_directory())
        return true;
    return std::any_of(p.begin(), p.end(), [](const fs::path& part) { return part == ".."; });
}

std::string utcTimestamp(std::time_t t)
{
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/// One archive being written, closed and freed however the writing ends.
class ZipWriter {
public:
    ZipWriter(const fs::path& file, std::time_t stamp) : m_a(archive_write_new()), m_stamp(stamp)
    {
        if (!m_a)
            throw std::runtime_error("WorkspacePackager::write() — cannot create an archive");
        check(archive_write_set_format_zip(m_a), "zip format");
        // Names are UTF-8, and the zip says so (the language-encoding flag), whatever the platform.
        check(archive_write_set_format_option(m_a, "zip", "hdrcharset", "UTF-8"), "UTF-8 names");
#ifdef _WIN32
        check(archive_write_open_filename_w(m_a, file.c_str()), pathToUtf8(file));
#else
        check(archive_write_open_filename(m_a, file.c_str()), pathToUtf8(file));
#endif
    }

    ~ZipWriter()
    {
        if (m_a)
            archive_write_free(m_a);   // closes too; only reached here on a failure, when nothing is kept
    }

    ZipWriter(const ZipWriter&)            = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    /// Starts entry \p target of \p size bytes; the data follows through data().
    void begin(const std::string& target, std::uint64_t size)
    {
        // Per entry: store what is compressed already, deflate the rest.
        check(compressedAlready(target) ? archive_write_zip_set_compression_store(m_a)
                                        : archive_write_zip_set_compression_deflate(m_a),
              target);
        archive_entry* e = archive_entry_new();
        archive_entry_set_pathname_utf8(e, target.c_str());
        archive_entry_set_size(e, static_cast<la_int64_t>(size));   // known, so ZIP64 is used where needed
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_perm(e, k_entryPermissions);
        archive_entry_set_mtime(e, m_stamp, 0);
        const int r = archive_write_header(m_a, e);
        archive_entry_free(e);
        check(r, target);
    }

    void data(const char* bytes, std::size_t n, const std::string& target)
    {
        if (archive_write_data(m_a, bytes, n) < 0)
            check(ARCHIVE_FATAL, target);
    }

    void text(const std::string& target, const std::string& content)
    {
        begin(target, content.size());
        data(content.data(), content.size(), target);
    }

    /// Finishes the archive — the central directory is written here, so a package is only valid after it.
    void close()
    {
        check(archive_write_close(m_a), "closing the archive");
        archive_write_free(m_a);
        m_a = nullptr;
    }

private:
    void check(int r, const std::string& what)
    {
        if (r >= ARCHIVE_WARN)   // ARCHIVE_OK or ARCHIVE_WARN: written
            return;
        const char* why = archive_error_string(m_a);
        throw std::runtime_error("WorkspacePackager::write() — " + what + ": " + (why ? why : "archive error"));
    }

    archive*    m_a;
    std::time_t m_stamp;
};

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
            // An input's hash is the one its last render saw — sanitize() takes a matching hash as
            // "processed", whatever the status says. Kept, it would make a copy with no outputs look
            // rendered and up to date. (Overlays keep theirs: it names their content, nothing else.)
            clear(input, "sha256");
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

bool WorkspacePackager::write(const PackagePlan&       plan,
                              const std::string&       zipPath,
                              const PackageProgress&   progress,
                              const CancellationToken* cancel)
{
    const auto refuse = [](const std::string& why) {
        return std::runtime_error("WorkspacePackager::write() — " + why);
    };

    // --- A plan that cannot be written is refused before anything is -----------------------------------
    std::unordered_set<std::string> taken;
    const auto claim = [&](const std::string& target) {
        if (unsafeTarget(target))
            throw refuse("not a path inside the package: '" + target + "'");
        if (!taken.insert(foldCase(target)).second)
            throw refuse("two files for one place in the package: '" + target + "'");
    };
    for (const auto& f : plan.files)
        claim(f.target);
    claim(plan.workspaceFileName);
    claim(k_packageManifestFileName);

    nlohmann::json details = nlohmann::json::object();
    if (!plan.applicationManifest.empty()) {
        details = nlohmann::json::parse(plan.applicationManifest, nullptr, /*allow_exceptions=*/false);
        if (!details.is_object())
            throw refuse("the application's manifest is not a JSON object");
    }

    std::vector<std::uint64_t> sizes;
    sizes.reserve(plan.files.size());
    std::uint64_t total = plan.workspaceJson.size();
    for (const auto& f : plan.files) {
        std::error_code ec;
        const auto      size = fs::file_size(utf8ToPath(f.source), ec);
        if (ec)
            throw refuse("cannot read '" + f.source + "': " + ec.message());
        sizes.push_back(size);
        total += size;
    }

    // --- Into a temporary file beside the package, renamed over it only once it is whole -----------------
    const fs::path finalPath = utf8ToPath(zipPath);
    const fs::path tmpPath =
        finalPath.parent_path() / utf8ToPath(pathToUtf8(finalPath.filename()) + ".tmp");
    const std::time_t now = std::time(nullptr);

    std::uint64_t done      = 0;
    bool          cancelled = false;
    const auto    advance   = [&](std::uint64_t n) {
        done += n;
        if (progress)
            progress(done, total);
    };

    try {
        ZipWriter         zip(tmpPath, now);
        std::vector<char> buffer(k_chunkBytes);

        for (std::size_t i = 0; i < plan.files.size() && !cancelled; ++i) {
            const auto&   f = plan.files[i];
            std::ifstream in(utf8ToPath(f.source), std::ios::binary);
            if (!in)
                throw refuse("cannot read '" + f.source + "'");
            zip.begin(f.target, sizes[i]);
            for (std::uint64_t left = sizes[i]; left > 0;) {
                if (cancel && cancel->isCancelled()) {
                    cancelled = true;
                    break;
                }
                const auto n = static_cast<std::streamsize>(std::min<std::uint64_t>(left, k_chunkBytes));
                in.read(buffer.data(), n);
                if (in.gcount() != n)
                    throw refuse("'" + f.source + "' changed while it was being packed");
                zip.data(buffer.data(), static_cast<std::size_t>(n), f.target);
                left -= static_cast<std::uint64_t>(n);
                advance(static_cast<std::uint64_t>(n));
            }
        }

        if (!cancelled) {
            zip.text(plan.workspaceFileName, plan.workspaceJson);
            advance(plan.workspaceJson.size());

            nlohmann::json missing = nlohmann::json::array();
            for (const auto& m : plan.missing)
                missing.push_back({{"project", m.project}, {"source", m.source}, {"target", m.target}});

            const nlohmann::json manifest = {
                {"format", k_manifestFormat},
                {"library", {{"name", "libplatemaker"}, {"version", std::string(version_string)}}},
                {"application",
                 {{"name", plan.applicationName}, {"version", plan.applicationVersion}, {"details", details}}},
                {"exported", utcTimestamp(now)},
                {"workspace", plan.workspaceFileName},
                {"missing", missing},
            };
            zip.text(k_packageManifestFileName, manifest.dump(4));   // last: a package with a manifest is whole
            zip.close();
        }
    } catch (...) {
        std::error_code ec;
        fs::remove(tmpPath, ec);
        throw;
    }

    std::error_code ec;
    if (cancelled) {
        fs::remove(tmpPath, ec);
        return false;
    }
    fs::rename(tmpPath, finalPath, ec);
    if (ec) {
        // Across devices a rename cannot work; a copy then does the same job (as WorkspaceSerializer::save()).
        fs::copy_file(tmpPath, finalPath, fs::copy_options::overwrite_existing, ec);
        std::error_code ignored;
        fs::remove(tmpPath, ignored);
        if (ec)
            throw refuse("cannot move the package to '" + zipPath + "': " + ec.message());
    }
    return true;
}

} // namespace Platemaker::Infrastructure
