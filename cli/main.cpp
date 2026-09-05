/**
 * \file
 * \brief platemaker CLI — Stage 2 implementation.
 *
 * Supported subcommands:
 *   platemaker --version
 *   platemaker workspace create               [--output FILE] [--target-width N] [--slice-height N]
 *
 *   Canvas profiles (selected by name):
 *   platemaker workspace add-canvas-profile   --workspace FILE --name NAME --canvas WxH --margins T,R,B,L
 *   platemaker workspace mod-canvas-profile   --workspace FILE --name NAME [--canvas WxH] [--margins T,R,B,L]
 *   platemaker workspace rm-canvas-profile    --workspace FILE --name NAME
 *   platemaker workspace list-canvas-profiles --workspace FILE
 *
 *   Output profiles (selected by id — names may repeat, ids do not):
 *   platemaker workspace list-presets         --workspace FILE
 *   platemaker workspace add-output-profile   --workspace FILE --name NAME
 *                       { --from-preset PRESET_ID | [--target-width N] [--slice-height N] [--format png|jpg|webp] }
 *   platemaker workspace mod-output-profile   --workspace FILE --output-profile ID [--name N] [--target-width N] [--slice-height N] [--format png|jpg|webp]
 *   platemaker workspace rm-output-profile    --workspace FILE --output-profile ID
 *   platemaker workspace list-output-profiles --workspace FILE
 *
 *   platemaker workspace list-all-profiles    --workspace FILE   (alias: list-profiles; canvas + output)
 *
 *   Profile portability (bundles / cross-workspace):
 *   platemaker workspace export-profiles      --workspace FILE --out BUNDLE.platemaker.profiles.json [--only NAME,...]
 *   platemaker workspace import-profiles      --workspace FILE --from SOURCE [--only NAME,...]
 *                       (SOURCE is a .platemaker.profiles.json bundle or another .platemaker.json workspace)
 *
 *   platemaker workspace list-projects  --workspace FILE
 *   platemaker project create  --workspace FILE --name NAME [--input DIR] [--output DIR]
 *   platemaker project mod     --workspace FILE --name NAME [--new-name N] [--input DIR] [--output DIR]
 *                              [--add-canvas-profile NAME] [--rm-canvas-profile NAME] [--output-profile ID]
 *   platemaker project duplicate --workspace FILE --name NAME --new-name N [--output DIR]
 *   platemaker project rm      --workspace FILE --name NAME
 *   platemaker project status  --workspace FILE --name NAME
 *   platemaker process --workspace FILE
 *                      { --input DIR | --project NAME }
 *                      [--output DIR] [--output-profile ID]
 *                      [--format png|jpg|webp] [--start-index N]
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
#include <platemaker/infrastructure/build_info/build_info.hpp>
#include <platemaker/infrastructure/control/cancellation_token.hpp>
#include <platemaker/infrastructure/id_generator/id_generator.hpp>
#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/infrastructure/log/log.hpp>
#include <platemaker/infrastructure/profile_bundle_serializer/profile_bundle_serializer.hpp>
#include <platemaker/infrastructure/workspace_editor/workspace_editor.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/infrastructure/file/file_meta_data.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/common_types.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/project_item.hpp>
#include <platemaker/models/workspace.hpp>
#include <platemaker/models/output_presets.hpp>
#include <platemaker/version.hpp>
#include <platemaker/ident.hpp> // PLATEMAKER_IDENT_STRING (build-tree-only version marker)

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
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
#  include <io.h>            // _isatty, _fileno (TTY detection for the process beauty-dump)
#else
#  include <unistd.h>        // isatty, fileno (TTY detection for the process beauty-dump)
#endif

namespace fs = std::filesystem;
using namespace Platemaker::Models;
using namespace Platemaker::Core;
using namespace Platemaker::Infrastructure;

namespace {
// Embedded version marker so platemaker-cli reports its version *without being run* (the runtime twin
// is `--version` above; this is the static, portable analogue of the Windows VERSIONINFO resource):
//   what    platemaker-cli            →  platemaker <ver> (platemaker-cli)
//   strings platemaker-cli | grep @(#)
// `used` (GCC/Clang, incl. MinGW) stops the linker stripping this otherwise-unreferenced symbol.
#if defined(__GNUC__) || defined(__clang__)
[[maybe_unused]] __attribute__((used))
#endif
const char kPlatemakerIdent[] = PLATEMAKER_IDENT_STRING " (platemaker-cli)";
} // namespace

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

// Project uids are now minted by WorkspaceEditor::addProject() — the lib owns identifier generation,
// so the CLI no longer hand-rolls a "proj-" id (see the project-creation call sites).

// ===========================================================================
// platemaker --version
// ===========================================================================

static int cmdVersion()
{
    using namespace Platemaker::Infrastructure;

    // First line stays machine-parseable: "platemaker <version>".
    std::cout << Platemaker::project_name << ' ' << Platemaker::version_string << '\n';
    std::cout << Platemaker::description << '\n';

    // Build identity and linked components come from the lib itself (the honest runtime answer),
    // not from anything hardcoded here.
    const BuildInfo build = buildInfo();
    std::cout << "built with " << build.compiler << " for " << build.platform << '\n';
    for (const LinkedComponent& c : linkedComponents()) {
        std::cout << c.name << ' ' << c.version << " (" << c.licence << ")\n";
    }
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
        << "  workspace add-canvas-profile\n"
        << "      --workspace FILE --name NAME\n"
        << "      { --canvas WxH | --canvas-safe-area WxH }  --margins T,R,B,L\n"
        << "      --canvas and --canvas-safe-area are mutually exclusive.\n"
        << "        --canvas WxH          Absolute canvas size (margins included).\n"
        << "        --canvas-safe-area WxH  Drawable area only; canvas = safe-area + margins.\n"
        << "      Add a canvas profile to an existing workspace.\n"
        << "\n"
        << "  workspace add-canvas-profile / mod-canvas-profile also accept:\n"
        << "    --margins-tpl-color R,G,B[,A]    Margin overlay colour for templates.\n"
        << "    --background-tpl-color R,G,B[,A] Background fill colour for templates.\n"
        << "\n"
        << "  workspace mod-canvas-profile --workspace FILE --name NAME\n"
        << "      [--canvas WxH | --canvas-safe-area WxH]  [--margins T,R,B,L]\n"
        << "      Modify an existing canvas profile.\n"
        << "  workspace rm-canvas-profile   --workspace FILE --name NAME\n"
        << "  workspace list-canvas-profiles --workspace FILE\n"
        << "\n"
        << "  Output profiles are selected by id (names may repeat, ids do not):\n"
        << "  workspace list-presets\n"
        << "      List the built-in output-profile presets.\n"
        << "  workspace add-output-profile --workspace FILE --name NAME\n"
        << "      { --from-preset PRESET_ID | [--target-width N] [--slice-height N] [--format png|jpg|webp] }\n"
        << "      Add an output profile — a copy of a preset, or one from scratch. Prints its id.\n"
        << "  workspace mod-output-profile --workspace FILE --output-profile ID\n"
        << "      [--name N] [--target-width N] [--slice-height N] [--format png|jpg|webp]\n"
        << "  workspace rm-output-profile   --workspace FILE --output-profile ID\n"
        << "  workspace list-output-profiles --workspace FILE\n"
        << "      (Presets are read-only; copy one with add-output-profile --from-preset.)\n"
        << "\n"
        << "  workspace list-all-profiles --workspace FILE   (alias: list-profiles)\n"
        << "      List canvas profiles and output profiles together in one view.\n"
        << "\n"
        << "  workspace export-profiles --workspace FILE --out BUNDLE.platemaker.profiles.json\n"
        << "      [--only NAME,NAME,...]\n"
        << "      Export canvas + output profiles to a portable bundle (templates/presets stripped).\n"
        << "  workspace import-profiles --workspace FILE --from SOURCE [--only NAME,NAME,...]\n"
        << "      Import profiles into the workspace from a bundle or another workspace (SOURCE is\n"
        << "      either a .platemaker.profiles.json or a .platemaker.json). Imports are additive\n"
        << "      copies with fresh ids, so the workspace stays self-contained.\n"
        << "\n"
        << "  process --workspace FILE\n"
        << "          { --input DIR | --project NAME }  [--output DIR]\n"
        << "          [--output-profile ID]\n"
        << "          [--format png|jpg|webp]  [--start-index N]\n"
        << "          [--target-width N]  [--slice-height N]\n"
        << "          [--json]\n"
        << "      Scale and slice all pages in the workspace.\n"
        << "      Output profile: --output-profile ID (or the project's assigned one) is used as\n"
        << "        stored; otherwise the --format/--start-index/--target-width/--slice-height flags\n"
        << "        define an ad-hoc profile. The two do not mix — a stored profile is not edited here.\n"
        << "      If canvas profiles are defined, each file is matched by width+height.\n"
        << "      Files not matching any profile are reported as incompatible.\n"
        << "      --no-profile: ignore canvas profiles; process all files with the\n"
        << "        standard pipeline (no margin cropping) using workspace output settings.\n"
        << "\n"
        << "  template --workspace FILE --profile NAME --output FILE\n"
        << "           [--margins-tpl-color R,G,B[,A]]  [--background-tpl-color R,G,B[,A]]\n"
        << "      Generate a canvas template PNG for use as a Procreate guide layer.\n"
        << "      The template shows the canvas background and margin zones.\n"
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
        << "           [--output-profile     ID]    assign output profile/preset (by id) to project\n"
        << " platemaker project duplicate --workspace FILE --name NAME --new-name N [--output DIR]\n"
        << "      Seed a new project from NAME: its input files + profile links only\n"
        << "      (no outputs, no output directory, fresh render state).\n"
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

    // Assemble an empty workspace (no canvas profiles yet).
    Workspace ws;
    ws.version = 2;

    // Presets are not persisted — the Webtoon Standard preset is always available from the catalogue,
    // so a plain `workspace create` leaves outputProfiles empty (quickstart resolves the preset). Only
    // when --target-width / --slice-height move the settings away from the preset do we store the
    // user's own profile, with a fresh id and a name of its own.
    OutputProfile op = webtoonStandardPreset();
    op.targetWidth   = targetWidth;
    op.sliceHeight   = sliceHeight;
    if (outputProfileSignature(op) != outputProfileSignature(webtoonStandardPreset())) {
        op.name = "Custom";
        WorkspaceEditor(ws).addOutputProfile(std::move(op)); // mints a fresh user id
    }

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
              << "  workspace add-canvas-profile --workspace " << outputFile
              << " --name NAME --canvas WxH --margins T,R,B,L\n";
    return 0;
}

// ===========================================================================
// platemaker workspace add-canvas-profile
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
    for (const auto& cp : ws.canvasProfiles()) {
        if (cp.name == name) {
            std::cerr << "Error: profile '" << name
                      << "' already exists. Use mod-canvas-profile to modify it.\n";
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

    WorkspaceEditor(ws).addCanvasProfile(std::move(cp)); // mints a fresh unique id

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
// platemaker workspace mod-canvas-profile
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

    // The palette is edited through WorkspaceEditor, which owns its invariants; mutate a copy in
    // place, then hand the whole list back via replaceCanvasProfiles (it preserves ids/templateInfo).
    auto           profiles = ws.canvasProfiles(); // copy
    CanvasProfile* cp        = nullptr;
    for (auto& p : profiles)
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

    WorkspaceEditor(ws).replaceCanvasProfiles(std::move(profiles));

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
// platemaker workspace rm-canvas-profile
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

    // Names may repeat, so remove every profile carrying this name (by its id, through the editor).
    std::vector<std::string> toRemove;
    for (const auto& p : ws.canvasProfiles())
        if (p.name == name) toRemove.push_back(p.id);

    if (toRemove.empty()) {
        std::cerr << "Error: profile '" << name << "' not found\n"; return 1;
    }

    WorkspaceEditor ed(ws);
    for (const auto& id : toRemove) ed.removeCanvasProfile(id);

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
// platemaker workspace list-canvas-profiles
// ===========================================================================

//! Prints the "Canvas profiles:" section (with an add tip when the workspace has none).
static void printCanvasProfilesSection(const Workspace& ws)
{
    if (ws.canvasProfiles().empty()) {
        std::cout << "Canvas profiles: (none)\n";
        std::cout << "  Tip: workspace add-canvas-profile --workspace FILE"
                  << " --name NAME --canvas WxH --margins T,R,B,L\n";
        return;
    }
    std::cout << "Canvas profiles:\n";
    for (const auto& cp : ws.canvasProfiles()) {
        const int safeW = cp.canvasSize.width  - cp.margins.left - cp.margins.right;
        const int safeH = cp.canvasSize.height - cp.margins.top  - cp.margins.bottom;
        std::cout << "  " << cp.name
                  << "  id=" << cp.id
                  << "  canvas=" << cp.canvasSize.width << 'x' << cp.canvasSize.height
                  << "  margins=" << cp.margins.top    << ','
                                  << cp.margins.right  << ','
                                  << cp.margins.bottom << ','
                                  << cp.margins.left
                  << "  safe-area=" << safeW << 'x' << safeH
                  << '\n';
    }
}

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

    printCanvasProfilesSection(ws);
    return 0;
}

// ===========================================================================
// platemaker workspace output-profile family (id-selected; presets are read-only)
// ===========================================================================

//! Prints one output-profile row.
static void printOutputProfile(const OutputProfile& op, const char* tag)
{
    std::cout << "  " << op.id
              << "  \"" << op.name << "\""
              << "  " << op.targetWidth << 'x' << op.sliceHeight
              << "  " << outputFormatExtension(op.outputFormat).substr(1)
              << "  " << tag << '\n';
}

//! Prints the "Output profiles:" section — the user's own profiles, then the built-in presets.
static void printOutputProfilesSection(const Workspace& ws)
{
    std::cout << "Output profiles:\n";
    for (const auto& op : ws.outputProfiles())    printOutputProfile(op, "(yours)");
    for (const auto& op : outputProfilePresets()) printOutputProfile(op, "(preset)");
}

static int cmdWorkspaceListPresets(const Opts&)
{
    std::cout << "Available output-profile presets:\n" ;
    for (const auto& p : outputProfilePresets())
        printOutputProfile(p, "(preset)");

    std::cout << "\nCreate your own copy with :\n" <<
                 "   workspace add-output-profile --workspace FILE --name NAME\n" << 
                 "   { --from-preset PRESET_ID | [--target-width N] [--slice-height N] [--format png|jpg|webp] }\n";
    return 0;
}

static int cmdWorkspaceListOutputProfiles(const Opts& opts)
{
    if (!opts.has("workspace")) { std::cerr << "Error: --workspace FILE is required\n"; return 1; }

    Workspace ws;
    try { ws = WorkspaceSerializer{}.load(opts.get("workspace")); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n'; return 2;
    }

    printOutputProfilesSection(ws);
    return 0;
}

// platemaker workspace list-all-profiles (alias: list-profiles) — canvas and output in one view.
static int cmdWorkspaceListAllProfiles(const Opts& opts)
{
    if (!opts.has("workspace")) { std::cerr << "Error: --workspace FILE is required\n"; return 1; }

    Workspace ws;
    try { ws = WorkspaceSerializer{}.load(opts.get("workspace")); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n'; return 2;
    }

    printCanvasProfilesSection(ws);
    std::cout << '\n';
    printOutputProfilesSection(ws);
    return 0;
}

// ===========================================================================
// platemaker workspace export-profiles / import-profiles (profile portability)
// ===========================================================================

//! Splits a comma-separated --only value into a set of names. Empty when the flag is absent.
//! Splits on commas only (profile names contain spaces), trimming whitespace around each name.
static std::set<std::string> parseOnlyNames(const Opts& opts)
{
    std::set<std::string> names;
    if (!opts.has("only")) return names;
    const std::string v = opts.get("only");
    for (std::string::size_type start = 0; start <= v.size();) {
        auto comma = v.find(',', start);
        if (comma == std::string::npos) comma = v.size();
        const auto first = v.find_first_not_of(" \t", start);
        if (first != std::string::npos && first < comma) {
            const auto last = v.find_last_not_of(" \t", comma - 1);
            names.insert(v.substr(first, last - first + 1));
        }
        start = comma + 1;
    }
    return names;
}

// Keeps only profiles whose name is in `keep` (a no-op when `keep` is empty), and records which
// requested names actually matched so the caller can warn about the rest.
template <typename Profiles>
static void filterByName(Profiles& profiles, const std::set<std::string>& keep,
                         std::set<std::string>& matched)
{
    if (keep.empty()) return;
    Profiles out;
    for (auto& p : profiles) {
        if (keep.count(p.name)) {
            matched.insert(p.name);
            out.push_back(std::move(p));
        }
    }
    profiles = std::move(out);
}

static int cmdWorkspaceExportProfiles(const Opts& opts)
{
    if (!opts.has("workspace")) { std::cerr << "Error: --workspace FILE is required\n"; return 1; }
    if (!opts.has("out"))       { std::cerr << "Error: --out FILE is required\n"; return 1; }

    Workspace ws;
    try { ws = WorkspaceSerializer{}.load(opts.get("workspace")); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n'; return 2;
    }

    // Copy the palettes out of the workspace (the accessors return const views). Presets and
    // templateInfo are stripped by the serializer, so nothing to clean up here.
    std::vector<CanvasProfile> canvas(ws.canvasProfiles().begin(), ws.canvasProfiles().end());
    std::vector<OutputProfile> output(ws.outputProfiles().begin(), ws.outputProfiles().end());

    const std::set<std::string> only = parseOnlyNames(opts);
    std::set<std::string>       matched;
    filterByName(canvas, only, matched);
    filterByName(output, only, matched);
    for (const auto& n : only)
        if (!matched.count(n))
            std::cerr << "Warning: --only name '" << n << "' matched no profile.\n";

    if (canvas.empty() && output.empty()) {
        std::cerr << "Error: nothing to export"
                  << (only.empty() ? " (workspace has no profiles)." : " (no --only name matched).")
                  << '\n';
        return 1;
    }

    try { ProfileBundleSerializer{}.save(canvas, output, opts.get("out")); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot write bundle: " << e.what() << '\n'; return 2;
    }

    std::cerr << "Exported " << canvas.size() << " canvas + " << output.size()
              << " output profile(s) to '" << opts.get("out") << "'.\n";
    return 0;
}

static int cmdWorkspaceImportProfiles(const Opts& opts)
{
    if (!opts.has("workspace")) { std::cerr << "Error: --workspace FILE is required\n"; return 1; }
    if (!opts.has("from"))      { std::cerr << "Error: --from FILE is required\n"; return 1; }

    const std::string wsFile   = opts.get("workspace");
    const std::string fromFile = opts.get("from");

    Workspace ws;
    try { ws = WorkspaceSerializer{}.load(wsFile); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n'; return 2;
    }

    // The source is either a profile bundle or a full workspace — both carry the two palettes. A
    // workspace is distinguished by its "projectItems" key; anything else is read as a bundle.
    std::vector<CanvasProfile> canvas;
    std::vector<OutputProfile> output;
    try {
        std::ifstream in(fromFile);
        if (!in.is_open())
            throw std::runtime_error("cannot open '" + fromFile + "'");
        const nlohmann::json probe = nlohmann::json::parse(in);

        if (probe.contains("projectItems")) {
            Workspace src = WorkspaceSerializer{}.load(fromFile); // repairs ids the same way as an open
            canvas.assign(src.canvasProfiles().begin(), src.canvasProfiles().end());
            output.assign(src.outputProfiles().begin(), src.outputProfiles().end());
        } else {
            ProfileBundle b = ProfileBundleSerializer{}.load(fromFile);
            canvas = std::move(b.canvasProfiles);
            output = std::move(b.outputProfiles);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot read source '" << fromFile << "': " << e.what() << '\n';
        return 2;
    }

    const std::set<std::string> only = parseOnlyNames(opts);
    std::set<std::string>       matched;
    filterByName(canvas, only, matched);
    filterByName(output, only, matched);
    for (const auto& n : only)
        if (!matched.count(n))
            std::cerr << "Warning: --only name '" << n << "' matched no profile in the source.\n";

    if (canvas.empty() && output.empty()) {
        std::cerr << "Error: nothing to import"
                  << (only.empty() ? " (source has no profiles)." : " (no --only name matched).")
                  << '\n';
        return 1;
    }

    const ImportProfilesReport report =
        WorkspaceEditor(ws).importProfiles(std::move(canvas), std::move(output));

    try { WorkspaceSerializer{}.save(ws, wsFile); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n'; return 2;
    }

    std::cerr << "Imported " << report.canvasIds.size() << " canvas + "
              << report.outputIds.size() << " output profile(s) into '" << wsFile << "'.\n";
    return 0;
}

static int cmdWorkspaceAddOutputProfile(const Opts& opts)
{
    if (!opts.has("workspace")) { std::cerr << "Error: --workspace FILE is required\n"; return 1; }
    if (!opts.has("name"))      { std::cerr << "Error: --name NAME is required\n"; return 1; }

    const std::string wsFile = opts.get("workspace");

    Workspace ws;
    try { ws = WorkspaceSerializer{}.load(wsFile); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n'; return 2;
    }

    OutputProfile op;
    if (opts.has("from-preset")) {
        // Instantiate a preset into an editable copy the user owns (a fresh id, below).
        const std::string key = opts.get("from-preset");
        const auto preset = outputProfilePresetById(key);
        if (!preset) {
            std::cerr << "Error: no preset with id '" << key
                      << "'. Run 'workspace list-presets'.\n";
            return 1;
        }
        op = *preset;
    } else {
        op.targetWidth = opts.getInt("target-width", 800);
        op.sliceHeight = opts.getInt("slice-height", 1280);
        if (opts.has("format")) {
            try { op.outputFormat = parseFormat(opts.get("format")); }
            catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
        }
    }
    op.name = opts.get("name");
    // The editor mints a fresh user id (never a preset id) and appends it to the palette.
    const std::string newId = WorkspaceEditor(ws).addOutputProfile(std::move(op));

    try { WorkspaceSerializer{}.save(ws, wsFile); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n'; return 2;
    }

    std::cout << "Output profile added: " << newId << "  \"" << opts.get("name") << "\"\n";
    return 0;
}

//! Shared guard: reject an operation that targets a built-in preset id.
static bool refuseIfPreset(const std::string& id, const std::string& wsFile)
{
    if (!outputProfilePresetById(id)) return false;
    std::cerr << "Error: '" << id << "' is a built-in preset and cannot be modified or removed.\n"
              << "  Make an editable copy instead:\n"
              << "    workspace add-output-profile --workspace " << wsFile
              << " --name NAME --from-preset " << id << "\n";
    return true;
}

static int cmdWorkspaceModOutputProfile(const Opts& opts)
{
    if (!opts.has("workspace"))      { std::cerr << "Error: --workspace FILE is required\n"; return 1; }
    if (!opts.has("output-profile")) { std::cerr << "Error: --output-profile ID is required\n"; return 1; }

    const std::string wsFile = opts.get("workspace");
    const std::string id     = opts.get("output-profile");
    if (refuseIfPreset(id, wsFile)) return 1;

    Workspace ws;
    try { ws = WorkspaceSerializer{}.load(wsFile); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n'; return 2;
    }

    // Mutate a copy of the palette, then hand it back through the editor (preset-strip / dedup safe).
    auto           profiles = ws.outputProfiles(); // copy
    OutputProfile* op        = nullptr;
    for (auto& p : profiles) if (p.id == id) { op = &p; break; }
    if (!op) { std::cerr << "Error: output profile '" << id << "' not found\n"; return 1; }

    if (opts.has("name"))         op->name        = opts.get("name");
    if (opts.has("target-width")) op->targetWidth = opts.getInt("target-width", op->targetWidth);
    if (opts.has("slice-height")) op->sliceHeight = opts.getInt("slice-height", op->sliceHeight);
    if (opts.has("format")) {
        try { op->outputFormat = parseFormat(opts.get("format")); }
        catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
    }

    WorkspaceEditor(ws).replaceOutputProfiles(std::move(profiles));

    try { WorkspaceSerializer{}.save(ws, wsFile); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n'; return 2;
    }

    std::cout << "Output profile updated: " << id << '\n';
    return 0;
}

static int cmdWorkspaceRmOutputProfile(const Opts& opts)
{
    if (!opts.has("workspace"))      { std::cerr << "Error: --workspace FILE is required\n"; return 1; }
    if (!opts.has("output-profile")) { std::cerr << "Error: --output-profile ID is required\n"; return 1; }

    const std::string wsFile = opts.get("workspace");
    const std::string id     = opts.get("output-profile");
    if (refuseIfPreset(id, wsFile)) return 1;

    Workspace ws;
    try { ws = WorkspaceSerializer{}.load(wsFile); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n'; return 2;
    }

    if (!WorkspaceEditor(ws).removeOutputProfile(id)) {
        std::cerr << "Error: output profile '" << id << "' not found\n"; return 1;
    }

    try { WorkspaceSerializer{}.save(ws, wsFile); }
    catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n'; return 2;
    }

    std::cout << "Output profile removed: " << id << '\n';
    return 0;
}

// ===========================================================================
// process — human-friendly "beauty-dump" output (non-JSON mode)
// ===========================================================================

namespace {

// UTF-8 byte sequences (the console is switched to CP_UTF8 in main() on Windows). Kept as explicit
// bytes so they render identically regardless of the compiler's execution charset. Always followed by
// a space at the use sites, so the hex escapes never merge with a trailing hex-letter.
namespace sym {
    constexpr const char* ok    = "\xE2\x9C\x94"; // heavy check   ✔
    constexpr const char* warn  = "\xE2\x9A\xA0"; // warning sign  ⚠
    constexpr const char* fail  = "\xE2\x9C\x96"; // heavy cross   ✖
    constexpr const char* play  = "\xE2\x96\xB6"; // play triangle ▶
    constexpr const char* dot   = "\xC2\xB7";     // middle dot    ·
    constexpr const char* arrow = "\xE2\x86\x92"; // rightwards    →
}

bool stderrIsTty()
{
#if defined(_WIN32)
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

const char* categoryTag(Platemaker::Models::ProcessingErrorCategory c)
{
    using C = Platemaker::Models::ProcessingErrorCategory;
    switch (c) {
        case C::Load:         return "load";
        case C::ProfileMatch: return "profile-match";
        case C::Slice:        return "slice";
        case C::Encode:       return "encode";
        case C::Io:           return "io";
        case C::Internal:     return "internal";
    }
    return "error";
}

/**
 * \brief Live, human-readable output for `process` — input tally, strip marker, slice progress and
 *        typed error sections. Non-JSON mode only. Writes to stderr; on a TTY the progress bar
 *        refreshes in place, otherwise it degrades to occasional plain lines. Symbols only, no colour.
 */
