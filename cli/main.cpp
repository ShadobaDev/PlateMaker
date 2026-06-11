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

#include <platemaker/core/margin_cropper/margin_cropper.hpp>
#include <platemaker/core/scaled_strip/scaled_strip.hpp>
#include <platemaker/core/scaler/scaler.hpp>
#include <platemaker/core/template_generator/template_generator.hpp>
#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/common_types.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>
#include <platemaker/models/workspace.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
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
#include <vector>

// ---------------------------------------------------------------------------
// Version constant (kept in sync with CMakeLists.txt project VERSION)
// ---------------------------------------------------------------------------
static constexpr const char* k_version = "0.1.1";

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

static std::string zeroPad(int n, int width)
{
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(width) << n;
    return oss.str();
}

static std::string fmtExt(OutputFormat fmt)
{
    switch (fmt) {
        case OutputFormat::PNG:  return ".png";
        case OutputFormat::JPEG: return ".jpg";
        case OutputFormat::WebP: return ".webp";
    }
    return ".png";
}

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

/**
 * \brief Reads the pixel width of an image file from its header only (fast).
 *
 * Uses VIPS_ACCESS_SEQUENTIAL so no pixels are decoded; only the image
 * metadata (IHDR for PNG, SOF for JPEG) is read.  Returns -1 on error.
 */
static int getImageWidth(const std::string& filePath)
{
    VipsImage* img = vips_image_new_from_file(
        filePath.c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!img) {
        vips_error_clear();
        return -1;
    }
    const int w = img->Xsize;
    g_object_unref(img);
    return w;
}

/**
 * \brief Reads the pixel height of an image file from its header only (fast).
 *
 * Uses VIPS_ACCESS_SEQUENTIAL so no pixels are decoded; only the image
 * metadata (IHDR for PNG, SOF for JPEG) is read.  Returns -1 on error.
 */
static int getImageHeight(const std::string& filePath)
{
    VipsImage* img = vips_image_new_from_file(
        filePath.c_str(), "access", VIPS_ACCESS_SEQUENTIAL, nullptr);
    if (!img) {
        vips_error_clear();
        return -1;
    }
    const int h = img->Ysize;
    g_object_unref(img);
    return h;
}

/// Returns the active OutputProfile, or a default Webtoon profile if none set.
static OutputProfile activeOutputProfile(const Workspace& ws)
{
    if (!ws.activeOutputProfileName.empty()) {
        for (const auto& op : ws.outputProfiles)
            if (op.name == ws.activeOutputProfileName) return op;
    }
    if (!ws.outputProfiles.empty()) return ws.outputProfiles.front();
    OutputProfile def;
    def.name = "Default";
    return def;
}

