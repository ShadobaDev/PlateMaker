/**
 * \file
 * \brief platemaker CLI — Stage 2 implementation.
 *
 * Supported subcommands:
 *   platemaker --version
 *   platemaker workspace create      [--output FILE] [--target-width N] [--slice-height N]
 *   platemaker workspace add-profile  --workspace FILE --name NAME --canvas WxH --margins T,R,B,L
 *   platemaker workspace mod-profile  --workspace FILE --name NAME [--canvas WxH] [--margins T,R,B,L]
 *   platemaker workspace rm-profile   --workspace FILE --name NAME
 *   platemaker workspace list-profiles --workspace FILE
 *   platemaker process --workspace FILE
 *                      [--input DIR] [--output DIR]
 *                      [--format png|jpg|webp] [--start-index N]
 *                      [--target-width N] [--slice-height N]
 *                      [--no-profile] [--json]
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
        << "      --workspace FILE --name NAME --canvas WxH --margins T,R,B,L\n"
        << "      Add a canvas profile to an existing workspace.\n"
        << "\n"
        << "  workspace mod-profile\n"
        << "      --workspace FILE --name NAME [--canvas WxH] [--margins T,R,B,L]\n"
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
    if (!opts.has("canvas")) {
        std::cerr << "Error: --canvas WxH is required\n"; return 1;
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
        canvasSize = parseSize(opts.get("canvas"));
        margins    = parseMargins(opts.get("margins"));
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    CanvasProfile cp;
    cp.name         = name;
    cp.canvasSize   = canvasSize;
    cp.margins      = margins;
    cp.visualColour = {255, 105, 180, 128}; // default: pink overlay at 50 % alpha

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

    if (opts.has("canvas")) {
        try { cp->canvasSize = parseSize(opts.get("canvas")); }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n'; return 1;
        }
    }
    if (opts.has("margins")) {
        try { cp->margins = parseMargins(opts.get("margins")); }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << '\n'; return 1;
        }
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
            std::cerr << "Error: --input '" << inputDir.string()
                      << "' is not a directory\n";
            return 2;
        }
        const auto files = scanImageDir(inputDir);
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
        std::cerr << "Error: no pages found. "
                     "Use --input DIR or add pages to the workspace.\n";
        return 1;
    }

    // --- Resolve output directory ---
    const std::string outputDir = opts.get("output", ws.outputDirectory);
    if (outputDir.empty()) {
        std::cerr << "Error: --output DIR is required "
                     "(workspace has no outputDirectory)\n";
        return 1;
    }
    try {
        fs::create_directories(outputDir);
    } catch (const std::exception& e) {
        std::cerr << "Error: cannot create output directory: " << e.what() << '\n';
        return 2;
    }

    // --- Resolve active output profile; apply CLI overrides ---
    OutputProfile outProfile = activeOutputProfile(ws);
    if (opts.has("format"))       outProfile.outputFormat = parseFormat(opts.get("format"));
    if (opts.has("start-index"))  outProfile.startIndex   = opts.getInt("start-index", 1);
    if (opts.has("target-width")) outProfile.targetWidth  = opts.getInt("target-width", 800);
    if (opts.has("slice-height")) outProfile.sliceHeight  = opts.getInt("slice-height", 1280);

    const bool jsonMode   = opts.has("json");
    // --no-profile bypasses canvas profile matching: treat workspace as if it
    // had no canvas profiles (standard pipeline, no margin cropping).
    const bool noProfile  = opts.has("no-profile");
    const bool hasProfiles = !ws.canvasProfiles.empty() && !noProfile;

    if (!jsonMode) {
        std::cerr << "Processing " << ws.pages.size() << " page(s)";
        if (noProfile)
            std::cerr << " [--no-profile: canvas profiles ignored]";
        else if (hasProfiles)
            std::cerr << " [" << ws.canvasProfiles.size()
                      << " canvas profile(s), matching by width+height]";
        std::cerr << " ...\n";
    }

    // --- Pipeline: for each page → detect profile → (crop) → scale → strip ---
    Scaler        scaler;
    MarginCropper cropper;
    ImageIO       imageIO;
    ScaledStrip   strip;

    std::vector<std::string> skippedPages;

    for (const auto& page : ws.pages) {
        try {
            const CanvasProfile* matchedProfile = nullptr;

            if (hasProfiles) {
                // Read image width from the file header (fast — no pixel decode).
                const int fileWidth = getImageWidth(page.filePath);
                if (fileWidth <= 0) {
                    throw std::runtime_error("cannot determine image dimensions");
                }
                const int fileHeight = getImageHeight(page.filePath);
                if (fileHeight <= 0) {
                    throw std::runtime_error("cannot determine image dimensions");
                }
                matchedProfile = findCanvasProfile(ws.canvasProfiles, fileWidth, fileHeight);
                if (!matchedProfile) {
                    if (!jsonMode)
                        std::cerr << "Warning: skipping '" << page.filePath
                                  << "': width=" << fileWidth
                                  << ", height=" << fileHeight
                                  << " does not match any canvas profile\n";
                    skippedPages.push_back(page.filePath);
                    continue;
                }
            }

            // Margin-aware pipeline only when the matched profile has non-zero margins.
            const bool doMarginCrop =
                matchedProfile != nullptr &&
                (matchedProfile->margins.top    > 0 ||
                 matchedProfile->margins.bottom > 0 ||
                 matchedProfile->margins.left   > 0 ||
                 matchedProfile->margins.right  > 0);

            if (doMarginCrop) {
                auto buf     = imageIO.load(page.filePath);
                auto cropped = cropper.crop(buf, matchedProfile->margins);
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
        const int fileNum = outProfile.startIndex + slice.index;
        const std::string name = "output_" + zeroPad(fileNum, 3) + ext;
        const std::string path = outputDir + "/" + name;
        try {
            imageIO.save(slice.image, path, outProfile.outputFormat,
                         outProfile.jpegOptions);
            outputFiles.push_back(name);
            if (!jsonMode) std::cerr << "  Saved " << name << '\n';
        } catch (const std::exception& e) {
            std::cerr << "Error: failed to save '" << path
                      << "': " << e.what() << '\n';
            return 3;
        }
    }

    // --- Summary ---
    if (jsonMode) {
        nlohmann::json j;
        j["sliceCount"]   = static_cast<int>(outputFiles.size());
        j["outputFiles"]  = outputFiles;
        j["skippedPages"] = skippedPages;
        j["cancelled"]    = false;
        std::cout << j.dump() << '\n';
    } else {
        std::cerr << "Done. " << outputFiles.size()
                  << " slice(s) written to " << outputDir << '\n';
    }

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
                if      (cmd2 == "create")        exitCode = cmdWorkspaceCreate(opts);
                else if (cmd2 == "add-profile")   exitCode = cmdWorkspaceAddProfile(opts);
                else if (cmd2 == "mod-profile")   exitCode = cmdWorkspaceModProfile(opts);
                else if (cmd2 == "rm-profile")    exitCode = cmdWorkspaceRmProfile(opts);
                else if (cmd2 == "list-profiles") exitCode = cmdWorkspaceListProfiles(opts);
                else {
                    std::cerr << "Unknown workspace subcommand '" << cmd2
                              << "'. Run with --help for usage.\n";
                    exitCode = 1;
                }
            }
        } else if (cmd1 == "process") {
            Opts opts = parseOpts(argc, argv, 2);
            exitCode = cmdProcess(opts);
        } else {
            std::cerr << "Unknown command '" << cmd1
                      << "'. Run with --help for usage.\n";
            exitCode = 1;
        }
    }

    vips_shutdown();
    return exitCode;
}