class ProcessDump {
public:
    explicit ProcessDump(bool tty) : m_tty(tty) {}

    // Once per input in phase 1 — accumulate the tally and remember skip/error notes for the section.
    void onInput(const Platemaker::Core::InputResult& r)
    {
        using Platemaker::Core::InputStatus;
        switch (r.status) {
            case InputStatus::Appended:                 ++m_appended;  break;
            case InputStatus::AppendedWithoutProfile:   ++m_noProfile; break;
            case InputStatus::AppendedProfileNotLinked: ++m_notLinked; break;
            case InputStatus::SkippedMissing:
                ++m_skipped;
                m_notes.push_back(std::string("     ") + sym::warn + " " +
                                  baseName(r.inputPath) + " \xE2\x80\x94 skipped (missing)");
                break;
            case InputStatus::SkippedError:
                ++m_skipped;
                m_notes.push_back(std::string("     ") + sym::warn + " " + baseName(r.inputPath) +
                                  " \xE2\x80\x94 skipped (" + categoryTag(r.errorCategory) +
                                  (r.detail.empty() ? std::string{} : ": " + r.detail) + ")");
                break;
            default: break;
        }
    }

    // Phase-1 → phase-2 boundary: flush the Inputs section, then announce the strip.
    void slicingStarted(int total)
    {
        printInputs();
        std::cerr << "  Strip    assembled " << sym::arrow << " " << total << " slice(s)\n";
    }

