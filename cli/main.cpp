/**
 * \file
 * \brief platemaker CLI — Stage 2 implementation.
 *
 * Supported subcommands:
 *   platemaker --version
 *   platemaker workspace create         [--output FILE] [--target-width N] [--slice-height N]
 *   platemaker workspace add-profile    --workspace FILE --name NAME --canvas WxH --margins T,R,B,L
 *   platemaker workspace mod-profile    --workspace FILE --name NAME [--canvas WxH] [--margins T,R,B,L]
 *   platemaker workspace rm-profile     --workspace FILE --name NAME
 *   platemaker workspace list-profiles  --workspace FILE
 *   platemaker workspace list-projects  --workspace FILE
 *   platemaker project create  --workspace FILE --name NAME [--input DIR] [--output DIR]
 *   platemaker project mod     --workspace FILE --name NAME [--new-name N] [--input DIR] [--output DIR]
 *   platemaker project rm      --workspace FILE --name NAME
 *   platemaker project status  --workspace FILE --name NAME
 *   platemaker process --workspace FILE
 *                      { --input DIR | --project NAME }
 *                      [--output DIR] [--format png|jpg|webp] [--start-index N]
 *                      [--target-width N] [--slice-height N]
 *                      [--no-profile] [--json]
 *   platemaker template --workspace FILE --profile NAME --output FILE
 *                       [--margins-tpl-color R,G,B[,A]] [--background-tpl-color R,G,B[,A]]
 * 
 * Canvas profile matching during process:
 *   - If the workspace has canvas profiles, each input file's pixel width is
 *     compared to every profile's canvasSize.width.  A matching profile's
 *     margins are applied (margin-aware pipeline).  Files whose width matches
 *     no profile are reported as incompatible and skipped.
 *   - If the workspace has no canvas profiles, all files are processed with
 *     the standard pipeline (no margin cropping).
 *
 * Exit codes:
 *   0  success
 *   1  usage / argument error
 *   2  IO error (file not found, permission denied, …)
 *   3  processing error (libvips failure, pipeline error, …)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * 
 * \author ShadobaDev <shadobadev@gmail.com>
 * \date 2026-06-02
 *
 * \copyright Copyright (c) 2026 ShadobaDev
 */

#include <vips/vips.h>

#include <platemaker/core/canvas_profile_matcher/canvas_profile_matcher.hpp>
#include <platemaker/core/margin_cropper/margin_cropper.hpp>
#include <platemaker/core/processing_pipeline/processing_pipeline.hpp>
#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/core/scaler/scaler.hpp>
#include <platemaker/core/template_generator/template_generator.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/infrastructure/id_generator/id_generator.hpp>
#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/common_types.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>
#include <platemaker/models/workspace.hpp>
#include <platemaker/version.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX          // MinGW's os_defines.h already defines it — don't redefine
#  endif
#  include <windows.h>       // SetConsoleOutputCP, GetCommandLineW, CommandLineToArgvW
#  include <shellapi.h>      // CommandLineToArgvW
#endif

namespace fs = std::filesystem;
using namespace Platemaker::Models;
using namespace Platemaker::Core;
using namespace Platemaker::Infrastructure;

// ===========================================================================
// Lightweight argument parser
// ===========================================================================

/**
 * \brief Parsed command-line options as a key→value string map.
 *
 * Boolean flags (e.g. \c --json) are stored with value \c "1".
 */
struct Opts {
    std::unordered_map<std::string, std::string> flags;

    [[nodiscard]] bool has(const std::string& key) const
    {
        return flags.count(key) > 0;
    }
    [[nodiscard]] std::string get(
        const std::string& key,
        const std::string& def = "") const
    {
        auto it = flags.find(key);
        return (it != flags.end()) ? it->second : def;
    }
    [[nodiscard]] int getInt(const std::string& key, int def) const
    {
        auto it = flags.find(key);
        if (it == flags.end()) return def;
        return std::stoi(it->second);
    }
};

static Opts parseOpts(int argc, char** argv, int start)
{
    Opts opts;
    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.size() < 2 || arg[0] != '-' || arg[1] != '-') continue;
        std::string key = arg.substr(2);
        if (i + 1 < argc) {
            std::string next = argv[i + 1];
            if (next.size() < 2 || next[0] != '-' || next[1] != '-') {
                opts.flags[key] = next;
                ++i;
                continue;
            }
        }
        opts.flags[key] = "1"; // boolean presence flag
    }
    return opts;
}

// ===========================================================================
// Helpers
// ===========================================================================


static OutputFormat parseFormat(const std::string& s)
{
    std::string lo = s;
    for (auto& c : lo)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lo == "png")                 return OutputFormat::PNG;
    if (lo == "jpg" || lo == "jpeg") return OutputFormat::JPEG;
    if (lo == "webp")                return OutputFormat::WebP;
    throw std::runtime_error("Unknown format '" + s + "'. Expected png, jpg, or webp.");
}

/// Parses "800x2560" → Size{800, 2560}.
static Size parseSize(const std::string& s)
{
    const auto pos = s.find('x');
    if (pos == std::string::npos)
        throw std::runtime_error(
            "Bad canvas format '" + s + "'. Expected WxH, e.g. 800x2560.");
    return {std::stoi(s.substr(0, pos)), std::stoi(s.substr(pos + 1))};
}

/// Parses "100,100,100,100" → Margins{top, right, bottom, left}.
static Margins parseMargins(const std::string& s)
{
    std::string sc = s;
    for (auto& c : sc) if (c == ',') c = ' ';
    std::istringstream ss(sc);
    int vals[4] = {0, 0, 0, 0};
    for (auto& v : vals) ss >> v;
    if (ss.fail())
        throw std::runtime_error(
            "Bad margins format '" + s +
            "'. Expected T,R,B,L, e.g. 100,100,100,100.");
    return {vals[0], vals[1], vals[2], vals[3]};
}

/**
 * \brief Parses an RGBA colour from a string in the format "R,G,B" or "R,G,B,A".
 *
 * Each component is an integer in the range 0–255.  The alpha component is
 * optional and defaults to 255 (fully opaque) when omitted.  Returns
 * \c std::nullopt if fewer than 3 components are provided or parsing fails.
 * Values are silently clamped to [0, 255].
 *
 * This function is deliberately lenient (no error is thrown on bad colour
 * format) because colours are a cosmetic option and should never abort
 * template generation.
 *
 * \param s Input string, e.g. "255,105,180,128" or "255,0,0".
 * \return Parsed RGBA, or std::nullopt on parse failure.
 */
static std::optional<RGBA> parseColour(const std::string& s)
{
    std::string sc = s;
    for (auto& c : sc) if (c == ',') c = ' ';
    std::istringstream ss(sc);
    int vals[4] = {0, 0, 0, 255}; // alpha defaults to fully opaque
    int count = 0;
    int v = 0;
    while (count < 4 && (ss >> v)) {
        vals[count++] = std::clamp(v, 0, 255);
    }
    if (count < 3) return std::nullopt; // need at least R, G, B
    return RGBA{
        static_cast<std::uint8_t>(vals[0]),
        static_cast<std::uint8_t>(vals[1]),
        static_cast<std::uint8_t>(vals[2]),
        static_cast<std::uint8_t>(vals[3])
    };
}

