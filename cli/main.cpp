/**
 * \file
 * \brief platemaker CLI — Stage 2 implementation.
 *
 * Supported subcommands:
 *   platemaker --version
 *   platemaker workspace create  --canvas WxH --margins T,R,B,L --output FILE
 *                                [--name NAME] [--target-width N] [--slice-height N]
 *   platemaker workspace list-profiles --workspace FILE
 *   platemaker process --workspace FILE
 *                      [--input DIR] [--output DIR]
 *                      [--format png|jpg|webp] [--start-index N]
 *                      [--target-width N] [--slice-height N]
 *                      [--json]
 *
 * Exit codes:
 *   0  success
 *   1  usage / argument error
 *   2  IO error (file not found, permission denied, …)
 *   3  processing error (libvips failure, pipeline error, …)
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
#include <platemaker/infrastructure/image_io/image_io.hpp>
#include <platemaker/infrastructure/workspace_serializer/workspace_serializer.hpp>
#include <platemaker/models/canvas_profile.hpp>
#include <platemaker/models/common_types.hpp>
#include <platemaker/models/output_profile.hpp>
#include <platemaker/models/page_item.hpp>
#include <platemaker/models/workspace.hpp>

// nlohmann/json is available transitively via libplatemaker (PUBLIC link)
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Version constant (kept in sync with CMakeLists.txt project VERSION)
// ---------------------------------------------------------------------------
static constexpr const char* k_version = "0.1.0";

// ---------------------------------------------------------------------------
// Namespace aliases
// ---------------------------------------------------------------------------
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
 * Call \c has() to test presence, \c get() / \c getInt() to retrieve values.
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

/// Parse argv[start..end) into an Opts map.
static Opts parseOpts(int argc, char** argv, int start)
{
    Opts opts;
    for (int i = start; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.size() < 2 || arg[0] != '-' || arg[1] != '-') continue;
        std::string key = arg.substr(2);
        // If the next token doesn't look like a flag, treat it as the value.
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

/// Zero-pads \p n to at least \p width decimal digits.
static std::string zeroPad(int n, int width)
{
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(width) << n;
    return oss.str();
}

/// Returns the file extension string (".png", ".jpg", ".webp") for a format.
static std::string fmtExt(OutputFormat fmt)
{
    switch (fmt) {
        case OutputFormat::PNG:  return ".png";
        case OutputFormat::JPEG: return ".jpg";
        case OutputFormat::WebP: return ".webp";
    }
    return ".png"; // unreachable, but satisfies -Wreturn-type
}

/// Parses "png"/"jpg"/"jpeg"/"webp" (case-insensitive) → OutputFormat.
static OutputFormat parseFormat(const std::string& s)
{
    std::string lo = s;
    for (auto& c : lo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lo == "png")              return OutputFormat::PNG;
    if (lo == "jpg" || lo == "jpeg") return OutputFormat::JPEG;
    if (lo == "webp")             return OutputFormat::WebP;
    throw std::runtime_error("Unknown format '" + s + "'. Expected png, jpg, or webp.");
}

/// Parses "800x2560" → Size{800, 2560}.
static Size parseSize(const std::string& s)
{
    auto pos = s.find('x');
    if (pos == std::string::npos)
        throw std::runtime_error("Bad canvas format '" + s + "'. Expected WxH, e.g. 800x2560.");
    return {std::stoi(s.substr(0, pos)), std::stoi(s.substr(pos + 1))};
}

/// Parses "100,100,100,100" → Margins{top, right, bottom, left}.
static Margins parseMargins(const std::string& s)
{
    // Replace commas with spaces so we can use >> for all four ints.
    std::string sc = s;
    for (auto& c : sc) if (c == ',') c = ' ';
    std::istringstream ss(sc);
    int vals[4] = {0, 0, 0, 0};
    for (auto& v : vals) ss >> v;
    if (ss.fail())
        throw std::runtime_error(
            "Bad margins format '" + s + "'. Expected T,R,B,L, e.g. 100,100,100,100.");
    return {vals[0], vals[1], vals[2], vals[3]};
}

/// Scans \p dir for image files and returns them sorted by filename.
static std::vector<fs::path> scanImageDir(const fs::path& dir)
{
    static const std::set<std::string> kExts = {
        ".png", ".jpg", ".jpeg", ".tif", ".tiff"
    };
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

/// Returns a pointer to the active CanvasProfile, or nullptr if none is set.
static const CanvasProfile* activeCanvasProfile(const Workspace& ws)
{
    if (ws.activeCanvasProfileName.empty()) return nullptr;
    for (const auto& cp : ws.canvasProfiles) {
        if (cp.name == ws.activeCanvasProfileName) return &cp;
    }
    return nullptr;
}

/// Returns the active OutputProfile, or a default Webtoon profile if none set.
static OutputProfile activeOutputProfile(const Workspace& ws)
{
    if (!ws.activeOutputProfileName.empty()) {
        for (const auto& op : ws.outputProfiles) {
            if (op.name == ws.activeOutputProfileName) return op;
        }
    }
    // Fall back to first profile, then to defaults.
    if (!ws.outputProfiles.empty()) return ws.outputProfiles.front();
    OutputProfile def;
    def.name = "Default";
    return def;
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
// platemaker --help
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
        << "  workspace create --canvas WxH --margins T,R,B,L --output FILE\n"
        << "                   [--name NAME] [--target-width N] [--slice-height N]\n"
        << "      Create a new workspace JSON file.\n"
        << "\n"
        << "  workspace list-profiles --workspace FILE\n"
        << "      Print profile names in an existing workspace.\n"
        << "\n"
        << "  process --workspace FILE\n"
        << "          [--input DIR]  [--output DIR]\n"
        << "          [--format png|jpg|webp]  [--start-index N]\n"
        << "          [--target-width N]  [--slice-height N]\n"
        << "          [--json]\n"
        << "      Scale and slice all pages in the workspace.\n"
        << "\n"
        << "Exit codes: 0=success  1=usage error  2=IO error  3=processing error\n";
    return 0;
}

// ===========================================================================
// platemaker workspace create
// ===========================================================================

static int cmdWorkspaceCreate(const Opts& opts)
{
    // Required arguments.
    if (!opts.has("canvas")) {
        std::cerr << "Error: --canvas WxH is required\n";
        return 1;
    }
    if (!opts.has("margins")) {
        std::cerr << "Error: --margins T,R,B,L is required\n";
        return 1;
    }
    if (!opts.has("output")) {
        std::cerr << "Error: --output FILE is required\n";
        return 1;
    }

    Size    canvasSize;
    Margins margins;
    try {
        canvasSize = parseSize(opts.get("canvas"));
        margins    = parseMargins(opts.get("margins"));
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    const std::string profileName = opts.get("name", "Default");
    const int targetWidth  = opts.getInt("target-width",  800);
    const int sliceHeight  = opts.getInt("slice-height",  1280);
    const std::string outputFile = opts.get("output");

    // Build the CanvasProfile.
    CanvasProfile cp;
    cp.name        = profileName;
    cp.canvasSize  = canvasSize;
    cp.margins     = margins;
    cp.visualColour = {255, 105, 180, 128}; // default pink overlay

    // Build the OutputProfile.
    OutputProfile op;
    op.name            = profileName;
    op.targetWidth     = targetWidth;
    op.sliceHeight     = sliceHeight;
    op.lastSlicePolicy = LastSlicePolicy::KeepAsIs;
    op.outputFormat    = OutputFormat::PNG;
    op.startIndex      = 1;

    // Assemble the workspace.
    Workspace ws;
    ws.version                 = 1;
    ws.canvasProfiles          = {cp};
    ws.outputProfiles          = {op};
    ws.activeCanvasProfileName = profileName;
    ws.activeOutputProfileName = profileName;

    try {
        WorkspaceSerializer{}.save(ws, outputFile);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot write workspace: " << e.what() << '\n';
        return 2;
    }

    std::cerr << "Workspace saved: " << outputFile << '\n';
    return 0;
}

// ===========================================================================
// platemaker workspace list-profiles
// ===========================================================================

static int cmdWorkspaceListProfiles(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n";
        return 1;
    }

    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(opts.get("workspace"));
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    std::cout << "Canvas profiles:\n";
    for (const auto& cp : ws.canvasProfiles)
        std::cout << "  " << cp.name
                  << "  (canvas " << cp.canvasSize.width << 'x' << cp.canvasSize.height
                  << ", margins " << cp.margins.top << ',' << cp.margins.right
                  << ',' << cp.margins.bottom << ',' << cp.margins.left << ")\n";

    std::cout << "Output profiles:\n";
    for (const auto& op : ws.outputProfiles)
        std::cout << "  " << op.name
                  << "  (target " << op.targetWidth
                  << "px, slice " << op.sliceHeight << "px)\n";

    return 0;
}

// ===========================================================================
// platemaker process
// ===========================================================================

static int cmdProcess(const Opts& opts)
{
    if (!opts.has("workspace")) {
        std::cerr << "Error: --workspace FILE is required\n";
        return 1;
    }

    // --- Load workspace ---
    Workspace ws;
    try {
        ws = WorkspaceSerializer{}.load(opts.get("workspace"));
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot load workspace: " << e.what() << '\n';
        return 2;
    }

    // --- Override pages from --input directory ---
    if (opts.has("input")) {
        fs::path inputDir = opts.get("input");
        if (!fs::is_directory(inputDir)) {
            std::cerr << "Error: --input '" << inputDir.string() << "' is not a directory\n";
            return 2;
        }
        auto files = scanImageDir(inputDir);
        ws.pages.clear();
        for (int i = 0; i < static_cast<int>(files.size()); ++i) {
            PageItem pi;
            pi.id       = std::to_string(i);
            pi.filePath = files[static_cast<std::size_t>(i)].string();
            pi.order    = i;
            ws.pages.push_back(pi);
        }
    }

    if (ws.pages.empty()) {
        std::cerr << "Error: no pages found. Use --input DIR or add pages to the workspace.\n";
        return 1;
    }

    // --- Resolve output directory ---
    std::string outputDir = opts.get("output", ws.outputDirectory);
    if (outputDir.empty()) {
        std::cerr << "Error: --output DIR is required (workspace has no outputDirectory)\n";
        return 1;
    }
    try {
        fs::create_directories(outputDir);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot create output directory: " << e.what() << '\n';
        return 2;
    }

    // --- Resolve active profiles, apply CLI overrides ---
    OutputProfile outProfile = activeOutputProfile(ws);

    if (opts.has("format"))       outProfile.outputFormat    = parseFormat(opts.get("format"));
    if (opts.has("start-index"))  outProfile.startIndex      = opts.getInt("start-index", 1);
    if (opts.has("target-width")) outProfile.targetWidth     = opts.getInt("target-width", 800);
    if (opts.has("slice-height")) outProfile.sliceHeight     = opts.getInt("slice-height", 1280);

    const bool jsonMode = opts.has("json");

    // --- Determine if margin-aware pipeline is active ---
    const CanvasProfile* cp        = activeCanvasProfile(ws);
    const bool marginAware =
        cp != nullptr &&
        (cp->margins.top    > 0 || cp->margins.bottom > 0 ||
         cp->margins.left   > 0 || cp->margins.right  > 0);

    if (!jsonMode)
        std::cerr << "Processing " << ws.pages.size() << " page(s)"
                  << (marginAware ? " [margin-aware]" : "") << " ...\n";

    // --- Pipeline: load → (crop) → scale → append ---
    Scaler       scaler;
    MarginCropper cropper;
    ImageIO      imageIO;
    ScaledStrip  strip;

    std::vector<std::string> skippedPages;

    for (const auto& page : ws.pages) {
        try {
            if (marginAware) {
                auto buf     = imageIO.load(page.filePath);
                auto cropped = cropper.crop(buf, cp->margins);
                auto scaled  = scaler.scale(std::move(cropped), page.filePath,
                                            outProfile.targetWidth);
                strip.append(std::move(scaled));
            } else {
                auto scaled = scaler.scale(page.filePath, outProfile.targetWidth);
                strip.append(std::move(scaled));
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: skipping '" << page.filePath
                      << "': " << e.what() << '\n';
            skippedPages.push_back(page.filePath);
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
        const int fileNum   = outProfile.startIndex + slice.index;
        const std::string name = "output_" + zeroPad(fileNum, 3) + ext;
        const std::string path = outputDir + "/" + name;
        try {
            imageIO.save(slice.image, path, outProfile.outputFormat, outProfile.jpegOptions);
            outputFiles.push_back(name);
            if (!jsonMode) std::cerr << "  Saved " << name << '\n';
        } catch (const std::exception& e) {
            std::cerr << "Error: failed to save '" << path << "': " << e.what() << '\n';
            return 3;
        }
    }

    // --- Summary ---
    if (jsonMode) {
        nlohmann::json j;
        j["sliceCount"]       = static_cast<int>(outputFiles.size());
        j["outputFiles"]      = outputFiles;
        j["skippedPages"]     = skippedPages;
        j["cancelled"]        = false;
        std::cout << j.dump() << '\n';
    } else {
        std::cerr << "Done. " << outputFiles.size() << " slice(s) written to "
                  << outputDir << '\n';
    }

    return 0;
}

// ===========================================================================
// main
// ===========================================================================

int main(int argc, char** argv)
{
    // Global libvips initialisation (also initialises GLib/GObject).
    if (VIPS_INIT(argv[0])) {
        std::cerr << "Fatal: libvips init failed: " << vips_error_buffer() << '\n';
        return 2;
    }

    int exitCode = 0;

    if (argc < 2) {
        cmdHelp(argv[0]);
        exitCode = 1;
    } else {
        std::string cmd1 = argv[1];

        if (cmd1 == "--version" || cmd1 == "-v") {
            exitCode = cmdVersion();
        } else if (cmd1 == "--help" || cmd1 == "-h") {
            exitCode = cmdHelp(argv[0]);
        } else if (cmd1 == "workspace" && argc >= 3) {
            std::string cmd2 = argv[2];
            Opts opts = parseOpts(argc, argv, 3);
            if (cmd2 == "create") {
                exitCode = cmdWorkspaceCreate(opts);
            } else if (cmd2 == "list-profiles") {
                exitCode = cmdWorkspaceListProfiles(opts);
            } else {
                std::cerr << "Unknown workspace subcommand '" << cmd2 << "'\n";
                exitCode = 1;
            }
        } else if (cmd1 == "process") {
            Opts opts = parseOpts(argc, argv, 2);
            exitCode = cmdProcess(opts);
        } else {
            std::cerr << "Unknown command '" << cmd1 << "'. Run with --help for usage.\n";
            exitCode = 1;
        }
    }

    vips_shutdown();
    return exitCode;
}