    // Per-slice progress tick.
    void progress(int done, int total, const std::string& name)
    {
        if (m_tty) {
            constexpr int width = 24;
            const int filled = total > 0 ? (done * width) / total : width;
            std::string bar(static_cast<std::size_t>(filled), '#');
            bar.append(static_cast<std::size_t>(width - filled), '-');
            std::string line = "  Slices   [" + bar + "] " + std::to_string(done) + "/" +
                               std::to_string(total) + "  " + name;
            if (line.size() < m_lastLen) line.append(m_lastLen - line.size(), ' ');
            m_lastLen = line.size();
            std::cerr << '\r' << line << std::flush;
            m_barActive = true;
        } else {
            const int pct = total > 0 ? (done * 100) / total : 100;
            if (pct >= m_lastPct + 10 || done == total) {
                m_lastPct = pct - (pct % 10);
                std::cerr << "  Slices   " << done << "/" << total << " (" << pct << "%)\n";
            }
        }
    }

    void sliceSkipped() { ++m_cleanSkipped; }

    void ensureInputsPrinted() { printInputs(); }

    // Normal (non-failed) completion of the slice phase.
    void finishSlices()
    {
        endBar();
        if (m_cleanSkipped > 0)
            std::cerr << "  Skipped  " << m_cleanSkipped
                      << " clean slice(s) (partial re-render)\n";
    }