/// Scans \p dir for image files and returns them sorted by filename.
static std::vector<fs::path> scanImageDir(const fs::path& dir)
{
    static const std::set<std::string> kExts = {
        ".png", ".jpg", ".jpeg", ".tif", ".tiff"};
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (kExts.count(ext)) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// Returns the OutputProfile for \p project, falling back to the workspace default.
static OutputProfile resolveOutputProfile(const Workspace& ws, const ProjectItem& project)
{
    if (!project.outputProfileId.empty()) {
        for (const auto& op : ws.outputProfiles)
            if (op.id == project.outputProfileId) return op;
    }
    if (!ws.outputProfiles.empty()) return ws.outputProfiles.front();
    OutputProfile def;
    def.name = "Default";
    return def;
}


/**
 * \brief Returns the current UTC time as an ISO 8601 string (e.g. "2026-06-02T14:30:00Z").
 */
static std::string nowIso8601()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

/**
 * \brief Returns a "proj-" identifier that no project in \p ws is already using.
 *
 * These used to come from nowIso8601(), which resolves to the second — so two projects
 * created by one command came out identical.
 *
 * \note "uid" (unique identifier), not "uuid": what we generate is a random identifier,
 *       not an RFC 4122 UUID, and it has never had that layout.  ProjectItem still calls
 *       the field \c uuid; renaming it is a public API break, so it waits for 0.3.0.
 */
static std::string makeProjectUid(const Workspace& ws)
{
    std::vector<std::string> taken;
    taken.reserve(ws.projectItems.size());
    for (const auto& pi : ws.projectItems)
        taken.push_back(pi.uuid);

    return makeUniqueId("proj", taken);
}

// ===========================================================================
// platemaker --version
// ===========================================================================

static int cmdVersion()
{
    // First line stays machine-parseable: "platemaker <version>".
    std::cout << Platemaker::project_name << ' ' << Platemaker::version_string << '\n';
    std::cout << Platemaker::description << '\n';
    std::cout << "libvips " << vips_version(0) << '.'
              << vips_version(1) << '.' << vips_version(2) << '\n';
    return 0;
}

// ===========================================================================
// platemaker --help / help
// ===========================================================================

static int cmdHelp(const std::string& prog)
{
    std::cout
        << "Usage: " << prog << " <command> [options]\n"
        << "\n"
        << "Commands:\n"
        << "  --version\n"
        << "      Print version and exit.\n"
        << "\n"
        << "  workspace create  [--output FILE] [--target-width N] [--slice-height N]\n"
        << "      Create a new empty workspace JSON file.\n"
        << "      Default output: ./project.platemaker.json\n"
        << "\n"
        << "  workspace add-profile\n"
        << "      --workspace FILE --name NAME\n"
        << "      { --canvas WxH | --canvas-safe-area WxH }  --margins T,R,B,L\n"
        << "      --canvas and --canvas-safe-area are mutually exclusive.\n"
        << "        --canvas WxH          Absolute canvas size (margins included).\n"
        << "        --canvas-safe-area WxH  Drawable area only; canvas = safe-area + margins.\n"
        << "      Add a canvas profile to an existing workspace.\n"
        << "\n"
        << "  workspace add-profile / mod-profile also accept:\n"
        << "    --margins-tpl-color R,G,B[,A]    Margin overlay colour for templates.\n"
        << "    --background-tpl-color R,G,B[,A] Background fill colour for templates.\n"
        << "\n"
        << "  workspace mod-profile\n"
        << "      --workspace FILE --name NAME\n"
        << "      [--canvas WxH | --canvas-safe-area WxH]  [--margins T,R,B,L]\n"
        << "      --canvas and --canvas-safe-area are mutually exclusive.\n"
        << "      With --canvas-safe-area: uses existing margins unless --margins also given.\n"
        << "      Modify an existing canvas profile.\n"
        << "\n"
        << "  workspace rm-profile --workspace FILE --name NAME\n"
        << "      Remove a canvas profile.\n"
        << "\n"
        << "  workspace list-profiles --workspace FILE\n"
        << "      List canvas and output profiles in a workspace.\n"
        << "\n"
        << "  process --workspace FILE\n"
        << "          [--input DIR]  [--output DIR]\n"
        << "          [--format png|jpg|webp]  [--start-index N]\n"
        << "          [--target-width N]  [--slice-height N]\n"
        << "          [--json]\n"
        << "      Scale and slice all pages in the workspace.\n"
        << "      If canvas profiles are defined, each file is matched by width+height.\n"
        << "      Files not matching any profile are reported as incompatible.\n"
        << "      --no-profile: ignore canvas profiles; process all files with the\n"
        << "        standard pipeline (no margin cropping) using workspace output settings.\n"
        << "\n"
        << "  template --workspace FILE --profile NAME --output FILE\n"
        << "           [--margins-tpl-color R,G,B[,A]]  [--background-tpl-color R,G,B[,A]]\n"
        << "      Generate a canvas template PNG for use as a Procreate guide layer.\n"
        << "      The template shows margin zones, safe-area border, and slice guide lines.\n"
        << "      Colour resolution order (most to least specific):\n"
        << "        1. --margins-tpl-color / --background-tpl-color CLI arguments\n"
        << "        2. Colours stored in the named canvas profile (workspace JSON)\n"
        << "        3. Built-in defaults: margins=pink(255,105,180,128), bg=transparent(0,0,0,0)\n"
        << "      Bad colour values are silently ignored (colours are cosmetic).\n"
        << "\n"
        << " platemaker project create  --workspace FILE --name NAME\n"
        << "           [--input DIR] [--output DIR]\n"
        << " platemaker project mod     --workspace FILE --name NAME\n"
        << "           [--new-name N] [--input DIR] [--output DIR]\n"
        << "           [--add-canvas-profile NAME]  link canvas profile to project\n"
        << "           [--rm-canvas-profile  NAME]  unlink canvas profile from project\n"
        << "           [--output-profile     NAME]  assign output profile to project\n"
        << " platemaker project rm      --workspace FILE --name NAME\n"
        << " platemaker project status  --workspace FILE --name NAME\n"
        << "      Manage named projects (input directories) within a workspace.\n"
        << "\n"
        << "Exit codes: 0=success  1=usage error  2=IO error  3=processing error\n";
    return 0;
}

// ===========================================================================
// platemaker workspace create
// ===========================================================================

static int cmdWorkspaceCreate(const Opts& opts)
{
    const std::string outputFile  = opts.get("output", "project.platemaker.json");
    const int         targetWidth = opts.getInt("target-width", 800);
    const int         sliceHeight = opts.getInt("slice-height", 1280);

    // Seed from the library's preset rather than assembling one here — the GUI seeds from
    // the same table, so both produce a workspace carrying the identical preset id.
    OutputProfile op = webtoonStandardPreset();
    op.targetWidth   = targetWidth;
    op.sliceHeight   = sliceHeight;

    // --target-width / --slice-height can move it away from the preset. Once it differs it
    // is not the preset any more, so it must not keep the shared id — the user asked for
    // different settings, which by definition makes this their own profile. Without this
    // the CLI would emit a file violating the invariant that load() then has to repair.
    if (outputProfileSignature(op) != outputProfileSignature(webtoonStandardPreset()))
        op.id = makeUniqueOutputProfileId({}); // brand-new workspace: nothing taken yet

    // Assemble an empty workspace (no canvas profiles yet).
    Workspace ws;
    ws.version        = 2;
    ws.outputProfiles = {op};

    try {
        const auto dir = fs::path(outputFile).parent_path();
        if (!dir.empty()) fs::create_directories(dir);
        WorkspaceSerializer{}.save(ws, outputFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot write workspace: " << e.what() << '\n';
        return 2;
    }

    std::cerr << "Workspace created: " << fs::absolute(outputFile).string() << '\n';
    std::cerr << "Tip: add canvas profiles with:\n"
              << "  workspace add-profile --workspace " << outputFile
              << " --name NAME --canvas WxH --margins T,R,B,L\n";
    return 0;
}

// ===========================================================================
// platemaker workspace add-profile
// ===========================================================================

static int cmdWorkspaceAddProfile(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }
    if (!opts.has("name")) {
        std::cerr << "Error: --name NAME is required\n"; return 1;
    }
    // --canvas and --canvas-safe-area are mutually exclusive.
    if (opts.has("canvas") && opts.has("canvas-safe-area")) {
        std::cerr
            << "Error: --canvas and --canvas-safe-area are mutually exclusive.\n"
            << "  Use --canvas WxH for the absolute canvas size (margins included).\n"
            << "  Use --canvas-safe-area WxH + --margins T,R,B,L to let the tool\n"
            << "  compute the absolute canvas: canvas = safe-area + margins.\n";
        return 1;
    }
    if (!opts.has("canvas") && !opts.has("canvas-safe-area")) {
        std::cerr << "Error: either --canvas WxH or --canvas-safe-area WxH is required\n";
        return 1;
    }
    if (!opts.has("margins")) {
        std::cerr << "Error: --margins T,R,B,L is required\n"; return 1;
    }

    const std::string wsFile = opts.get("workspace");
    const std::string name   = opts.get("name");

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    // Duplicate name check.
    for (const auto& cp : ws.canvasProfiles) {
        if (cp.name == name) {
            std::cerr << "Error: profile '" << name
                      << "' already exists. Use mod-profile to modify it.\n";
            return 1;
        }
    }

    Size    canvasSize;
    Margins margins;
    try {
        margins = parseMargins(opts.get("margins"));
        if (opts.has("canvas")) {
            canvasSize = parseSize(opts.get("canvas"));
        } else {
            // --canvas-safe-area: compute absolute canvas = safe-area + margins.
            const Size safeArea = parseSize(opts.get("canvas-safe-area"));
            canvasSize.width  = safeArea.width  + margins.left + margins.right;
            canvasSize.height = safeArea.height + margins.top  + margins.bottom;
            std::cerr << "  Safe area  : " << safeArea.width << 'x' << safeArea.height << '\n'
                      << "  Canvas size: " << canvasSize.width << 'x' << canvasSize.height
                      << " (safe area + margins)\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    CanvasProfile cp;
    cp.id           = makeUniqueCanvasProfileId(ws.canvasProfiles);
    cp.name         = name;
    cp.canvasSize   = canvasSize;
    cp.margins      = margins;
    cp.visualColour     = {255, 105, 180, 128}; // default: pink overlay at 50 % alpha
    cp.backgroundColour = {0,   0,   0,   0};   // default: fully transparent

    // Optional template colour overrides — silently ignored on bad format.
    if (opts.has("margins-tpl-color")) {
        if (const auto col = parseColour(opts.get("margins-tpl-color")))
            cp.visualColour = *col;
        else
            std::cerr << "Warning: ignoring bad --margins-tpl-color value '"
                      << opts.get("margins-tpl-color") << "'\n";
    }
    if (opts.has("background-tpl-color")) {
        if (const auto col = parseColour(opts.get("background-tpl-color")))
            cp.backgroundColour = *col;
        else
            std::cerr << "Warning: ignoring bad --background-tpl-color value '"
                      << opts.get("background-tpl-color") << "'\n";
    }

    ws.canvasProfiles.push_back(cp);

    try {
        WorkspaceSerializer{}.save(ws, wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n';
        return 2;
    }

    std::cerr << "Profile '" << name << "' added.\n";
    return 0;
}

// ===========================================================================
// platemaker workspace mod-profile
// ===========================================================================

static int cmdWorkspaceModProfile(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }
    if (!opts.has("name")) {
        std::cerr << "Error: --name NAME is required\n"; return 1;
    }

    const std::string wsFile = opts.get("workspace");
    const std::string name   = opts.get("name");

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    CanvasProfile* cp = nullptr;
    for (auto& p : ws.canvasProfiles)
        if (p.name == name) { cp = &p; break; }
    if (!cp) {
        std::cerr << "Error: profile '" << name << "' not found\n"; return 1;
    }

    // --canvas and --canvas-safe-area are mutually exclusive.
    if (opts.has("canvas") && opts.has("canvas-safe-area")) {
        std::cerr
            << "Error: --canvas and --canvas-safe-area are mutually exclusive.\n"
            << "  Use --canvas WxH for the absolute canvas size (margins included).\n"
            << "  Use --canvas-safe-area WxH to compute canvas = safe-area + margins.\n"
            << "  (Existing profile margins are used when --margins is not provided.)\n";
        return 1;
    }

    if (opts.has("canvas")) {
        try { cp->canvasSize = parseSize(opts.get("canvas")); }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n'; return 1;
        }
    }
    // Update margins before computing canvas-safe-area so the result uses
    // the new margins when both flags are provided simultaneously.
    if (opts.has("margins") && !opts.has("canvas-safe-area")) {
        try { cp->margins = parseMargins(opts.get("margins")); }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n'; return 1;
        }
    }
    if (opts.has("canvas-safe-area")) {
        try {
            // When --margins is also given, use those margins; otherwise keep
            // the existing profile margins to compute the absolute canvas size.
            const Margins effMargins = opts.has("margins")
                ? parseMargins(opts.get("margins"))
                : cp->margins;
            const Size safeArea = parseSize(opts.get("canvas-safe-area"));
            cp->canvasSize.width  = safeArea.width  + effMargins.left + effMargins.right;
            cp->canvasSize.height = safeArea.height + effMargins.top  + effMargins.bottom;
            cp->margins = effMargins; // persist the (possibly new) margins
            std::cerr << "  Safe area  : " << safeArea.width << 'x' << safeArea.height << '\n'
                      << "  Canvas size: " << cp->canvasSize.width << 'x' << cp->canvasSize.height
                      << " (safe area + margins)\n";
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n'; return 1;
        }
    }

    // Optional template colour overrides — silently ignored on bad format.
    if (opts.has("margins-tpl-color")) {
        if (const auto col = parseColour(opts.get("margins-tpl-color")))
            cp->visualColour = *col;
        else
            std::cerr << "Warning: ignoring bad --margins-tpl-color value '"
                      << opts.get("margins-tpl-color") << "'\n";
    }
    if (opts.has("background-tpl-color")) {
        if (const auto col = parseColour(opts.get("background-tpl-color")))
            cp->backgroundColour = *col;
        else
            std::cerr << "Warning: ignoring bad --background-tpl-color value '"
                      << opts.get("background-tpl-color") << "'\n";
    }

    try {
        WorkspaceSerializer{}.save(ws, wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n';
        return 2;
    }

    std::cerr << "Profile '" << name << "' updated.\n";
    return 0;
}

// ===========================================================================
// platemaker workspace rm-profile
// ===========================================================================

static int cmdWorkspaceRmProfile(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }
    if (!opts.has("name")) {
        std::cerr << "Error: --name NAME is required\n"; return 1;
    }

    const std::string wsFile = opts.get("workspace");
    const std::string name   = opts.get("name");

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    const auto sizeBefore = ws.canvasProfiles.size();
    ws.canvasProfiles.erase(
        std::remove_if(ws.canvasProfiles.begin(), ws.canvasProfiles.end(),
            [&name](const CanvasProfile& p) { return p.name == name; }),
        ws.canvasProfiles.end());

    if (ws.canvasProfiles.size() == sizeBefore) {
        std::cerr << "Error: profile '" << name << "' not found\n"; return 1;
    }

    try {
        WorkspaceSerializer{}.save(ws, wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n';
        return 2;
    }

    std::cerr << "Profile '" << name << "' removed.\n";
    return 0;
}

// ===========================================================================
// platemaker workspace list-profiles
// ===========================================================================

static int cmdWorkspaceListProfiles(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(opts.get("workspace"));
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    if (ws.canvasProfiles.empty()) {
        std::cout << "Canvas profiles: (none)\n";
        std::cout << "  Tip: workspace add-profile --workspace FILE"
                  << " --name NAME --canvas WxH --margins T,R,B,L\n";
    } else {
        std::cout << "Canvas profiles:\n";
        for (const auto& cp : ws.canvasProfiles) {
            const int safeW = cp.canvasSize.width
                              - cp.margins.left - cp.margins.right;
            const int safeH = cp.canvasSize.height
                              - cp.margins.top  - cp.margins.bottom;
            std::cout << "  " << cp.name
                      << "  id=" << cp.id
                      << "  canvas=" << cp.canvasSize.width << 'x'
                                     << cp.canvasSize.height
                      << "  margins=" << cp.margins.top    << ','
                                      << cp.margins.right  << ','
                                      << cp.margins.bottom << ','
                                      << cp.margins.left
                      << "  safe-area=" << safeW << 'x' << safeH
                      << '\n';
        }
    }

    std::cout << "\nOutput profiles:\n";
    if (ws.outputProfiles.empty()) {
        std::cout << "  (none)\n";
    } else {
        for (const auto& op : ws.outputProfiles) {
            std::cout << "  " << op.name
                      << "  id=" << op.id
                      << "  target=" << op.targetWidth << "px"
                      << "  slice=" << op.sliceHeight << "px"
                      << '\n';
        }
    }

    return 0;
}

// ===========================================================================
// platemaker process
// ===========================================================================

static int cmdProcess(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }

    const std::string wsFile = opts.get("workspace");

    // --- Load workspace ---
    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    // -----------------------------------------------------------------------
    // Resolve the working ProjectItem.
    //
    // Priority:
    //   1. --project NAME  → named project from workspace
    //   2. --input DIR     → find by inputDirectory or create new in-flight
    //   3. Neither         → error
    // -----------------------------------------------------------------------

    // Output profile is resolved once we know the project (see below), then CLI
    // flags override individual fields on top of the resolved profile.
    OutputProfile outProfile;

    const bool jsonMode    = opts.has("json");
    const bool noProfile   = opts.has("no-profile");
    const bool hasProfiles = !ws.canvasProfiles.empty() && !noProfile;

    int projectIdx = -1; // index into ws.projectItems; -1 = new project

    if (opts.has("project")) {
        const std::string projName = opts.get("project");
        for (int i = 0; i < static_cast<int>(ws.projectItems.size()); ++i)
            if (ws.projectItems[static_cast<std::size_t>(i)].name == projName)
                { projectIdx = i; break; }
        if (projectIdx < 0) {
            std::cerr << "Error: project '" << projName
                      << "' not found in workspace.\n"
                      << "  Use 'workspace list-projects --workspace " << wsFile
                      << "' to see available projects.\n";
            return 1;
        }
    } else if (opts.has("input")) {
        fs::path inputDir = opts.get("input");
        if (!fs::is_directory(inputDir)) {
            std::cerr << "Error: --input '" << inputDir.string()
                      << "' is not a directory\n";
            return 2;
        }
        const std::string absInput = fs::absolute(inputDir).string();

        // Look for an existing project matching this directory.
        for (int i = 0; i < static_cast<int>(ws.projectItems.size()); ++i)
            if (ws.projectItems[static_cast<std::size_t>(i)].inputDirectory == absInput)
                { projectIdx = i; break; }

        if (projectIdx < 0) {
            // No match — create a new project and append to workspace.
            ProjectItem newProj;
            newProj.name           = inputDir.filename().string();
            newProj.uuid           = makeProjectUid(ws);
            newProj.inputDirectory = absInput;
            // Use mergeFileScan() on the empty project to populate the file
            // list via the library layer (same path as updates later on).
            const auto files = scanImageDir(inputDir);
            std::vector<std::string> paths;
            paths.reserve(files.size());
            for (const auto& f : files)
                paths.push_back(fs::absolute(f).string());
            newProj.mergeFileScan(paths);
            ws.projectItems.push_back(std::move(newProj));
            projectIdx = static_cast<int>(ws.projectItems.size()) - 1;
        }
    } else {
        std::cerr << "Error: either --project NAME or --input DIR is required.\n"
                  << "  --project NAME : process an existing workspace project.\n"
                  << "  --input DIR    : process a directory (find or create project).\n";
        return 1;
    }

    ProjectItem& project = ws.projectItems[static_cast<std::size_t>(projectIdx)];

    // Resolve output profile now that we know which project we're processing,
    // then let explicit CLI flags override individual fields.
    outProfile = resolveOutputProfile(ws, project);
    if (opts.has("format"))       outProfile.outputFormat = parseFormat(opts.get("format"));
    if (opts.has("start-index"))  outProfile.startIndex   = opts.getInt("start-index", 1);
    if (opts.has("target-width")) outProfile.targetWidth  = opts.getInt("target-width", 800);
    if (opts.has("slice-height")) outProfile.sliceHeight  = opts.getInt("slice-height", 1280);

    if (project.getInputImages().empty()) {
        std::cerr << "Error: project '" << project.name
                  << "' has no input files.\n";
        return 1;
    }

    // --no-profile is honoured by passing an empty canvas-profile palette (no margin
    // matching). Declared before sanitize() because status refresh, staleness detection
    // and the pipeline run must all agree on which palette is in effect.
    const std::vector<CanvasProfile> noProfiles;
    const std::vector<CanvasProfile>& effectiveProfiles =
        noProfile ? noProfiles : ws.canvasProfiles;

    // --- Sanitize — update file statuses (disk + config) and check if reprocess is needed ---
    project.sanitize(effectiveProfiles);

    // Detect an output-invalidating configuration change (format / slice size /
    // quality / …) by comparing the current profile signature against the one
    // stored at the last render. A mismatch makes every existing slice stale.
    const std::string curSig = Platemaker::Models::outputProfileSignature(outProfile);
    const bool hasOutputs    = !project.getOutputImages().empty();
    const bool sigMismatch =
        !project.outputSignature.empty() && project.outputSignature != curSig;

    // Format change is detectable even without a stored signature (project rendered
    // before signatures existed): the recorded slice extension won't match.
    bool formatMismatch = false;
    if (hasOutputs) {
        const std::string wantExt =
            Platemaker::Models::outputFormatExtension(outProfile.outputFormat);
        const std::string& firstName = project.getOutputImages().front().fileName;
        const auto dot = firstName.find_last_of('.');
        const std::string haveExt =
            (dot == std::string::npos) ? std::string{} : firstName.substr(dot);
        formatMismatch = !haveExt.empty() && haveExt != wantExt;
    }

    // Canvas-profile edits are invisible to every check above: they change neither the
    // input files nor the output files, and outputProfileSignature() covers the output
    // profile only. Without this, editing margins left the project reporting itself up
    // to date while its outputs were stale.
    const auto canvasChange =
        project.detectCanvasConfigChange(effectiveProfiles);

    const bool configChanged =
        hasOutputs && (sigMismatch || formatMismatch || canvasChange.any());

    if (!jsonMode && canvasChange.any()) {
        if (canvasChange.listChanged)
            std::cerr << "Canvas profiles changed since the last render "
                         "(added / removed / reordered) — re-rendering.\n";
        else
            std::cerr << "Canvas profile edited since the last render — "
                      << canvasChange.changedInputs.size()
                      << " page(s) affected; re-rendering.\n";
    }

    if (project.isUpToDate() && !configChanged) {
        if (!jsonMode)
            std::cerr << "Nothing to do: all "
                      << project.getInputImages().size()
                      << " file(s) unchanged since last run (project: "
                      << project.name << ").\n";
        else {
            nlohmann::json j;
            j["sliceCount"]   = 0;
            j["outputFiles"]  = nlohmann::json::array();
            j["skippedPages"] = nlohmann::json::array();
            j["cancelled"]    = false;
            j["incremental"]  = true;
            j["upToDate"]     = true;
            j["project"]      = project.name;
            std::cout << j.dump() << '\n';
        }
        return 0;
    }

    // --- Decide full vs partial re-render -------------------------------------
    // When every input is Processed but some outputs are Missing/Modified, only
    // those slices need regenerating (partial). A config change invalidates every
    // slice, so it always forces a full render.
    const bool partial = !configChanged && project.inputsAllProcessed();
    std::unordered_set<std::string> dirtySlices;
    if (partial) {
        const auto names = project.dirtyOutputNames();
        dirtySlices.insert(names.begin(), names.end());
    }

    // Capture the existing output files so the ones the new configuration no
    // longer produces (e.g. old-format files) can be removed after a full render.
    std::vector<std::string> oldOutputNames;
    const std::string oldOutputDir = project.getOutputDirectory();
    if (configChanged)
        for (const auto& of : project.getOutputImages())
            oldOutputNames.push_back(of.fileName);

    // --- Resolve output directory ---
    // A partial re-render must target the directory the existing outputs live in
    // (the one sanitize() validated them against), ignoring any --output override.
    std::string outputDir = opts.get("output");
    if (partial && !project.getOutputDirectory().empty())
        outputDir = project.getOutputDirectory();
    if (outputDir.empty()) outputDir = project.getOutputDirectory();
    if (outputDir.empty()) outputDir = ws.outputDirectory;
    if (outputDir.empty()) {
        std::cerr << "Error: --output DIR is required "
                     "(project and workspace have no outputDirectory)\n";
        return 1;
    }
    try {
        fs::create_directories(outputDir);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot create output directory: " << e.what() << '\n';
        return 2;
    }

    // --- Pipeline ---
    if (!jsonMode && partial) {
        std::cerr << "Re-rendering " << dirtySlices.size()
                  << " missing/modified slice(s) (inputs unchanged) ...\n";
    } else if (!jsonMode) {
        std::cerr << "Processing " << project.getInputImages().size() << " file(s)";
        if (noProfile)
            std::cerr << " [--no-profile: canvas profiles ignored]";
        else if (hasProfiles)
            std::cerr << " [" << ws.canvasProfiles.size()
                      << " canvas profile(s), matching by width+height]";
        std::cerr << " ...\n";
    }

    // The CLI never cancels, so it uses a token that stays unset.
    Platemaker::Infrastructure::CancellationToken cancelToken;

    const auto outcome = Platemaker::Core::ProcessingPipeline{}.run(
        project.getInputImages(),
        outProfile,
        effectiveProfiles,
        project.canvasProfileIds,
        outputDir,
        cancelToken,
        /*onProgress*/ {},
        /*onLog*/ [&](Platemaker::Core::ProcessingLogLevel level, const std::string& msg) {
            using L = Platemaker::Core::ProcessingLogLevel;
            if (level == L::Info) {
                if (!jsonMode) std::cerr << "  " << msg << '\n';
            } else {
                std::cerr << (level == L::Error ? "Error: " : "Warning: ")
                          << msg << '\n';
            }
        },
        /*onSliceSaved*/ {},
        /*onlySlices*/ partial ? &dirtySlices : nullptr);

    if (outcome.failed)
        return 3;

    // --- Update ProjectItem via library API, then save workspace ---
    if (partial) {
        project.applyPartialResults(outcome.records);
    } else {
        project.applyProcessingResults(outcome.records, outcome.appliedProfiles,
                                       effectiveProfiles, outputDir, nowIso8601());

        // Remove outputs the new configuration no longer produces (e.g. old-format
        // files after a PNG→JPEG switch). The CLI is non-interactive, so it cleans
        // up automatically and reports what it removed.
        if (configChanged && !oldOutputNames.empty()) {
            std::unordered_set<std::string> produced;
            for (const auto& rec : outcome.records) produced.insert(rec.fileName);

            int removed = 0;
            for (const auto& fileName : oldOutputNames) {
                if (produced.count(fileName)) continue;
                std::error_code ec;
                if (fs::remove(fs::path(oldOutputDir) / fileName, ec) && !ec) {
                    ++removed;
                    if (!jsonMode)
                        std::cerr << "  Removed stale output: " << fileName << '\n';
                }
            }
            if (removed > 0 && !jsonMode)
                std::cerr << "Cleaned up " << removed
                          << " stale output file(s) from the previous configuration.\n";
        }
    }

    // Record the configuration that produced these outputs so a later
    // format/size/quality change is detected as stale.
    project.outputSignature = curSig;

    try {
        WorkspaceSerializer{}.save(ws, wsFile);
        if (!jsonMode)
            std::cerr << "Incremental cache saved to " << wsFile << '\n';
    } catch (const std::exception& e) {
        std::cerr << "Warning: could not update workspace: "
                  << e.what() << '\n';
    }

    // --- Summary ---
    std::vector<std::string> outputFiles;
    outputFiles.reserve(outcome.records.size());
    for (const auto& rec : outcome.records)
        outputFiles.push_back(rec.fileName);

    if (jsonMode) {
        nlohmann::json j;
        j["sliceCount"]   = static_cast<int>(outputFiles.size());
        j["outputFiles"]  = outputFiles;
        j["skippedPages"] = outcome.skippedPages;
        j["cancelled"]    = outcome.cancelled;
        j["incremental"]  = partial;
        j["upToDate"]     = false;
        std::cout << j.dump() << '\n';
    } else {
        std::cerr << "Done. " << outputFiles.size()
                  << " slice(s) written to " << outputDir << '\n';
    }

    return 0;
}

// ===========================================================================
// platemaker workspace list-projects
// ===========================================================================

static int cmdWorkspaceListProjects(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(opts.get("workspace"));
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    if (ws.projectItems.empty()) {
        std::cout << "Projects: (none)\n"
                  << "  Tip: platemaker project create --workspace "
                  << opts.get("workspace")
                  << " --name NAME [--input DIR] [--output DIR]\n";
        return 0;
    }

    std::cout << "Projects (" << ws.projectItems.size() << "):\n";
    for (const auto& pi : ws.projectItems) {
        std::cout << "  name          : " << pi.name << '\n'
                  << "    uuid        : " << pi.uuid << '\n'
                  << "    input dir   : "
                  << (pi.inputDirectory.empty() ? "(not set)" : pi.inputDirectory) << '\n'
                  << "    output dir  : "
                  << (pi.getOutputDirectory().empty() ? "(not set)"
                                                      : pi.getOutputDirectory()) << '\n'
                  << "    input files : " << pi.getInputImages().size() << '\n'
                  << "    output files: " << pi.getOutputImages().size() << '\n';
    }
    return 0;
}

// ===========================================================================
// platemaker project create
// ===========================================================================

static int cmdProjectCreate(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }
    if (!opts.has("name")) {
        std::cerr << "Error: --name NAME is required\n"; return 1;
    }

    const std::string wsFile = opts.get("workspace");

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    // Duplicate name check.
    for (const auto& pi : ws.projectItems) {
        if (pi.name == opts.get("name")) {
            std::cerr << "Error: project '" << opts.get("name")
                      << "' already exists. Use 'project mod' to modify it.\n";
            return 1;
        }
    }

    ProjectItem newProj;
    newProj.name = opts.get("name");
    newProj.uuid = makeProjectUid(ws);

    if (opts.has("input")) {
        const fs::path inputDir = opts.get("input");
        if (!fs::is_directory(inputDir)) {
            std::cerr << "Error: --input '" << opts.get("input")
                      << "' is not a directory\n";
            return 2;
        }
        newProj.inputDirectory = fs::absolute(inputDir).string();
        // mergeFileScan() on an empty project just populates the input list;
        // sha256-based rename detection is available on the first mod later.
        const auto files = scanImageDir(inputDir);
        std::vector<std::string> paths;
        paths.reserve(files.size());
        for (const auto& f : files)
            paths.push_back(fs::absolute(f).string());
        newProj.mergeFileScan(paths);
    }

    if (opts.has("output"))
        newProj.getOutputDirectory() = opts.get("output");

    ws.projectItems.push_back(std::move(newProj));

    try {
        WorkspaceSerializer{}.save(ws, wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n';
        return 2;
    }

    std::cerr << "Project '" << opts.get("name") << "' created";
    if (opts.has("input"))
        std::cerr << " (" << ws.projectItems.back().getInputImages().size()
                  << " input file(s) from " << opts.get("input") << ")";
    std::cerr << ".\n";
    return 0;
}

// ===========================================================================
// platemaker project mod
// ===========================================================================

static int cmdProjectMod(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }
    if (!opts.has("name")) {
        std::cerr << "Error: --name NAME is required\n"; return 1;
    }

    const std::string wsFile = opts.get("workspace");

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    ProjectItem* pi = nullptr;
    for (auto& p : ws.projectItems)
        if (p.name == opts.get("name")) { pi = &p; break; }
    if (!pi) {
        std::cerr << "Error: project '" << opts.get("name") << "' not found.\n"
                  << "  Use 'workspace list-projects --workspace " << wsFile
                  << "' to see available projects.\n";
        return 1;
    }

    if (opts.has("new-name")) {
        // Duplicate check for new name.
        for (const auto& p : ws.projectItems) {
            if (p.name == opts.get("new-name") && &p != pi) {
                std::cerr << "Error: project '" << opts.get("new-name")
                          << "' already exists.\n";
                return 1;
            }
        }
        pi->name = opts.get("new-name");
    }

    if (opts.has("input")) {
        const fs::path inputDir = opts.get("input");
        if (!fs::is_directory(inputDir)) {
            std::cerr << "Error: --input '" << opts.get("input")
                      << "' is not a directory\n";
            return 2;
        }
        pi->inputDirectory = fs::absolute(inputDir).string();
        // mergeFileScan() detects renames via sha256 and only invalidates
        // outputs when the strip structure changes (add / remove / reorder).
        const auto files = scanImageDir(inputDir);
        std::vector<std::string> paths;
        paths.reserve(files.size());
        for (const auto& f : files)
            paths.push_back(fs::absolute(f).string());
        const auto scanResult = pi->mergeFileScan(paths);
        std::cerr << "  Input updated: " << pi->getInputImages().size()
                  << " file(s) from " << opts.get("input");
        if (!scanResult.added.empty())
            std::cerr << "  [+" << scanResult.added.size() << " new]";
        if (!scanResult.renamed.empty())
            std::cerr << "  [" << scanResult.renamed.size() << " renamed]";
        if (!scanResult.removed.empty())
            std::cerr << "  [-" << scanResult.removed.size() << " removed]";
        if (scanResult.outputsInvalidated)
            std::cerr << "  [outputs invalidated]";
        std::cerr << '\n';
    }

    if (opts.has("output"))
        pi->getOutputDirectory() = opts.get("output");

    if (opts.has("add-canvas-profile")) {
        const std::string profName = opts.get("add-canvas-profile");
        // Resolve name → ID
        const CanvasProfile* cp = nullptr;
        for (const auto& p : ws.canvasProfiles)
            if (p.name == profName) { cp = &p; break; }
        if (!cp) {
            std::cerr << "Error: canvas profile '" << profName
                      << "' not found in workspace.\n"; return 1;
        }
        if (!pi->addCanvasProfile(ws.canvasProfiles, cp->id)) {
            std::cerr << "Error: cannot link canvas profile '" << profName
                      << "' — conflict: another linked profile has the same canvas dimensions.\n";
            return 1;
        }
        std::cerr << "Canvas profile '" << profName << "' linked to project '" << pi->name << "'.\n";
    }

    if (opts.has("rm-canvas-profile")) {
        const std::string profName = opts.get("rm-canvas-profile");
        const CanvasProfile* cp = nullptr;
        for (const auto& p : ws.canvasProfiles)
            if (p.name == profName) { cp = &p; break; }
        if (!cp) {
            std::cerr << "Error: canvas profile '" << profName
                      << "' not found in workspace.\n"; return 1;
        }
        const auto before = pi->canvasProfileIds.size();
        pi->canvasProfileIds.erase(
            std::remove(pi->canvasProfileIds.begin(), pi->canvasProfileIds.end(), cp->id),
            pi->canvasProfileIds.end());
        if (pi->canvasProfileIds.size() == before)
            std::cerr << "Warning: profile '" << profName
                      << "' was not linked to project '" << pi->name << "'.\n";
        else
            std::cerr << "Canvas profile '" << profName << "' unlinked from project '" << pi->name << "'.\n";
    }

    if (opts.has("output-profile")) {
        const std::string profName = opts.get("output-profile");
        const OutputProfile* op = nullptr;
        for (const auto& p : ws.outputProfiles)
            if (p.name == profName) { op = &p; break; }
        if (!op) {
            std::cerr << "Error: output profile '" << profName
                      << "' not found in workspace.\n"; return 1;
        }
        pi->outputProfileId = op->id;
        std::cerr << "Output profile '" << profName << "' assigned to project '" << pi->name << "'.\n";
    }

    try {
        WorkspaceSerializer{}.save(ws, wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n';
        return 2;
    }

    std::cerr << "Project '" << pi->name << "' updated.\n";
    return 0;
}

// ===========================================================================
// platemaker project rm
// ===========================================================================

static int cmdProjectRm(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }
    if (!opts.has("name")) {
        std::cerr << "Error: --name NAME is required\n"; return 1;
    }

    const std::string wsFile = opts.get("workspace");
    const std::string name   = opts.get("name");

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    const std::size_t sizeBefore = ws.projectItems.size();

    // ProjectItem is move-only so we must use index-based removal.
    for (std::size_t i = 0; i < ws.projectItems.size(); ++i) {
        if (ws.projectItems[i].name == name) {
            ws.projectItems.erase(ws.projectItems.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }

    if (ws.projectItems.size() == sizeBefore) {
        std::cerr << "Error: project '" << name << "' not found.\n";
        return 1;
    }

    try {
        WorkspaceSerializer{}.save(ws, wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n';
        return 2;
    }

    std::cerr << "Project '" << name << "' removed.\n";
    return 0;
}

// ===========================================================================
// platemaker project status
// ===========================================================================

static int cmdProjectStatus(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }
    if (!opts.has("name")) {
        std::cerr << "Error: --name NAME is required\n"; return 1;
    }

    const std::string wsFile = opts.get("workspace");
    const std::string name   = opts.get("name");

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    ProjectItem* pi = nullptr;
    for (auto& p : ws.projectItems)
        if (p.name == name) { pi = &p; break; }
    if (!pi) {
        std::cerr << "Error: project '" << name << "' not found.\n"
                  << "  Use 'workspace list-projects --workspace " << wsFile
                  << "' to see available projects.\n";
        return 1;
    }

    // Sanitize updates file statuses from disk hashes and from the canvas profiles in
    // effect, so pages whose profile changed since their render report DESYNCHRONIZED
    // rather than a misleading PROCESSED.
    const bool upToDate = pi->sanitize(ws.canvasProfiles);

    static const auto statusStr = [](FileStatus s) -> const char* {
        switch (s) {
            case FileStatus::Pending:        return "PENDING";
            case FileStatus::Processed:      return "PROCESSED";
            case FileStatus::Modified:       return "MODIFIED";
            case FileStatus::Missing:        return "MISSING";
            case FileStatus::Desynchronized: return "DESYNC";
            case FileStatus::Done:           return "DONE";
        }
        return "UNKNOWN";
    };

    // Resolve profile names for display.
    std::string canvasProfilesSummary;
    if (pi->canvasProfileIds.empty()) {
        canvasProfilesSummary = "(all workspace profiles)";
    } else {
        for (const auto& id : pi->canvasProfileIds) {
            if (!canvasProfilesSummary.empty()) canvasProfilesSummary += ", ";
            bool found = false;
            for (const auto& cp : ws.canvasProfiles)
                if (cp.id == id) { canvasProfilesSummary += cp.name; found = true; break; }
            if (!found) canvasProfilesSummary += id + " (missing)";
        }
    }
    std::string outputProfileSummary;
    if (pi->outputProfileId.empty()) {
        outputProfileSummary = ws.outputProfiles.empty()
            ? "(none — using built-in default)"
            : ws.outputProfiles.front().name + " (workspace default)";
    } else {
        for (const auto& op : ws.outputProfiles)
            if (op.id == pi->outputProfileId) { outputProfileSummary = op.name; break; }
        if (outputProfileSummary.empty())
            outputProfileSummary = pi->outputProfileId + " (missing)";
    }

    std::cout << "Project: " << pi->name << '\n'
              << "  uuid           : " << pi->uuid << '\n'
              << "  input dir      : "
              << (pi->inputDirectory.empty() ? "(not set)" : pi->inputDirectory) << '\n'
              << "  output dir     : "
              << (pi->getOutputDirectory().empty() ? "(not set)"
                                                   : pi->getOutputDirectory()) << '\n'
              << "  canvas profiles: " << canvasProfilesSummary << '\n'
              << "  output profile : " << outputProfileSummary << '\n'
              << "  up-to-date     : " << (upToDate ? "yes" : "no") << '\n'
              << '\n'
              << "  Input files (" << pi->getInputImages().size() << "):\n";

    for (const auto& inf : pi->getInputImages()) {
        std::cout << "    [" << statusStr(inf.status) << "] "
                  << inf.filePath << '\n';
        if (!inf.lastProcessed.empty())
            std::cout << "      last processed: " << inf.lastProcessed << '\n';
    }

    if (!pi->getOutputImages().empty()) {
        std::cout << "\n  Output files (" << pi->getOutputImages().size() << "):\n";
        for (const auto& outf : pi->getOutputImages())
            std::cout << "    [" << statusStr(outf.status) << "] "
                      << outf.fileName << '\n';
    }

    return 0;
}

// ===========================================================================
// platemaker template
// ===========================================================================

/**
 * \brief Renders a canvas template PNG from a named canvas profile.
 *
 * Colour resolution priority (most to least specific):
 *   1. \c --margins-tpl-color / \c --background-tpl-color CLI arguments.
 *   2. Values stored in the canvas profile (in the workspace JSON).
 *   3. Struct defaults baked into \c CanvasProfile
 *      (pink semi-transparent margin, fully transparent background).
 *
 * Because colour is a cosmetic detail, bad or missing colour arguments are
 * silently ignored rather than causing a hard error.
 */
static int cmdTemplate(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }
    if (!opts.has("profile")) {
        std::cerr << "Error: --profile NAME is required\n"; return 1;
    }
    if (!opts.has("output")) {
        std::cerr << "Error: --output FILE is required\n"; return 1;
    }

    const std::string wsFile      = opts.get("workspace");
    const std::string profileName = opts.get("profile");

    // Templates are always saved as PNG.  If the caller omits the .png
    // extension (or uses a different one), append it so the output file is
    // recognised correctly by all tools.
    std::string outputPath = opts.get("output");
    {
        const fs::path p(outputPath);
        std::string ext = p.extension().string();
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".png") {
            outputPath += ".png";
            std::cerr << "Note: '.png' extension appended; output: " << outputPath << '\n';
        }
    }

    // --- Load workspace ---
    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    // --- Find the requested canvas profile ---
    const CanvasProfile* profilePtr = nullptr;
    for (const auto& cp : ws.canvasProfiles)
        if (cp.name == profileName) { profilePtr = &cp; break; }

    if (!profilePtr) {
        std::cerr << "Error: canvas profile '" << profileName
                  << "' not found in workspace.\n"
                  << "  Use 'workspace list-profiles --workspace " << wsFile
                  << "' to see available profiles.\n";
        return 1;
    }

    // --- Resolve output profile (for slice guide positions) ---
    const OutputProfile outProfile = ws.outputProfiles.empty()
        ? OutputProfile{} : ws.outputProfiles.front();

    // --- Apply optional CLI colour overrides (priority: CLI > profile > default) ---
    // We copy the profile so we can modify colours without touching the workspace.
    CanvasProfile cp = *profilePtr;

    if (opts.has("margins-tpl-color")) {
        if (const auto col = parseColour(opts.get("margins-tpl-color")))
            cp.visualColour = *col;
        else
            std::cerr << "Warning: ignoring bad --margins-tpl-color value '"
                      << opts.get("margins-tpl-color") << "'\n";
    }
    if (opts.has("background-tpl-color")) {
        if (const auto col = parseColour(opts.get("background-tpl-color")))
            cp.backgroundColour = *col;
        else
            std::cerr << "Warning: ignoring bad --background-tpl-color value '"
                      << opts.get("background-tpl-color") << "'\n";
    }

    // --- Ensure output directory exists ---
    const fs::path outPath(outputPath);
    if (const auto parent = outPath.parent_path(); !parent.empty()) {
        try {
            fs::create_directories(parent);
        } catch (const std::exception& e) {
            std::cerr << "Error: cannot create output directory: " << e.what() << '\n';
            return 2;
        }
    }

    // --- Generate ---
    try {
        TemplateGenerator{}.generate(cp, outProfile, outputPath);
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: invalid profile parameters: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: template generation failed: " << e.what() << '\n';
        return 3;
    }

    std::cerr << "Template written: " << fs::absolute(outPath).string() << '\n';
    std::cerr << "  Profile    : " << cp.name << '\n';
    std::cerr << "  Canvas     : " << cp.canvasSize.width << 'x' << cp.canvasSize.height << '\n';
    std::cerr << "  Safe area  : " << cp.safeArea().width << 'x' << cp.safeArea().height << '\n';
    return 0;
}

