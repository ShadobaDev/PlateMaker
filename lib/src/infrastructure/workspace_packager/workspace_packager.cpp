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

#include "infrastructure/file/folder_paths.hpp"        // genericUtf8(), relativeInside()
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

/// Where a file from **outside** the workspace folder goes — the only names this library chooses. A file in
/// the folder keeps its place there, so a package holds the consumer's layout, whatever it is.
constexpr const char* k_inputsDir   = "inputs";     //!< pages: a person opening the zip looks for them here
constexpr const char* k_externalDir = "external";   //!< anything else the workspace named from elsewhere

/// Names in a package are compared the way the file systems it lands on compare them: without case.
/// ponytail: ASCII case only — two names differing only in the case of a non-ASCII letter are kept apart.
std::string foldCase(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/**
 * Gives every file the copy names one place in the package, and rewrites the copy to name it.
 *
 * Two passes, so the workspace's own files are never displaced: first every file **in the workspace
 * folder**, at its own path there — the package mirrors the folder, and knows nothing of what the consumer
 * calls its subfolders; then every file **from elsewhere**, into \c inputs/ (pages) or \c external/, under
 * its own name, with " (2)" only where the name is taken.
 */
class Planner {
public:
    Planner(PackagePlan& plan, const fs::path& folder, const std::vector<std::string>& reserved)
        : m_plan(plan)
    {
        std::error_code ec;
        m_folder = fs::weakly_canonical(folder, ec);
        if (ec)
            m_folder = folder.lexically_normal();
        for (const auto& name : reserved)
            m_taken.insert(foldCase(name));
    }

    /**
     * Records that \p field names a file (its current value, as the workspace names it) — one from outside
     * the folder goes under \p externalDir. Placed, and \p field rewritten, by settle().
     */
    void refer(nlohmann::json& field, const char* externalDir, std::string project)
    {
        Reference r;
        r.field       = &field;
        r.externalDir = externalDir;
        r.project     = std::move(project);
        r.modelPath   = field.get<std::string>();
        r.source      = absoluteOf(r.modelPath);
        std::error_code ec;
        r.exists = fs::is_regular_file(r.source, ec);
        // The same file however it was spelled: canonical as far as the disk goes, so the case of a folder
        // name typed differently still finds it inside the workspace folder.
        fs::path identity = fs::weakly_canonical(r.source, ec);
        if (ec)
            identity = r.source.lexically_normal();
        r.key      = foldCase(pathToUtf8(identity));
        r.inFolder = relativeInside(identity, m_folder);
        m_refs.push_back(std::move(r));
    }

    void settle()
    {
        for (const bool inside : {true, false})
            for (const Reference& r : m_refs)
                if (r.inFolder.empty() != inside)
                    *r.field = place(r);
    }

private:
    struct Reference {
        nlohmann::json* field = nullptr;   // a value in the copy; stable, since no key is added or removed after
        const char*     externalDir = nullptr;
        std::string     project;
        std::string     modelPath;
        fs::path        source;
        bool            exists = false;
        std::string     key;               // the file's identity, case-folded
        std::string     inFolder;          // its path in the workspace folder; empty when it is not in it
    };

    std::string place(const Reference& r)
    {
        if (const auto it = m_placed.find(r.key); it != m_placed.end())
            return it->second;   // one file, one place, however many things name it

        const std::string target = r.inFolder.empty()
                                       ? claim(r.externalDir, r.source.filename())
                                       : claim(utf8ToPath(r.inFolder).parent_path(), utf8ToPath(r.inFolder).filename());
        m_placed.emplace(r.key, target);
        if (r.exists)
            m_plan.files.push_back({genericUtf8(r.source), target});
        else
            m_plan.missing.push_back({r.project, r.modelPath, target});
        return target;
    }

    /// A path from the model, made absolute: a rootless one is in the workspace folder, as load() reads it.
    fs::path absoluteOf(const std::string& modelPath) const
    {
        const fs::path p = utf8ToPath(modelPath);
        return p.has_root_path() ? p : m_folder / p;
    }

    /// The first free name in \p dir (empty = the package root): \p name, then "stem (2).ext", "stem (3).ext", …
    /// A file in the folder only fails its own name where a case-sensitive disk held two that differ in case.
    std::string claim(const fs::path& dir, const fs::path& name)
    {
        const std::string prefix = dir.empty() ? std::string{} : genericUtf8(dir) + '/';
        const std::string stem   = pathToUtf8(name.stem());
        const std::string ext    = pathToUtf8(name.extension());
        for (int n = 1;; ++n) {
            const std::string candidate =
                prefix + (n == 1 ? stem + ext : stem + " (" + std::to_string(n) + ")" + ext);
            if (m_taken.insert(foldCase(candidate)).second)
                return candidate;
        }
    }

    PackagePlan&                                 m_plan;
    fs::path                                     m_folder;
    std::vector<Reference>                       m_refs;
    std::unordered_map<std::string, std::string> m_placed;   //!< file identity → its place
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
    // The root's own two files are never given to anything else.
    Planner planner(plan, file.parent_path(), {plan.workspaceFileName, k_packageManifestFileName});

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
            planner.refer(input["filePath"], k_inputsDir, name);
            input["status"] = Models::FileStatus::Pending;   // not rendered, in this copy
            // An input's hash is the one its last render saw — sanitize() takes a matching hash as
            // "processed", whatever the status says. Kept, it would make a copy with no outputs look
            // rendered and up to date. (Overlays keep theirs: it names their content, nothing else.)
            clear(input, "sha256");
            clear(input, "lastProcessed");
            clear(input, "contributesTo");
            clear(input, "thumbnailPath");                    // a cache on the machine it came from
        }

        for (auto& overlay : project["stripOverlays"])
            if (!overlay.value("assetPath", std::string{}).empty())
                planner.refer(overlay["assetPath"], k_externalDir, name);
    }

    // Templates are named relative to the workspace folder already; a relative one is simply in it.
    for (auto& profile : j["canvasProfiles"])
        if (auto& info = profile["templateInfo"]; info.is_object() && !info.value("path", std::string{}).empty())
            planner.refer(info["path"], k_externalDir, {});

    planner.settle();
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