    // Fatal error: render the typed outcome error.
    void failure(const std::optional<Platemaker::Models::ProcessingError>& err)
    {
        endBar();
        printInputs();
        std::cerr << sym::fail << " FAILED";
        if (err) {
            std::cerr << " \xE2\x80\x94 " << categoryTag(err->category);
            if (!err->slice.empty()) std::cerr << " / " << err->slice;
            std::cerr << "\n    " << err->message << "\n";
        } else {
            std::cerr << "\n";
        }
    }

private:
    void printInputs()
    {
        if (m_inputsPrinted) return;
        m_inputsPrinted = true;
        std::cerr << "  Inputs   " << m_appended << " appended";
        if (m_noProfile) std::cerr << " " << sym::dot << " " << m_noProfile << " no-profile";
        if (m_notLinked) std::cerr << " " << sym::dot << " " << m_notLinked << " unlinked";
        if (m_skipped)   std::cerr << " " << sym::dot << " " << m_skipped << " skipped";
        std::cerr << "\n";
        for (const auto& n : m_notes) std::cerr << n << "\n";
    }

    void endBar()
    {
        if (m_barActive) { std::cerr << '\n'; m_barActive = false; }
    }

    static std::string baseName(const std::string& p)
    {
        const auto pos = p.find_last_of("/\\");
        return pos == std::string::npos ? p : p.substr(pos + 1);
    }