// ===========================================================================
// main
// ===========================================================================

// Real CLI logic. argv is always UTF-8 (guaranteed by the Windows main() below).
static int runCli(int argc, char** argv)
{
    if (VIPS_INIT(argv[0])) {
        std::cerr << "Fatal: libvips init failed: "
                  << vips_error_buffer() << '\n';
        return 2;
    }

    int exitCode = 0;

    if (argc < 2) {
        cmdHelp(argv[0]);
        exitCode = 1;
    } else {
        const std::string cmd1 = argv[1];

        if (cmd1 == "--version" || cmd1 == "-v") {
            exitCode = cmdVersion();
        } else if (cmd1 == "--help" || cmd1 == "-h" || cmd1 == "help") {
            exitCode = cmdHelp(argv[0]);
        } else if (cmd1 == "workspace") {
            if (argc < 3) {
                std::cerr << "Error: 'workspace' requires a subcommand. "
                             "Run with --help for usage.\n";
                exitCode = 1;
            } else {
                const std::string cmd2 = argv[2];
                Opts opts = parseOpts(argc, argv, 3);
                if      (cmd2 == "create")         exitCode = cmdWorkspaceCreate(opts);
                else if (cmd2 == "add-profile")    exitCode = cmdWorkspaceAddProfile(opts);
                else if (cmd2 == "mod-profile")    exitCode = cmdWorkspaceModProfile(opts);
                else if (cmd2 == "rm-profile")     exitCode = cmdWorkspaceRmProfile(opts);
                else if (cmd2 == "list-profiles")  exitCode = cmdWorkspaceListProfiles(opts);
                else if (cmd2 == "list-projects")  exitCode = cmdWorkspaceListProjects(opts);
                else {
                    std::cerr << "Unknown workspace subcommand '" << cmd2
                              << "'. Run with --help for usage.\n";
                    exitCode = 1;
                }
            }
        } else if (cmd1 == "project") {
            if (argc < 3) {
                std::cerr << "Error: 'project' requires a subcommand. "
                             "Run with --help for usage.\n";
                exitCode = 1;
            } else {
                const std::string cmd2 = argv[2];
                Opts opts = parseOpts(argc, argv, 3);
                if      (cmd2 == "create") exitCode = cmdProjectCreate(opts);
                else if (cmd2 == "mod")    exitCode = cmdProjectMod(opts);
                else if (cmd2 == "rm")     exitCode = cmdProjectRm(opts);
                else if (cmd2 == "status") exitCode = cmdProjectStatus(opts);
                else {
                    std::cerr << "Unknown project subcommand '" << cmd2
                              << "'. Run with --help for usage.\n";
                    exitCode = 1;
                }
            }
        } else if (cmd1 == "process") {
            Opts opts = parseOpts(argc, argv, 2);
            exitCode = cmdProcess(opts);
        } else if (cmd1 == "template") {
            Opts opts = parseOpts(argc, argv, 2);
            exitCode = cmdTemplate(opts);
        } else {
            std::cerr << "Unknown command '" << cmd1
                      << "'. Run with --help for usage.\n";
            exitCode = 1;
        }
    }

    vips_shutdown();
    return exitCode;
}