std::optional<UnpackedPackage> WorkspacePackager::unpack(const std::string&       zipPath,
                                                         const std::string&       folder,
                                                         const PackageProgress&   progress,
                                                         const CancellationToken* cancel)
{
    const auto refuse = [](const std::string& why) {
        return std::runtime_error("WorkspacePackager::unpack() — " + why);
    };

    const fs::path target  = fs::absolute(utf8ToPath(folder));
    const fs::path partial = target.parent_path() / utf8ToPath(pathToUtf8(target.filename()) + ".partial");
    std::error_code ec;
    if (fs::exists(target, ec))
        throw refuse("'" + pathToUtf8(target) + "' exists already");
    if (fs::exists(partial, ec))
        throw refuse("'" + pathToUtf8(partial) + "' is left from an unpack that did not finish; remove it");

    const fs::path zip   = utf8ToPath(zipPath);
    const auto     total = fs::file_size(zip, ec);
    if (ec)
        throw refuse("cannot read '" + zipPath + "': " + ec.message());

    // Owned from here on: whatever goes wrong, the half-unpacked folder goes with it.
    struct Reader {
        archive* a = archive_read_new();
        ~Reader() { archive_read_free(a); }
    } reader;
    archive* const a = reader.a;
    const auto     archiveError = [&](const std::string& what) {
        const char* why = archive_error_string(a);
        return refuse(what + ": " + (why ? why : "archive error"));
    };

    bool cancelled = false;
    try {
        fs::create_directories(partial);
        archive_read_support_format_zip(a);
#ifdef _WIN32
        if (archive_read_open_filename_w(a, zip.c_str(), k_chunkBytes) != ARCHIVE_OK)
#else
        if (archive_read_open_filename(a, zip.c_str(), k_chunkBytes) != ARCHIVE_OK)
#endif
            throw archiveError(zipPath);

        std::unordered_set<std::string> seen;
        archive_entry*                  entry = nullptr;
        for (int r; (r = archive_read_next_header(a, &entry)) != ARCHIVE_EOF && !cancelled;) {
            if (r < ARCHIVE_WARN)
                throw archiveError(zipPath);
            const char*       utf8 = archive_entry_pathname_utf8(entry);
            const std::string name = utf8 ? utf8 : (archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "");
            if (archive_entry_filetype(entry) == AE_IFDIR)
                continue;   // folders come with the files in them
            if (archive_entry_filetype(entry) != AE_IFREG)
                throw refuse("'" + name + "' is not an ordinary file");
            if (unsafeTarget(name))
                throw refuse("'" + name + "' would land outside the folder");
            if (!seen.insert(foldCase(name)).second)
                throw refuse("two entries for '" + name + "'");

            const fs::path out = partial / utf8ToPath(name);
            fs::create_directories(out.parent_path());
            std::ofstream file(out, std::ios::binary);
            if (!file)
                throw refuse("cannot write '" + pathToUtf8(out) + "'");

            const void* block  = nullptr;
            size_t      size   = 0;
            la_int64_t  offset = 0;
            for (int d; (d = archive_read_data_block(a, &block, &size, &offset)) != ARCHIVE_EOF;) {
                if (d < ARCHIVE_WARN)
                    throw archiveError(name);
                if (cancel && cancel->isCancelled()) {
                    cancelled = true;
                    break;
                }
                file.seekp(static_cast<std::streamoff>(offset));   // a sparse entry skips its holes
                file.write(static_cast<const char*>(block), static_cast<std::streamsize>(size));
                if (progress)
                    progress(static_cast<std::uint64_t>(archive_filter_bytes(a, -1)), total);
            }
            if (!file)
                throw refuse("cannot write '" + pathToUtf8(out) + "'");
        }
    } catch (...) {
        fs::remove_all(partial, ec);
        throw;
    }
    if (cancelled) {
        fs::remove_all(partial, ec);
        return std::nullopt;
    }

    // --- It is a package only if its manifest says which workspace it holds, and that file came too ---
    UnpackedPackage result;
    try {
        nlohmann::json j;
        {   // closed before the rename below: Windows will not rename a folder with a file open in it
            std::ifstream manifestFile(partial / k_packageManifestFileName, std::ios::binary);
            if (!manifestFile)
                throw refuse("no " + std::string(k_packageManifestFileName) + " — not a Platemaker package");
            j = nlohmann::json::parse(manifestFile, nullptr, /*allow_exceptions=*/false);
        }
        if (!j.is_object())
            throw refuse(std::string(k_packageManifestFileName) + " is not a JSON object");

        PackageManifest& m   = result.manifest;
        m.format             = j.value("format", 0);
        m.exported           = j.value("exported", std::string{});
        m.workspaceFileName  = j.value("workspace", std::string{});
        if (const auto lib = j.find("library"); lib != j.end() && lib->is_object())
            m.libraryVersion = lib->value("version", std::string{});
        m.applicationDetails = "{}";
        if (const auto app = j.find("application"); app != j.end() && app->is_object()) {
            m.applicationName    = app->value("name", std::string{});
            m.applicationVersion = app->value("version", std::string{});
            if (const auto d = app->find("details"); d != app->end() && d->is_object())
                m.applicationDetails = d->dump();
        }
        if (const auto missing = j.find("missing"); missing != j.end() && missing->is_array())
            for (const auto& x : *missing)
                if (x.is_object())
                    m.missing.push_back({x.value("project", std::string{}), x.value("source", std::string{}),
                                         x.value("target", std::string{})});

        if (m.workspaceFileName.empty() || unsafeTarget(m.workspaceFileName)
            || m.workspaceFileName.find('/') != std::string::npos
            || !fs::is_regular_file(partial / utf8ToPath(m.workspaceFileName), ec))
            throw refuse("the manifest names no workspace file at the package root");

        fs::rename(partial, target);
    } catch (...) {
        fs::remove_all(partial, ec);
        throw;
    }
    result.workspaceFile = genericUtf8(target / utf8ToPath(result.manifest.workspaceFileName));
    return result;
}

} // namespace Platemaker::Infrastructure