    bool        m_tty;
    int         m_appended = 0, m_noProfile = 0, m_notLinked = 0, m_skipped = 0, m_cleanSkipped = 0;
    std::vector<std::string> m_notes;
    bool        m_inputsPrinted = false;
    bool        m_barActive     = false;
    std::size_t m_lastLen       = 0;
    int         m_lastPct       = -10;
};

} // namespace

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
    const bool hasProfiles = !ws.canvasProfiles().empty() && !noProfile;

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
            // No match — create a new project via the library (which mints the uid) and append.
            ProjectItem& newProj = WorkspaceEditor(ws).addProject(inputDir.filename().string());
            newProj.inputDirectory = absInput;
            // Use mergeFileScan() on the empty project to populate the file
            // list via the library layer (same path as updates later on).
            const auto files = scanImageDir(inputDir);
            std::vector<std::string> paths;
            paths.reserve(files.size());
            for (const auto& f : files)
                paths.push_back(fs::absolute(f).string());
            newProj.mergeFileScan(paths);
            projectIdx = static_cast<int>(ws.projectItems.size()) - 1;
        }
    } else {
        std::cerr << "Error: either --project NAME or --input DIR is required.\n"
                  << "  --project NAME : process an existing workspace project.\n"
                  << "  --input DIR    : process a directory (find or create project).\n";
        return 1;
    }

    ProjectItem& project = ws.projectItems[static_cast<std::size_t>(projectIdx)];

    // Choose the output profile. A *selected* profile — named with --output-profile, or the one the
    // project is assigned — is used exactly as stored (a preset included); it is never edited here.
    // The inline --format / --target-width / --slice-height / --start-index flags instead build an
    // *ad-hoc* profile, used only when nothing is selected — so an override can never silently mutate
    // a stored profile or a preset (that is what duplicating a profile is for).
    std::optional<OutputProfile> selected;
    if (opts.has("output-profile")) {
        selected = Platemaker::Models::resolveOutputProfile(ws, opts.get("output-profile"));
        if (!selected) {
            std::cerr << "Error: no output profile or preset with id '" << opts.get("output-profile")
                      << "'. Run 'workspace list-output-profiles'.\n";
            return 1;
        }
    } else if (!project.outputProfileId().empty()) {
        selected = Platemaker::Models::resolveOutputProfile(ws, project.outputProfileId());
    }

    const bool hasInlineOverrides = opts.has("format") || opts.has("start-index")
                                 || opts.has("target-width") || opts.has("slice-height");

    if (selected) {
        if (hasInlineOverrides)
            std::cerr << "Note: --format/--start-index/--target-width/--slice-height ignored — an "
                         "output profile is selected. Edit that profile, or omit it to render ad-hoc.\n";
        outProfile = *selected;
    } else {
        // Ad-hoc: seed from the workspace's first user profile if any, else the Webtoon preset, then
        // apply the inline flags. This is the "process this directory with these settings" path.
        outProfile = ws.outputProfiles().empty()
                         ? Platemaker::Models::webtoonStandardPreset()
                         : ws.outputProfiles().front();
        if (opts.has("format"))       outProfile.outputFormat = parseFormat(opts.get("format"));
        if (opts.has("start-index"))  outProfile.startIndex   = opts.getInt("start-index", 1);
        if (opts.has("target-width")) outProfile.targetWidth  = opts.getInt("target-width", 800);
        if (opts.has("slice-height")) outProfile.sliceHeight  = opts.getInt("slice-height", 1280);
    }

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
        noProfile ? noProfiles : ws.canvasProfiles();

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

    // Reordering / adding / removing inputs shifts the continuous strip, so every downstream slice
    // changes while each file stays byte-identical. Fold it into configChanged so the *full* path runs
    // (applyProcessingResults refreshes the baseline; the partial path would leave it stale forever).
    const bool inputOrderChanged = project.detectInputCompositionChange();

    // Colour correction / overlays change output bytes but touch no input or output file, so — like the
    // output-profile signature — only a stored fingerprint catches it. No empty-guard: "no processing"
    // has an empty signature, so a pre-feature project (empty stored) that stays without processing does
    // not re-render, while enabling the grade or an overlay flips the signature and forces a full render.
    const std::string curProcSig =
        Platemaker::Models::processingConfigSignature(project.colourCorrection, project.getStripOverlays());
    const bool procSigMismatch = project.processingSignature != curProcSig;

    const bool configChanged =
        hasOutputs && (sigMismatch || formatMismatch || canvasChange.anyChanged() || inputOrderChanged
                       || procSigMismatch);

    if (!jsonMode && procSigMismatch)
        std::cerr << "Colour correction / overlays changed since the last render — re-rendering.\n";

    if (!jsonMode && canvasChange.anyChanged()) {
        if (canvasChange.listChanged)
            std::cerr << "Canvas profiles changed since the last render "
                         "(added / removed / reordered) — re-rendering.\n";
        else
            std::cerr << "Canvas profile edited since the last render — "
                      << canvasChange.changedInputs.size()
                      << " page(s) affected; re-rendering.\n";
    }

    if (!jsonMode && inputOrderChanged)
        std::cerr << "Input order/composition changed since the last render — re-rendering.\n";

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
    ProcessDump dump(stderrIsTty());
    if (!jsonMode) {
        std::cerr << sym::play << " " << project.name << " \xE2\x80\x94 ";
        if (partial) {
            std::cerr << "partial re-render of " << dirtySlices.size() << " slice(s)\n";
        } else {
            std::cerr << project.getInputImages().size() << " file(s)";
            if (noProfile)
                std::cerr << " " << sym::dot << " canvas profiles ignored";
            else if (hasProfiles)
                std::cerr << " " << sym::dot << " " << ws.canvasProfiles().size()
                          << " canvas profile(s)";
            std::cerr << "\n";
        }
    }

    // The CLI never cancels, so it uses a token that stays unset.
    Platemaker::Infrastructure::CancellationToken cancelToken;

    Platemaker::Core::ProcessingCallbacks callbacks;
    if (jsonMode) {
        // JSON mode stays quiet on stdout; only surface warnings/errors on stderr.
        callbacks.onLog = [](Platemaker::Core::ProcessingLogLevel level, const std::string& msg) {
            using L = Platemaker::Core::ProcessingLogLevel;
            if (level != L::Info)
                std::cerr << (level == L::Error ? "Error: " : "Warning: ") << msg << '\n';
        };
    } else {
        // Beauty-dump: the input tally / strip marker / progress bar cover the run, so onLog(Info/Warning)
        // is intentionally not wired (it would duplicate onInput and onProgress). Fatal errors surface via
        // outcome.error, post-render failures via applyProcessingResults()'s return — both below.
        // (Per-component diagnostic tracing is a separate channel: the --trace shadow argument, handled
        // in runCli(); its output goes to the logger's sink on stderr, clear of the progress bar.)
        callbacks.onInput          = [&](const Platemaker::Core::InputResult& r)        { dump.onInput(r); };
        callbacks.onSlicingStarted = [&](const Platemaker::Core::SlicingStarted& s)     { dump.slicingStarted(s.expectedSliceCount); };
        callbacks.onProgress       = [&](const Platemaker::Core::ProcessingProgress& p) { dump.progress(p.sliceDone, p.sliceTotal, p.sliceName); };
        callbacks.onSliceSkipped   = [&](const Platemaker::Core::SliceSkipped&)         { dump.sliceSkipped(); };
    }

    const auto t0 = std::chrono::steady_clock::now();

    Platemaker::Core::RenderRequest request;
    request.inputs           = project.inputsInOrder(); // `order` sequence, not stored-vector order
    request.outputProfile    = outProfile;
    request.canvasProfiles   = effectiveProfiles;
    request.canvasProfileIds = project.canvasProfileIds();
    request.outputDirectory  = outputDir;
    request.colourCorrection = project.colourCorrection;
    request.stripOverlays    = project.getStripOverlays();
    if (partial)
        request.onlySlices = dirtySlices;
    // thumbnailCacheDir stays empty: the CLI has nothing to preview, so it pays nothing.

    const auto outcome =
        Platemaker::Core::ProcessingPipeline::render(request, cancelToken, callbacks);

    if (outcome.failed) {
        if (!jsonMode) dump.failure(outcome.error);
        return 3;
    }
    if (!jsonMode) dump.finishSlices();

    // --- Update ProjectItem via library API, then save workspace ---
    std::vector<Platemaker::Models::ProcessingError> postRenderErrors;
    if (partial) {
        project.applyPartialResults(outcome.records);
    } else {
        postRenderErrors = project.applyProcessingResults(
                                       outcome.records, outcome.appliedProfiles,
                                       outcome.skippedPages,
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
    // Same, for the colour-correction / overlay config (empty when none is configured).
    project.processingSignature = curProcSig;

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

    const double elapsedSec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (jsonMode) {
        std::vector<std::string> unverified;
        unverified.reserve(postRenderErrors.size());
        for (const auto& e : postRenderErrors) unverified.push_back(e.file);

        nlohmann::json j;
        j["sliceCount"]       = static_cast<int>(outputFiles.size());
        j["outputFiles"]      = outputFiles;
        j["skippedPages"]     = outcome.skippedPages;
        j["unverifiedInputs"] = unverified;   // rendered but unhashable — see FileStatus::Error
        j["cancelled"]        = outcome.cancelled;
        j["incremental"]      = partial;
        j["upToDate"]         = false;
        std::cout << j.dump() << '\n';
    } else {
        // Post-render hash failures: the render succeeded, but these inputs could not be verified and
        // are now FileStatus::Error (they will not be silently reprocessed on the next run).
        if (!postRenderErrors.empty()) {
            std::cerr << "  " << sym::warn << " " << postRenderErrors.size()
                      << " input(s) unverified after render (io):\n";
            for (const auto& e : postRenderErrors)
                std::cerr << "     " << e.file << "\n";
        }
        std::ostringstream secs;
        secs << std::fixed << std::setprecision(1) << elapsedSec;
        std::cerr << sym::ok << " Done in " << secs.str() << "s \xE2\x80\x94 "
                  << outputFiles.size() << " slice(s) " << sym::arrow << " " << outputDir << "\n";
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
                  << "    uid         : " << pi.uid << '\n'
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

    // The library mints the project uid (a workspace-unique concern the consumer must not own).
    ProjectItem& newProj = WorkspaceEditor(ws).addProject(opts.get("name"));

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
        for (const auto& p : ws.canvasProfiles())
            if (p.name == profName) { cp = &p; break; }
        if (!cp) {
            std::cerr << "Error: canvas profile '" << profName
                      << "' not found in workspace.\n"; return 1;
        }
        if (!WorkspaceEditor(ws).addCanvasProfileToProject(*pi, cp->id)) {
            std::cerr << "Error: cannot link canvas profile '" << profName
                      << "' — conflict: another linked profile has the same canvas dimensions.\n";
            return 1;
        }
        std::cerr << "Canvas profile '" << profName << "' linked to project '" << pi->name << "'.\n";
    }

    if (opts.has("rm-canvas-profile")) {
        const std::string profName = opts.get("rm-canvas-profile");
        const CanvasProfile* cp = nullptr;
        for (const auto& p : ws.canvasProfiles())
            if (p.name == profName) { cp = &p; break; }
        if (!cp) {
            std::cerr << "Error: canvas profile '" << profName
                      << "' not found in workspace.\n"; return 1;
        }
        if (!WorkspaceEditor(ws).removeCanvasProfileFromProject(*pi, cp->id))
            std::cerr << "Warning: profile '" << profName
                      << "' was not linked to project '" << pi->name << "'.\n";
        else
            std::cerr << "Canvas profile '" << profName << "' unlinked from project '" << pi->name << "'.\n";
    }

    if (opts.has("output-profile")) {
        // Selected by id, resolved against the user's profiles and the preset catalogue — so a
        // project can be pointed at a preset (by its stable id) just as at a user profile.
        const std::string profId = opts.get("output-profile");
        if (!WorkspaceEditor(ws).setProjectOutputProfile(*pi, profId)) {
            std::cerr << "Error: no output profile or preset with id '" << profId
                      << "'. Run 'workspace list-output-profiles'.\n"; return 1;
        }
        std::cerr << "Output profile '" << profId << "' assigned to project '" << pi->name << "'.\n";
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
// platemaker project duplicate
// ===========================================================================

static int cmdProjectDuplicate(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n"; return 1;
    }
    if (!opts.has("name")) {
        std::cerr << "Error: --name NAME is required (the project to copy)\n"; return 1;
    }
    if (!opts.has("new-name")) {
        std::cerr << "Error: --new-name NAME is required (the copy's name)\n"; return 1;
    }

    const std::string wsFile  = opts.get("workspace");
    const std::string srcName = opts.get("name");
    const std::string newName = opts.get("new-name");

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    // Locate the source project by name.
    ProjectItem* src = nullptr;
    for (auto& p : ws.projectItems)
        if (p.name == srcName) { src = &p; break; }
    if (!src) {
        std::cerr << "Error: project '" << srcName << "' not found.\n"
                  << "  Use 'workspace list-projects --workspace " << wsFile
                  << "' to see available projects.\n";
        return 1;
    }

    // The copy's name must be free.
    for (const auto& p : ws.projectItems) {
        if (p.name == newName) {
            std::cerr << "Error: project '" << newName << "' already exists.\n";
            return 1;
        }
    }

    // Seed a new project from the source: input files + profile links (canvas + output) only. The
    // output directory, the output slice list and all render state are dropped, so the copy starts
    // fresh (inputs Pending) and renders into its own folder. The lib mints the fresh, workspace-unique
    // project uid. (src may dangle once the vector grows on append — read the returned reference.)
    ProjectItem& copy = WorkspaceEditor(ws).duplicateProject(*src, newName);

    // A duplicate deliberately has no output directory — two projects writing one folder would
    // overwrite each other's slices. Let the caller point it at its own here, as 'project create' does.
    if (opts.has("output"))
        copy.getOutputDirectory() = opts.get("output");

    const std::size_t inputCount = copy.getInputImages().size();

    try {
        WorkspaceSerializer{}.save(ws, wsFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot save workspace: " << e.what() << '\n';
        return 2;
    }

    std::cerr << "Project '" << srcName << "' duplicated as '" << newName << "' ("
              << inputCount << " input file(s)).\n";
    if (!opts.has("output"))
        std::cerr << "  Note: no output directory set — use 'project mod --name \"" << newName
                  << "\" --output DIR' before rendering.\n";
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
    const bool upToDate = pi->sanitize(ws.canvasProfiles());

    static const auto statusStr = [](FileStatus s) -> const char* {
        switch (s) {
            case FileStatus::Pending:        return "PENDING";
            case FileStatus::Processed:      return "PROCESSED";
            case FileStatus::Modified:       return "MODIFIED";
            case FileStatus::Missing:        return "MISSING";
            case FileStatus::Desynchronized: return "DESYNC";
            case FileStatus::Done:           return "DONE";
            case FileStatus::Skipped:        return "SKIPPED";
            case FileStatus::Error:          return "ERROR";
        }
        return "UNKNOWN";
    };

    // Resolve profile names for display.
    std::string canvasProfilesSummary;
    if (pi->canvasProfileIds().empty()) {
        canvasProfilesSummary = "(all workspace profiles)";
    } else {
        for (const auto& id : pi->canvasProfileIds()) {
            if (!canvasProfilesSummary.empty()) canvasProfilesSummary += ", ";
            bool found = false;
            for (const auto& cp : ws.canvasProfiles())
                if (cp.id == id) { canvasProfilesSummary += cp.name; found = true; break; }
            if (!found) canvasProfilesSummary += id + " (missing)";
        }
    }
    std::string outputProfileSummary;
    if (pi->outputProfileId().empty()) {
        outputProfileSummary = ws.outputProfiles().empty()
            ? "(none — using built-in default)"
            : ws.outputProfiles().front().name + " (workspace default)";
    } else {
        for (const auto& op : ws.outputProfiles())
            if (op.id == pi->outputProfileId()) { outputProfileSummary = op.name; break; }
        if (outputProfileSummary.empty())
            outputProfileSummary = pi->outputProfileId() + " (missing)";
    }

    std::cout << "Project: " << pi->name << '\n'
              << "  uid            : " << pi->uid << '\n'
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
    for (const auto& cp : ws.canvasProfiles())
        if (cp.name == profileName) { profilePtr = &cp; break; }

    if (!profilePtr) {
        std::cerr << "Error: canvas profile '" << profileName
                  << "' not found in workspace.\n"
                  << "  Use 'workspace list-canvas-profiles --workspace " << wsFile
                  << "' to see available profiles.\n";
        return 1;
    }

    // --- Resolve output profile (only consumed by the currently-disabled slice-guide lines) ---
    const OutputProfile outProfile = ws.outputProfiles().empty()
        ? OutputProfile{} : ws.outputProfiles().front();

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

// Shadow global argument: --trace=0xHEX (or a decimal / 0-prefixed value) turns on the library's
// per-component diagnostic logging (Platemaker::Infrastructure::Log). The value is a bitmask, one
// bit per component — see log.hpp for the assignment (0x1 ProcessingPipeline, 0x2 Scaler,
// 0x4 ScaledStrip, … 0x4000 Memory; ~0 / a big hex enables everything). Recognised anywhere on the command line
// and consumed here, so ordinary subcommand parsing ignores it. Output goes to the logger sink
// (stderr by default). Off unless requested — a normal run stays silent.
static void applyTraceArg(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        std::string val;
        if (a.rfind("--trace=", 0) == 0)          val = a.substr(8);
        else if (a == "--trace" && i + 1 < argc)  val = argv[++i];
        else                                      continue;
        if (val.empty()) continue;

        try {
            const std::uint64_t mask = std::stoull(val, nullptr, 0); // base 0: 0x… hex or decimal
            Platemaker::Infrastructure::Log::setEnabledComponents(mask);
            std::cerr << "Diagnostic tracing enabled (components 0x"
                      << std::hex << mask << std::dec << ").\n";
        } catch (const std::exception&) {
            std::cerr << "Ignoring invalid --trace value '" << val
                      << "' (expected a bitmask, e.g. --trace=0x7).\n";
        }
    }
}

// Real CLI logic. argv is always UTF-8 (guaranteed by the Windows main() below).
static int runCli(int argc, char** argv)
{
    // Print the in-flight C++ exception on a terminate() (uncaught exception / noexcept violation /
    // pure-virtual call / a throw during unwinding) so it is not a silent abort. Cheap C++-side hygiene
    // (see docs/TODO.md); it does NOT catch a hardware fault such as a segfault — that is not a C++
    // exception. The command dispatch below also has its own try/catch; this is the backstop for what
    // escapes it (e.g. anything outside that block).
    std::set_terminate([] {
        if (std::exception_ptr e = std::current_exception()) {
            try { std::rethrow_exception(e); }
            catch (const std::exception& ex) { std::cerr << "Fatal: unhandled exception: " << ex.what() << '\n'; }
            catch (...)                       { std::cerr << "Fatal: unhandled non-standard exception.\n"; }
        } else {
            std::cerr << "Fatal: terminate() called with no active exception.\n";
        }
        std::cerr.flush();
        std::abort();
    });

    if (VIPS_INIT(argv[0])) {
        std::cerr << "Fatal: libvips init failed: "
                  << vips_error_buffer() << '\n';
        return 2;
    }

    // Global diagnostic-tracing switch, applied before any command runs.
    applyTraceArg(argc, argv);

    int exitCode = 0;

    // Top-level safety net: individual commands handle their expected errors and return a code; this
    // catches anything unforeseen that still escapes (a bug, out-of-memory, a non-std throw) so the
    // process reports a diagnostic instead of terminating. (A segfault / null dereference is not a C++
    // exception and is not caught here — that would need a crash handler.)
    try {

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
                if      (cmd2 == "create")              exitCode = cmdWorkspaceCreate(opts);
                // Canvas profiles.
                else if (cmd2 == "add-canvas-profile")   exitCode = cmdWorkspaceAddProfile(opts);
                else if (cmd2 == "mod-canvas-profile")   exitCode = cmdWorkspaceModProfile(opts);
                else if (cmd2 == "rm-canvas-profile")    exitCode = cmdWorkspaceRmProfile(opts);
                else if (cmd2 == "list-canvas-profiles") exitCode = cmdWorkspaceListProfiles(opts);
                // Output profiles (id-selected; presets are read-only, instantiate with --from-preset).
                else if (cmd2 == "add-output-profile")   exitCode = cmdWorkspaceAddOutputProfile(opts);
                else if (cmd2 == "mod-output-profile")   exitCode = cmdWorkspaceModOutputProfile(opts);
                else if (cmd2 == "rm-output-profile")    exitCode = cmdWorkspaceRmOutputProfile(opts);
                else if (cmd2 == "list-output-profiles") exitCode = cmdWorkspaceListOutputProfiles(opts);
                else if (cmd2 == "list-presets")         exitCode = cmdWorkspaceListPresets(opts);
                // Combined view: both families in one listing.
                else if (cmd2 == "list-all-profiles" || cmd2 == "list-profiles")
                    exitCode = cmdWorkspaceListAllProfiles(opts);
                else if (cmd2 == "export-profiles")      exitCode = cmdWorkspaceExportProfiles(opts);
                else if (cmd2 == "import-profiles")      exitCode = cmdWorkspaceImportProfiles(opts);
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
                if      (cmd2 == "create")    exitCode = cmdProjectCreate(opts);
                else if (cmd2 == "mod")       exitCode = cmdProjectMod(opts);
                else if (cmd2 == "duplicate") exitCode = cmdProjectDuplicate(opts);
                else if (cmd2 == "rm")        exitCode = cmdProjectRm(opts);
                else if (cmd2 == "status")    exitCode = cmdProjectStatus(opts);
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

    } catch (const std::exception& e) {
        std::cerr << "Internal error: " << e.what() << '\n'
                  << "This is a bug — please report it (with the command you ran) on the project's issue tracker.\n";
        exitCode = 3;
    } catch (...) {
        std::cerr << "Internal error (non-standard exception).\n"
                  << "This is a bug — please report it (with the command you ran) on the project's issue tracker.\n";
        exitCode = 3;
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