/// Finds the first canvas profile whose canvasSize.width matches \p fileWidth.
static const CanvasProfile* findCanvasProfile(
    const std::vector<CanvasProfile>& profiles, int fileWidth, int fileHeight)
{
    for (const auto& cp : profiles)
        if (cp.canvasSize.width  == fileWidth &&
            cp.canvasSize.height == fileHeight)
            return &cp;
    return nullptr;
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

// ===========================================================================
// platemaker --version
// ===========================================================================

static int cmdVersion()
{
    std::cout << k_version << '\n';
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
        << " platemaker project mod     --workspace FILE --name NAME \n"
        << "           [--new-name N] [--input DIR] [--output DIR]\n"
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

    // Build a default "Webtoon Standard" output profile.
    OutputProfile op;
    op.name            = "Webtoon Standard";
    op.targetWidth     = targetWidth;
    op.sliceHeight     = sliceHeight;
    op.lastSlicePolicy = LastSlicePolicy::KeepAsIs;
    op.outputFormat    = OutputFormat::PNG;
    op.startIndex      = 1;

    // Assemble an empty workspace (no canvas profiles yet).
    Workspace ws;
    ws.version                 = 1;
    ws.outputProfiles          = {op};
    ws.activeOutputProfileName = op.name;

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
    if (ws.activeCanvasProfileName.empty())
        ws.activeCanvasProfileName = name;

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

    if (ws.activeCanvasProfileName == name) {
        ws.activeCanvasProfileName = ws.canvasProfiles.empty()
            ? std::string{} : ws.canvasProfiles.front().name;
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
                      << "  canvas=" << cp.canvasSize.width << 'x'
                                     << cp.canvasSize.height
                      << "  margins=" << cp.margins.top    << ','
                                      << cp.margins.right  << ','
                                      << cp.margins.bottom << ','
                                      << cp.margins.left
                      << "  safe-area=" << safeW << 'x' << safeH;
            if (cp.name == ws.activeCanvasProfileName) std::cout << "  [active]";
            std::cout << '\n';
        }
    }

    std::cout << "\nOutput profiles:\n";
    if (ws.outputProfiles.empty()) {
        std::cout << "  (none)\n";
    } else {
        for (const auto& op : ws.outputProfiles) {
            std::cout << "  " << op.name
                      << "  target=" << op.targetWidth << "px"
                      << "  slice=" << op.sliceHeight << "px";
            if (op.name == ws.activeOutputProfileName) std::cout << "  [active]";
            std::cout << '\n';
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

    // Resolve output profile now (before we might return early).
    OutputProfile outProfile = activeOutputProfile(ws);
    if (opts.has("format"))       outProfile.outputFormat = parseFormat(opts.get("format"));
    if (opts.has("start-index"))  outProfile.startIndex   = opts.getInt("start-index", 1);
    if (opts.has("target-width")) outProfile.targetWidth  = opts.getInt("target-width", 800);
    if (opts.has("slice-height")) outProfile.sliceHeight  = opts.getInt("slice-height", 1280);

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
            newProj.uuid           = "proj-" + nowIso8601();
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

    if (project.getInputImages().empty()) {
        std::cerr << "Error: project '" << project.name
                  << "' has no input files.\n";
        return 1;
    }

    // --- Sanitize — update file statuses and check if reprocess is needed ---
    project.sanitize();

    if (project.isUpToDate()) {
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

    // --- Resolve output directory ---
    std::string outputDir = opts.get("output");
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
    if (!jsonMode) {
        std::cerr << "Processing " << project.getInputImages().size() << " file(s)";
        if (noProfile)
            std::cerr << " [--no-profile: canvas profiles ignored]";
        else if (hasProfiles)
            std::cerr << " [" << ws.canvasProfiles.size()
                      << " canvas profile(s), matching by width+height]";
        std::cerr << " ...\n";
    }

    Scaler        scaler;
    MarginCropper cropper;
    ImageIO       imageIO;
    ScaledStrip   strip;

    std::vector<std::string> skippedPages;

    for (const auto& file : project.getInputImages()) {
        if (file.status == FileStatus::Missing) {
            if (!jsonMode)
                std::cerr << "Warning: skipping missing file '"
                          << file.filePath << "'\n";
            skippedPages.push_back(file.filePath);
            continue;
        }
        try {
            const CanvasProfile* matchedProfile = nullptr;
            if (hasProfiles) {
                const int fileWidth  = getImageWidth(file.filePath);
                const int fileHeight = getImageHeight(file.filePath);
                if (fileWidth <= 0 || fileHeight <= 0)
                    throw std::runtime_error("cannot determine image dimensions");
                matchedProfile = findCanvasProfile(ws.canvasProfiles, fileWidth, fileHeight);
                if (!matchedProfile) {
                    if (!jsonMode)
                        std::cerr << "Warning: skipping '" << file.filePath
                                  << "': " << fileWidth << 'x' << fileHeight
                                  << " does not match any canvas profile\n";
                    skippedPages.push_back(file.filePath);
                    continue;
                }
            }

            const bool doMarginCrop =
                matchedProfile != nullptr &&
                (matchedProfile->margins.top    > 0 ||
                 matchedProfile->margins.bottom > 0 ||
                 matchedProfile->margins.left   > 0 ||
                 matchedProfile->margins.right  > 0);

            if (doMarginCrop) {
                auto buf     = imageIO.load(file.filePath);
                auto cropped = cropper.crop(buf, matchedProfile->margins);
                auto scaled  = scaler.scale(std::move(cropped), file.filePath,
                                            outProfile.targetWidth);
                strip.append(std::move(scaled));
            } else {
                auto scaled = scaler.scale(file.filePath, outProfile.targetWidth);
                strip.append(std::move(scaled));
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: skipping '" << file.filePath
                      << "': " << e.what() << '\n';
            skippedPages.push_back(file.filePath);
        }
    }

    if (strip.totalHeight() == 0) {
        std::cerr << "Error: no pages were loaded successfully\n";
        return 3;
    }

    // --- Slice ---
    std::vector<SliceResult> slices;
    try {
        slices = strip.sliceAll(outProfile.sliceHeight, outProfile.lastSlicePolicy);
    } catch (const std::exception& e) {
        std::cerr << "Error: slicing failed: " << e.what() << '\n';
        return 3;
    }

    // --- Save slices ---
    const std::string ext = fmtExt(outProfile.outputFormat);
    std::vector<std::string> outputFiles;

    for (auto& slice : slices) {
        const int fileNum = outProfile.startIndex + slice.index;
        const std::string outName = "output_" + zeroPad(fileNum, 3) + ext;
        const std::string outPath = outputDir + "/" + outName;
        try {
            imageIO.save(slice.image, outPath, outProfile.outputFormat,
                         outProfile.jpegOptions);
            outputFiles.push_back(outName);
            if (!jsonMode) std::cerr << "  Saved " << outName << '\n';
        } catch (const std::exception& e) {
            std::cerr << "Error: failed to save '" << outPath
                      << "': " << e.what() << '\n';
            return 3;
        }
    }

    // --- Update ProjectItem via library API, then save workspace ---
    {
        // Build ProcessingSliceRecord list (Models layer — no pixel data).
        std::vector<ProcessingSliceRecord> records;
        records.reserve(slices.size());
        for (std::size_t si = 0; si < slices.size(); ++si) {
            ProcessingSliceRecord rec;
            rec.fileName     = outputFiles[si];
            rec.outputSha256 = FileMetaData::computeFileSha256(
                                   outputDir + "/" + outputFiles[si]);
            rec.sourceMap    = slices[si].sourceMap;
            records.push_back(std::move(rec));
        }

        // applyProcessingResults() updates sha256 hashes, contributesTo,
        // OutputFile list and rebuilds runtime lookup tables — all in the
        // library layer rather than scattered across CLI code.
        project.applyProcessingResults(records, outputDir, nowIso8601());

        try {
            WorkspaceSerializer{}.save(ws, wsFile);
            if (!jsonMode)
                std::cerr << "Incremental cache saved to " << wsFile << '\n';
        } catch (const std::exception& e) {
            std::cerr << "Warning: could not update workspace: "
                      << e.what() << '\n';
        }
    }

    // --- Summary ---
    if (jsonMode) {
        nlohmann::json j;
        j["sliceCount"]   = static_cast<int>(outputFiles.size());
        j["outputFiles"]  = outputFiles;
        j["skippedPages"] = skippedPages;
        j["cancelled"]    = false;
        j["incremental"]  = false;
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
    newProj.uuid = "proj-" + nowIso8601();

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

    // Sanitize updates file statuses by checking hashes on disk.
    const bool upToDate = pi->sanitize();

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

    std::cout << "Project: " << pi->name << '\n'
              << "  uuid        : " << pi->uuid << '\n'
              << "  input dir   : "
              << (pi->inputDirectory.empty() ? "(not set)" : pi->inputDirectory) << '\n'
              << "  output dir  : "
              << (pi->getOutputDirectory().empty() ? "(not set)"
                                                   : pi->getOutputDirectory()) << '\n'
              << "  up-to-date  : " << (upToDate ? "yes" : "no") << '\n'
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
    const OutputProfile outProfile = activeOutputProfile(ws);

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

int main(int argc, char** argv)
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