// ===========================================================================
// Platform entry points
//
// On Windows the shell passes argv in the active console code page (e.g. CP1250
// for a Polish locale), so non-ASCII arguments — e.g. --name "Rozdział 01" —
// arrive as invalid UTF-8 and later break JSON serialisation
// (json.exception.type_error.316). We ignore the ANSI argv entirely and rebuild
// a UTF-8 argv from the real UTF-16 command line, and switch console output to
// UTF-8 so non-ASCII text also prints correctly.
// ===========================================================================
#ifdef _WIN32
int main()
{
    SetConsoleOutputCP(CP_UTF8);

    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) {
        std::cerr << "Fatal: cannot parse the command line.\n";
        return 1;
    }

    // Convert each UTF-16 argument to a UTF-8 std::string (owned by `storage`).
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(wargc));
    for (int i = 0; i < wargc; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                            nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? static_cast<std::size_t>(len - 1) : 0, '\0');
        if (len > 0)
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                s.data(), len, nullptr, nullptr);
        storage.push_back(std::move(s));
    }
    LocalFree(wargv);

    // Build a char* argv[] pointing into `storage` (stable — reserved above).
    std::vector<char*> argv_utf8;
    argv_utf8.reserve(storage.size() + 1);
    for (auto& s : storage) argv_utf8.push_back(s.data());
    argv_utf8.push_back(nullptr);

    const int exitCode = runCli(wargc, argv_utf8.data());

    // Skip the C runtime / loader teardown and terminate immediately.
    //
    // Returning here would run static destructors and then ntdll!LdrShutdownProcess,
    // which unloads libvips' large DLL dependency graph and runs each DLL's
    // DLL_PROCESS_DETACH. That path intermittently deadlocks on a loader RunOnce lock
    // (confirmed by gdb: main thread wedged in LdrShutdownProcess → ZwWaitForAlertByThreadId;
    // reproduces under both MinGW and MSVC, so it is the third-party teardown, not our code
    // or compiler — see docs/TODO.md). A CLI has nothing to clean up that the OS won't
    // reclaim, so we flush our output and hard-exit, bypassing that teardown entirely.
    //
    // Flushing is mandatory: many messages end in '\n' (not std::endl), so their bytes are
    // still buffered; TerminateProcess would otherwise drop them and tests reading stdout
    // would see truncated output. Note _exit()/ExitProcess() do NOT help — they too invoke
    // LdrShutdownProcess; only TerminateProcess bypasses it.
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);   // flush every C stdio buffer as well
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(exitCode));
    return exitCode;        // not reached
}
#else
int main(int argc, char** argv)
{
    return runCli(argc, argv);
}
#endif
